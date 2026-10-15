/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 */

/*
 * Musica y avisos sonoros. Es el archivo mas largo de la biblioteca, pero la
 * idea es simple: un hilo reproductor y un buzon.
 *
 * - Las funciones publicas (lib_audio_play, pause, stop, seek...) no tocan el
 *   audio: dejan una orden en g.cmd y despiertan al hilo con
 *   pthread_cond_signal.
 * - El hilo (playback_thread) es el unico que decodifica MP3 y habla con la
 *   tarjeta de sonido. Asi nunca hay dos hilos peleando por ella.
 *
 * El enum PlayCmd son las ordenes posibles: NONE, PLAY, PAUSE, RESUME, STOP,
 * QUIT.
 *
 * Todo el estado vive en una sola variable, g, una estructura sin nombre
 * declarada static:
 *
 * - tracks[64], track_count, audio_dir: La lista de canciones y la carpeta
 * - thread, lock, cond: El hilo, su candado y su variable de condicion
 * - cmd, cmd_filepath, cmd_track_id, seek_req: El buzon: la orden pendiente y
 *   sus datos
 * - status, current_id, position_secs, volume: Que suena, en que segundo y a
 *   que volumen
 * - playlist[64], playlist_n, pl_activa, pl_pos: La playlist y la posicion que
 *   suena
 * - initialized: Si ya se llamo a lib_audio_init
 *
 * Bibliotecas externas: mpg123 decodifica MP3 a muestras de sonido; ALSA
 * (snd_pcm_*) manda esas muestras a la tarjeta y (snd_mixer_*) controla el
 * volumen.
 *
 * Para entender: Una cadena en C es un arreglo de char que termina en '\0'.
 * Por eso al copiar con strncpy(destino, origen, tamano - 1) se escribe a mano
 * destino[tamano - 1] = '\0': si el origen era muy largo, strncpy no pone el
 * terminador.
 */

/* Tipos, constantes LIB_AUDIO_* y prototipos del modulo. */
#include "lib_audio.h"
/* printf, fprintf, snprintf y manejo de archivos (fopen, fgets, rename). */
#include <stdio.h>
/* qsort, para ordenar las pistas por nombre. */
#include <stdlib.h>
/* memset, memcpy, strncpy, strcmp y strcspn. */
#include <string.h>
/* Hilo de reproduccion, mutex y variable de condicion. */
#include <pthread.h>
/* opendir y readdir, para escanear la carpeta de audio. */
#include <dirent.h>
/* access, fsync y unlink. */
#include <unistd.h>
/* Senales (kill): quedo de la version que lanzaba un proceso por cada
   notificacion; hoy solo aparece en codigo comentado. */
#include <signal.h>
/* Constantes de acceso a archivos, como F_OK para access(). */
#include <fcntl.h>
/* waitpid: el mismo caso que signal.h. */
#include <sys/wait.h>
/* strcasecmp, para reconocer la extension .mp3 sin importar mayusculas. */
#include <strings.h>  
/* Decodificador de MP3. */
#include <mpg123.h>
/* ALSA: la salida de audio (PCM) y el control de volumen (mixer). */
#include <alsa/asoundlib.h>

/* ═══════════════════════════════════════════════════════════
   Comandos para el thread de reproduccion
═══════════════════════════════════════════════════════════ */
/* Ordenes que la API le deja al hilo de reproduccion. La API nunca toca el
   decodificador ni ALSA: escribe un comando y despierta al hilo. */
typedef enum {
    /* Sin orden pendiente. */
    PCMD_NONE   = 0,
    /* Reproducir el archivo de cmd_filepath. */
    PCMD_PLAY   = 1,
    /* Pausar la pista que suena. */
    PCMD_PAUSE  = 2,
    /* Reanudar la pista en pausa. */
    PCMD_RESUME = 3,
    /* Detener la reproduccion. */
    PCMD_STOP   = 4,
    /* Terminar el hilo. */
    PCMD_QUIT   = 5,
} PlayCmd;

/* ═══════════════════════════════════════════════════════════
   Parametros Globales
═══════════════════════════════════════════════════════════ */
/* Todo el estado del modulo en una sola estructura estatica, g. */
static struct {
    // Lista de pistas
    /* Pistas encontradas en el ultimo escaneo, y cuantas son. */
    LibAudioTrack   tracks[LIB_AUDIO_TRACKS_MAX];
    int             track_count;
    /* Carpeta donde estan los MP3. */
    char            audio_dir[512];

    // Thread de reproduccion
    /* Hilo que decodifica y escribe en ALSA. */
    pthread_t        thread;
    /* Protege todos los campos de g. */
    pthread_mutex_t  lock;
    /* Despierta al hilo cuando llega un comando o un salto. */
    pthread_cond_t   cond;
    /* Comando pendiente. */
    PlayCmd          cmd;
    /* Archivo e id de la pista del comando PLAY. */
    char             cmd_filepath[512];
    int              cmd_track_id;
    float            seek_req;        /* salto pedido en segundos, o -1 */

    // Estado actual
    /* Reproduciendo, en pausa o detenido. */
    LibAudioStatus   status;
    /* Id de la pista en curso, o -1. */
    int              current_id;
    /* Segundos reproducidos de la pista en curso. */
    float            position_secs;
    /* Volumen de la musica, de 0 a 100. */
    int              volume;          

    // Playlist persistente: ids en orden; pl_pos es la posicion que suena,
    // o -1 si lo que suena es una pista suelta
    int              playlist[LIB_AUDIO_TRACKS_MAX];
    /* Cantidad de pistas de la playlist. */
    int              playlist_n;
    /* 1 si lo que suena es la playlist. */
    int              pl_activa;
    int              pl_pos;

    // PID del proceso de notificación activo
    // pid_t            notify_pid;

    /* 1 cuando lib_audio_init() ya corrio. */
    int              initialized;
} g;

// ═══════════ Helpers PCM ALSA

/* Abre la salida de audio y la configura para muestras de 16 bits con la
   frecuencia y los canales de la pista. Devuelve el dispositivo, o NULL. */
static snd_pcm_t *alsa_open(long rate, int channels)
{
    /*
     * Abre la salida de sonido y la configura para una cancion.
     *
     * 1. snd_pcm_open(&pcm, "hw:1,0", PLAYBACK, 0) - abre la tarjeta 1, que es
     *    la salida PWM que va por GPIO 18 al amplificador.
     * 2. Configura los parametros:
     *    - Muestras intercaladas (izquierda, derecha, izquierda, derecha...).
     *    - 16 bits con signo.
     *    - Los canales de la cancion (1 o 2).
     *    - La frecuencia mas cercana a la de la cancion (por ejemplo 44 100
     *      Hz).
     *    - Un buffer de 4096 cuadros.
     * 3. snd_pcm_hw_params aplica todo; si falla, cierra y devuelve NULL.
     * 4. snd_pcm_prepare la deja lista para recibir sonido.
     */

    /* Dispositivo PCM y codigo de error de ALSA. */
    snd_pcm_t *pcm = NULL;
    int err;

    /* Abre el dispositivo para reproduccion, en modo bloqueante. */
    err = snd_pcm_open(&pcm, LIB_AUDIO_ALSA_DEVICE,
                       SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "[audio] snd_pcm_open: %s\n", snd_strerror(err));
        return NULL;
    }

    /* Parametros de hardware: se reservan en la pila y parten de todo lo que
       el dispositivo admite. */
    snd_pcm_hw_params_t *hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm, hw);
    /* Muestras intercaladas (L R L R), de 16 bits con signo en
       little-endian,
       y con los canales de la pista. */
    snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, hw, (unsigned int)channels);

    /* Frecuencia de muestreo: la mas cercana que el dispositivo acepte. */
    unsigned int r = (unsigned int)rate;
    snd_pcm_hw_params_set_rate_near(pcm, hw, &r, 0);

    // Buffer de 4096 frames para cada track
    snd_pcm_uframes_t buf_sz = 4096;
    snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buf_sz);

    /* Aplica los parametros al dispositivo. */
    err = snd_pcm_hw_params(pcm, hw);
    if (err < 0) {
        fprintf(stderr, "[audio] snd_pcm_hw_params: %s\n", snd_strerror(err));
        snd_pcm_close(pcm);
        return NULL;
    }

    /* Lo deja listo para recibir muestras. */
    snd_pcm_prepare(pcm);
    return pcm;
}

// Mezcla a mono un bloque estereo S16 intercalado (ver LIB_AUDIO_MONO)
static void mezclar_a_mono(unsigned char *buf, size_t bytes, int channels)
{
    /*
     * Convierte un bloque estereo a mono, porque el robot tiene un solo
     * parlante.
     *
     * Recorre cada "cuadro" (una muestra izquierda + una derecha, 4 bytes) y
     * reemplaza las dos por su promedio: (L + R) / 2. La suma se hace en int
     * para que no se desborde de 16 bits.
     *
     * Si la cancion ya es mono, no hace nada.
     *
     * Para entender: Usa memcpy en vez de tratar el buffer como int16_t*
     * porque un arreglo de bytes no garantiza la "alineacion" que algunos
     * procesadores ARM exigen para leer enteros de 16 bits.
     */

#if LIB_AUDIO_MONO
    /* Solo hay algo que mezclar si la pista es estereo. */
    if (channels != 2) return;
    /* Cantidad de cuadros: un cuadro son dos muestras de 16 bits. */
    size_t frames = bytes / sizeof(int16_t[2]);
    /* Cada cuadro se reemplaza por su promedio, repetido en los dos canales. */
    for (size_t i = 0; i < frames; i++) {
        /* memcpy y no un cast: buf es un arreglo de bytes, sin garantia de
           alineacion para int16_t. */
        int16_t lr[2];
        memcpy(lr, buf + i * sizeof(lr), sizeof(lr));
        /* En int para que la suma no desborde los 16 bits. */
        lr[0] = lr[1] = (int16_t)(((int)lr[0] + (int)lr[1]) / 2);
        memcpy(buf + i * sizeof(lr), lr, sizeof(lr));
    }
#else
    /* Sin mezcla a mono la funcion no hace nada; esto silencia el aviso de
       parametros sin usar. */
    (void)buf; (void)bytes; (void)channels;
#endif
}

// Ajusta el volumen del mixer ALSA
static void alsa_set_volume(int vol_pct)
{
    /*
     * Cambia el volumen de la tarjeta.
     *
     * 1. Recorta el porcentaje a 0-100.
     * 2. Abre el mezclador, se conecta a la tarjeta hw:1 y carga sus
     *    controles.
     * 3. Busca el control llamado "PCM".
     * 4. Lee su rango (por ejemplo -10239 a 400) y convierte el porcentaje a
     *    esa escala: min + (max - min) x pct / 100.
     * 5. Lo aplica a todos los canales y cierra el mezclador.
     */

    /* Limita el porcentaje a 0-100. */
    if (vol_pct < 0)   vol_pct = 0;
    if (vol_pct > 100) vol_pct = 100;

    /* Abre el mezclador de ALSA. */
    snd_mixer_t *handle = NULL;
    if (snd_mixer_open(&handle, 0) < 0) return;

    // Apuntar a card 1: la salida PWM analogica de la rpi4 (GPIO 18 con audremap)
    if (snd_mixer_attach(handle, LIB_AUDIO_MIXER_CARD) < 0) {
        snd_mixer_close(handle); return;
    }
    /* Registra y carga los controles de esa tarjeta. */
    snd_mixer_selem_register(handle, NULL, NULL);
    snd_mixer_load(handle);

    /* Identificador del control que se busca: indice 0 y el nombre
       configurado. */
    snd_mixer_selem_id_t *sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, LIB_AUDIO_MIXER_CTRL);

    /* Busca el control. */
    snd_mixer_elem_t *elem = snd_mixer_find_selem(handle, sid);
    if (elem) {
        /* Rango del control, en las unidades propias de la tarjeta. */
        long pmin, pmax;
        snd_mixer_selem_get_playback_volume_range(elem, &pmin, &pmax);
        /* Convierte el porcentaje a esa escala y lo aplica a todos los
           canales. */
        long vol = pmin + (long)(((pmax - pmin) * vol_pct) / 100);
        snd_mixer_selem_set_playback_volume_all(elem, vol);
        printf("[audio] Volumen card 1 '%s': %d%% (raw=%ld)\n",
               LIB_AUDIO_MIXER_CTRL, vol_pct, vol);
    } else {
        fprintf(stderr, "[audio] Control '%s' no encontrado en hw:1\n",
                LIB_AUDIO_MIXER_CTRL);
    }

    /* Cierra el mezclador. */
    snd_mixer_close(handle);
}

// Duracion del MP3 usando mpg123
static int mp3_duration(const char *path)
{
    /*
     * Mide cuantos segundos dura un MP3.
     *
     * 1. Crea un decodificador temporal (mpg123_new).
     * 2. Abre el archivo y lo recorre entero (mpg123_scan) para conocer su
     *    largo exacto en muestras.
     * 3. Lee la frecuencia de muestreo y divide: segundos = muestras /
     *    frecuencia.
     * 4. Cierra y libera el decodificador. Si algo falla, devuelve 0.
     *
     * Ojo: llama a mpg123_param(mh, ...) una linea antes de revisar si mh es
     * NULL. Si mpg123_new fallara, usaria un puntero nulo.
     */

    /* Decodificador temporal, solo para medir la pista. */
    int err_code;
    mpg123_handle *mh = mpg123_new(NULL, &err_code);
    mpg123_param(mh, MPG123_QUIET, 1, 0);  /* suprimir warnings de ID3 malformados */
    if (!mh) return 0;

    /* Duracion en segundos; queda en 0 si no se puede abrir o medir. */
    int secs = 0;
    if (mpg123_open(mh, path) == MPG123_OK) {
        /* Recorre el archivo para conocer su largo exacto en muestras. */
        mpg123_scan(mh);
        /* Largo en muestras, y frecuencia de muestreo para pasarlo a
           segundos. */
        off_t  length = mpg123_length(mh);
        long   rate   = 44100;
        int    ch, enc;
        mpg123_getformat(mh, &rate, &ch, &enc);
        if (rate > 0 && length > 0)
            secs = (int)(length / rate);
        /* Cierra el archivo; despues se libera el decodificador. */
        mpg123_close(mh);
    }
    mpg123_delete(mh);
    return secs;
}

/* ═══════════════════════════════════════════════════════════
   Playlist persistente
═══════════════════════════════════════════════════════════ */

/* Las dos se llaman con g.lock tomado. */
/* Busca una pista por su id. Devuelve NULL si no existe. */
static const LibAudioTrack *pista_por_id(int id)
{
    /*
     * Busca en la lista la cancion con ese id y devuelve un puntero a ella, o
     * NULL. Se llama con el candado tomado.
     */

    for (int i = 0; i < g.track_count; i++)
        if (g.tracks[i].id == id) return &g.tracks[i];
    return NULL;
}

/* Deja pedida la pista de la posicion pl_pos + 1 (circular). 0 si la playlist
   quedo sin pistas validas. */
static int pedir_siguiente_de_playlist(void)
{
    /*
     * Deja pedida la siguiente cancion de la playlist.
     *
     * Avanza pl_pos en forma circular: (pl_pos + 1) % playlist_n, asi despues
     * de la ultima vuelve a la primera. Si esa cancion ya no existe, prueba la
     * siguiente (como mucho tantas veces como canciones hay). Al encontrar
     * una, arma la orden PLAY con su archivo e id y devuelve 1.
     *
     * Ejemplo con 3 canciones: posicion 2 -> (2 + 1) % 3 = 0.
     */

    /* Como maximo se prueban tantas posiciones como pistas tiene la
       playlist. */
    for (int intento = 0; intento < g.playlist_n; intento++) {
        /* Avanza una posicion, volviendo a 0 al pasar la ultima. */
        g.pl_pos = (g.pl_pos + 1) % g.playlist_n;
        /* Pista de esa posicion; si ya no existe, se prueba la siguiente. */
        const LibAudioTrack *t = pista_por_id(g.playlist[g.pl_pos]);
        if (!t) continue;
        /* Deja el comando PLAY armado con el archivo y el id de la pista. */
        strncpy(g.cmd_filepath, t->filepath, sizeof(g.cmd_filepath) - 1);
        g.cmd_filepath[sizeof(g.cmd_filepath) - 1] = '\0';
        g.cmd_track_id = t->id;
        g.cmd          = PCMD_PLAY;
        return 1;
    }
    return 0;
}

/* Arma la ruta del archivo de la playlist dentro de la carpeta de audio. */
static void ruta_playlist(char *out, size_t len)
{
    /*
     * Arma la ruta del archivo de la playlist: carpeta de audio +
     * "/canciones/playlist.txt", con snprintf.
     */

    snprintf(out, len, "%s/%s", g.audio_dir, LIB_AUDIO_PLAYLIST_FILE);
}

/* Lee la playlist del disco y la traduce a ids. Sin archivo, todas las pistas
   en orden. Se llama con g.lock tomado, despues de un escaneo. */
static void cargar_playlist(void)
{
    /*
     * Lee la playlist del disco y la convierte en ids.
     *
     * 1. Si el archivo no existe, la playlist son todas las canciones en orden
     *    y sale.
     * 2. Si existe, lee linea por linea con fgets:
     *    - Quita el salto de linea: linea[strcspn(linea, "\r\n")] = '\0'
     *      (busca el primer \r o \n y pone el terminador ahi).
     *    - Salta las lineas vacias.
     *    - Busca la cancion con ese nombre y agrega su id.
     *    - Si el nombre ya no existe (se borro la cancion), lo ignora.
     *
     * Se guarda por nombre y no por id porque los ids pueden cambiar si se
     * agregan canciones.
     */

    /* Ruta del archivo de la playlist. */
    char ruta[600];
    ruta_playlist(ruta, sizeof(ruta));

    /* La playlist se reconstruye desde cero. */
    g.playlist_n = 0;
    FILE *f = fopen(ruta, "r");
    /* Sin archivo: la playlist son todas las pistas, en el orden del
       escaneo. */
    if (!f) {
        for (int i = 0; i < g.track_count; i++) g.playlist[g.playlist_n++] = g.tracks[i].id;
        return;
    }

    /* Una linea por pista. */
    char linea[LIB_AUDIO_NAME_MAX + 4];
    /* Lee linea por linea, hasta el fin del archivo o hasta llenar la
       playlist. */
    while (fgets(linea, sizeof(linea), f) && g.playlist_n < LIB_AUDIO_TRACKS_MAX) {
        /* Quita el salto de linea final. */
        linea[strcspn(linea, "\r\n")] = '\0';
        /* Las lineas vacias se saltan. */
        if (!linea[0]) continue;
        /* Busca la pista con ese nombre de archivo y agrega su id. */
        for (int i = 0; i < g.track_count; i++) {
            if (strcmp(g.tracks[i].filename, linea) == 0) {
                g.playlist[g.playlist_n++] = g.tracks[i].id;
                break;
            }
        }
        /* Un archivo que ya no esta se ignora: la playlist no se rompe por
           borrar una cancion de la particion. */
    }
    /* Cierra el archivo. */
    fclose(f);
}

/* Escribe en un temporal y lo renombra: un corte de energia a mitad deja la
   playlist vieja o la nueva, nunca una a medias. */
static int guardar_playlist(void)
{
    /*
     * Escribe la playlist de forma segura ante cortes de luz.
     *
     * 1. Abre playlist.txt.tmp y escribe un nombre de archivo por linea.
     * 2. fflush (vacia el buffer de C) y fsync (obliga al sistema a escribir
     *    en la SD de verdad).
     * 3. rename(tmp, ruta): reemplaza el archivo viejo de una sola vez. Si se
     *    va la luz antes, queda el viejo; despues, el nuevo. Nunca uno a
     *    medias.
     * 4. Si el renombrado falla, borra el temporal (unlink).
     */

    /* Ruta definitiva y ruta del temporal. */
    char ruta[600], tmp[620];
    ruta_playlist(ruta, sizeof(ruta));
    snprintf(tmp, sizeof(tmp), "%s.tmp", ruta);

    /* Abre el temporal para escritura. */
    FILE *f = fopen(tmp, "w");
    if (!f) {
        fprintf(stderr, "[audio] No se pudo escribir '%s'\n", tmp);
        return -1;
    }
    /* Un nombre de archivo por linea, en el orden de la playlist. */
    for (int i = 0; i < g.playlist_n; i++) {
        const LibAudioTrack *t = pista_por_id(g.playlist[i]);
        if (t) fprintf(f, "%s\n", t->filename);
    }
    /* Vacia el buffer de stdio y fuerza la escritura al disco antes de
       cerrar. */
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    /* El renombrado es atomico: reemplaza el archivo viejo de una sola vez. */
    if (rename(tmp, ruta) != 0) {
        fprintf(stderr, "[audio] No se pudo reemplazar '%s'\n", ruta);
        /* Si falla, se borra el temporal. */
        unlink(tmp);
        return -1;
    }
    return 0;
}

/* Copia los ids de la playlist. Devuelve cuantos copio. */
int lib_audio_playlist_get(int *ids, int max)
{
    /*
     * Copia los ids de la playlist al arreglo del que llama (como maximo max)
     * y devuelve cuantos copio.
     */

    /* Sin inicializar, sin destino o sin espacio no se copia nada. */
    if (!g.initialized || !ids || max <= 0) return 0;
    pthread_mutex_lock(&g.lock);
    /* No mas de los que caben. */
    int n = g.playlist_n < max ? g.playlist_n : max;
    memcpy(ids, g.playlist, (size_t)n * sizeof(int));
    pthread_mutex_unlock(&g.lock);
    return n;
}

/* Reemplaza la playlist y la guarda en disco. Devuelve 0 o -1. */
int lib_audio_playlist_set(const int *ids, int n)
{
    /*
     * Reemplaza la playlist.
     *
     * 1. Valida: modulo listo, n entre 0 y 64.
     * 2. Con el candado, revisa que todos los ids existan; si uno no, no
     *    cambia nada y devuelve -1.
     * 3. Copia la lista nueva.
     * 4. Si la playlist esta sonando, busca en la lista nueva la cancion
     *    actual para seguir desde ahi. Si quedo vacia, la desactiva.
     * 5. La guarda en disco y devuelve el resultado.
     */

    /* Valida los argumentos. */
    if (!g.initialized || n < 0 || n > LIB_AUDIO_TRACKS_MAX || (n > 0 && !ids)) return -1;

    pthread_mutex_lock(&g.lock);
    /* Todos los ids tienen que existir; con uno invalido no se cambia nada. */
    for (int i = 0; i < n; i++) {
        if (!pista_por_id(ids[i])) {
            pthread_mutex_unlock(&g.lock);
            return -1;
        }
    }
    /* Copia la lista nueva. */
    if (n > 0) memcpy(g.playlist, ids, (size_t)n * sizeof(int));
    g.playlist_n = n;

    /* Si la playlist esta sonando, la posicion sigue a la pista actual; si
       se la quito, la siguiente sera la primera de la lista nueva. */
    if (g.pl_activa) {
        /* Busca en la lista nueva la pista que suena. */
        g.pl_pos = -1;
        for (int i = 0; i < n; i++)
            if (g.playlist[i] == g.current_id) { g.pl_pos = i; break; }
        /* Una playlist vacia deja de estar activa. */
        if (n == 0) g.pl_activa = 0;
    }
    /* Guarda en disco, todavia con el candado tomado. */
    int r = guardar_playlist();
    pthread_mutex_unlock(&g.lock);

    printf("[audio] Playlist guardada: %d pista(s)\n", n);
    return r;
}

/* Empieza a reproducir la playlist desde una posicion. */
int lib_audio_play_playlist(int pos)
{
    /*
     * Empieza la playlist desde una posicion.
     *
     * Truco: pone pl_pos = pos - 1 y llama a pedir_siguiente_de_playlist(),
     * que avanza uno. Asi reutiliza la misma funcion. Si la posicion esta
     * fuera de rango, empieza desde 0.
     */

    if (!g.initialized) return -1;
    pthread_mutex_lock(&g.lock);
    /* Sin pistas no hay nada que reproducir. */
    if (g.playlist_n == 0) {
        pthread_mutex_unlock(&g.lock);
        return -1;
    }
    /* Una posicion fuera de rango se toma como la primera. */
    if (pos < 0 || pos >= g.playlist_n) pos = 0;
    g.pl_activa = 1;
    g.pl_pos    = pos - 1;   /* pedir_siguiente avanza una */
    /* Deja armado el PLAY de esa posicion y despierta al hilo. */
    int ok = pedir_siguiente_de_playlist();
    if (ok) pthread_cond_signal(&g.cond);
    else    g.pl_activa = 0;
    pthread_mutex_unlock(&g.lock);
    return ok ? 0 : -1;
}

/* Posicion de la playlist que suena, o -1. */
int lib_audio_playlist_pos(void)
{
    /*
     * Devuelve la posicion de la playlist que suena, o -1 si lo que suena es
     * una cancion suelta.
     */

    pthread_mutex_lock(&g.lock);
    int p = g.pl_activa ? g.pl_pos : -1;
    pthread_mutex_unlock(&g.lock);
    return p;
}

/* Aplica el salto pedido con lib_audio_seek(), si hay uno. Se llama con
   g.lock tomado y lo devuelve tomado; lo suelta mientras mueve el
   decodificador, que no toca el estado compartido. */
static void aplicar_salto(mpg123_handle *mh, snd_pcm_t *pcm, long rate)
{
    /*
     * Aplica un salto pedido desde la barra de progreso. Se llama con el
     * candado tomado.
     *
     * 1. Si seek_req < 0, no hay salto pendiente.
     * 2. Toma el destino, borra la peticion y suelta el candado (mover el
     *    decodificador puede tardar).
     * 3. mpg123_seek(mh, segundos x frecuencia, SEEK_SET) - salta a esa
     *    muestra.
     * 4. snd_pcm_drop + prepare - descarta lo que estaba en el buffer de la
     *    tarjeta, para que el salto se oiga de inmediato.
     * 5. Vuelve a tomar el candado y actualiza position_secs.
     */

    /* Sin salto pendiente no hay nada que hacer. */
    if (g.seek_req < 0.0f) return;
    /* Toma el destino y borra la peticion. */
    float destino = g.seek_req;
    g.seek_req = -1.0f;
    pthread_mutex_unlock(&g.lock);

    /* Mueve el decodificador a la muestra que corresponde a ese segundo. */
    off_t muestra = mpg123_seek(mh, (off_t)(destino * (float)rate), SEEK_SET);
    /* Lo que ya estaba en el buffer de ALSA es de la posicion vieja: se
       descarta para que el salto se oiga en el acto. */
    snd_pcm_drop(pcm);
    snd_pcm_prepare(pcm);

    /* Con el candado otra vez, publica la posicion nueva. */
    pthread_mutex_lock(&g.lock);
    if (muestra >= 0) {
        g.position_secs = (float)muestra / (float)rate;
        printf("[audio] Salto a %.1fs\n", g.position_secs);
    } else {
        fprintf(stderr, "[audio] No se pudo saltar a %.1fs: %s\n",
                destino, mpg123_strerror(mh));
    }
}

/* ═══════════════════════════════════════════════════════════
   Main Thread de reproducción principal
═══════════════════════════════════════════════════════════ */
/* Cuerpo del hilo de reproduccion. Espera un comando PLAY, abre el archivo
   y la salida, y decodifica y escribe bloque a bloque; entre bloques
   atiende los comandos y los saltos que van llegando. */
static void *playback_thread(void *arg)
{
    /*
     * El hilo reproductor. Tiene dos ciclos: uno por cancion y, dentro, uno
     * por bloque.
     *
     * Ciclo exterior (una vuelta por cancion):
     *
     * 1. Duerme en pthread_cond_wait mientras no haya orden.
     * 2. Copia la orden y sus datos, la marca como atendida y borra cualquier
     *    salto viejo.
     * 3. QUIT -> sale del hilo. Cualquier orden que no sea PLAY se ignora (no
     *    hay nada sonando).
     * 4. Crea el decodificador (una sola vez) y abre el MP3.
     * 5. Lee el formato de la cancion y fuerza 16 bits.
     * 6. Cierra la salida de la cancion anterior y abre una nueva con
     *    alsa_open. Si falla, marca detenido.
     * 7. Publica: sonando, id, posicion 0.
     *
     * Ciclo interior (un bloque de 8 KB por vuelta):
     *
     * 1. Con el candado, revisa el buzon:
     *    - Salto pendiente -> aplicar_salto.
     *    - PAUSE -> vacia la tarjeta, marca pausado y espera. Mientras espera,
     *      si llega un salto lo aplica (se puede saltar en pausa). Con RESUME
     *      vuelve a sonar.
     *    - STOP, PLAY o QUIT -> sale del ciclo interior. Solo STOP limpia el
     *      estado; PLAY y QUIT los atiende el ciclo exterior.
     * 2. mpg123_read decodifica un bloque.
     * 3. Si la cancion termino:
     *    - Si sono mas de 0,5 s y la playlist esta activa -> pide la siguiente
     *      y sale.
     *    - Si es una cancion suelta -> rebobina al inicio (mpg123_seek(mh, 0))
     *      y sigue: asi se repite sin un hueco de silencio.
     *    - Si no alcanzo a sonar (archivo roto) -> se detiene.
     * 4. Cualquier otro error de decodificacion corta la cancion.
     * 5. Mezcla a mono y escribe en ALSA con snd_pcm_writei. Si ALSA se quedo
     *    sin datos ("underrun"), snd_pcm_recover lo arregla.
     * 6. Suma a la posicion: cuadros escritos / frecuencia.
     *
     * Al terminar la cancion: si el usuario pidio otra, descarta lo que
     * quedaba en la tarjeta (drop); si no, deja que termine de sonar (drain).
     */

    /* El argumento del hilo no se usa. */
    (void)arg;
    /* Decodificador y salida de audio; se reutilizan de una pista a otra. */
    mpg123_handle *mh  = NULL;
    snd_pcm_t     *pcm = NULL;

    /* Una vuelta por cada pista. */
    while (1) {
        pthread_mutex_lock(&g.lock);
        /* Duerme hasta que haya un comando. */
        while (g.cmd == PCMD_NONE)
            pthread_cond_wait(&g.cond, &g.lock);

        /* Copia el comando y sus datos, y lo marca como atendido. */
        PlayCmd cmd = g.cmd;
        char    fp[512];
        int     tid = g.cmd_track_id;
        strncpy(fp, g.cmd_filepath, sizeof(fp) - 1);
        fp[511] = '\0';
        g.cmd = PCMD_NONE;
        g.seek_req = -1.0f;   /* un salto pedido para la pista anterior no aplica */
        pthread_mutex_unlock(&g.lock);

        /* QUIT termina el hilo. Cualquier otro que no sea PLAY se ignora
           aqui,
           porque no hay pista sonando. */
        if (cmd == PCMD_QUIT) break;
        if (cmd != PCMD_PLAY) continue; 

        //Abrir archivo MP3
        /* El decodificador se crea una sola vez. */
        if (!mh) mh = mpg123_new(NULL, NULL);
        mpg123_param(mh, MPG123_QUIET, 1, 0);  /* suprimir warnings de ID3 */
        if (!mh) { fprintf(stderr, "[audio] mpg123_new failed\n"); continue; }

        /* Abre el archivo de la pista. */
        if (mpg123_open(mh, fp) != MPG123_OK) {
            fprintf(stderr, "[audio] No se pudo abrir '%s': %s\n",
                    fp, mpg123_strerror(mh));
            continue;
        }

        // Forzar formato S16_LE para ALSA
        /* Formato de la pista; la salida se fuerza a 16 bits con signo. */
        long rate = 44100; int channels = 2, encoding = MPG123_ENC_SIGNED_16;
        mpg123_getformat(mh, &rate, &channels, &encoding);
        mpg123_format_none(mh);
        mpg123_format(mh, rate, channels, MPG123_ENC_SIGNED_16);

        // Abrir ALSA
        /* Si quedaba una salida abierta de la pista anterior, se cierra. */
        if (pcm) { snd_pcm_close(pcm); pcm = NULL; }
        pcm = alsa_open(rate, channels);
        if (!pcm) {
            /* Sin salida de audio la pista no puede sonar: el estado queda
               detenido. */
            mpg123_close(mh);
            pthread_mutex_lock(&g.lock);
            g.status     = LIB_AUDIO_STOPPED;
            g.current_id = -1;
            pthread_mutex_unlock(&g.lock);
            continue;
        }

        /* Publica que la pista esta sonando desde el segundo cero. */
        pthread_mutex_lock(&g.lock);
        g.status        = LIB_AUDIO_PLAYING;
        g.current_id    = tid;
        g.position_secs = 0.0f;
        pthread_mutex_unlock(&g.lock);
        printf("[audio] Reproduciendo id=%d '%s'\n", tid, fp);

        // Establecer bucle de decodificacion
        /* Bloque decodificado, bytes obtenidos y codigo del decodificador. */
        unsigned char buf[8192];
        size_t        done;
        int           dec_err;
        /* 1 cuando hay que dejar esta pista. */
        int           stop_requested = 0;
        int           siguiente = 0;      /* fin natural, sigue la playlist */

        /* Una vuelta por bloque. */
        while (!stop_requested) {

            // Verificar comando pendiente
            pthread_mutex_lock(&g.lock);

            // SALTO desde la barra de progreso del panel
            aplicar_salto(mh, pcm, rate);

            // PAUSA: suspender decodificación hasta RESUME / STOP
            if (g.cmd == PCMD_PAUSE) {
                /* Atiende la pausa y la publica. */
                g.cmd    = PCMD_NONE;
                g.status = LIB_AUDIO_PAUSED;
                // Vaciar el buffer para audio residual
                snd_pcm_drop(pcm);   
                printf("[audio] Pausado en %.1fs\n", g.position_secs);

                /* En pausa tambien se puede saltar: se mueve el decodificador
                   y se sigue en pausa, en la posicion nueva. */
                while (g.cmd != PCMD_RESUME && g.cmd != PCMD_STOP &&
                       g.cmd != PCMD_PLAY  && g.cmd != PCMD_QUIT) {
                    /* Con un salto pendiente, se aplica; si no, se duerme
                       hasta el proximo aviso. */
                    if (g.seek_req >= 0.0f) aplicar_salto(mh, pcm, rate);
                    else                    pthread_cond_wait(&g.cond, &g.lock);
                }

                if (g.cmd == PCMD_RESUME) {
                    /* Vuelve a reproducir. */
                    g.cmd    = PCMD_NONE;
                    g.status = LIB_AUDIO_PLAYING;
                    // Rearmar el ALSA
                    snd_pcm_prepare(pcm);   
                    printf("[audio] Resumido\n");
                }
            }

            /* STOP / nueva cancion / salir */
            if (g.cmd == PCMD_STOP || g.cmd == PCMD_PLAY || g.cmd == PCMD_QUIT) {
                /* Se sale del ciclo de bloques. */
                stop_requested = 1;
                /* Solo STOP limpia el estado; PLAY y QUIT los atiende la
                   vuelta exterior. */
                if (g.cmd == PCMD_STOP) {
                    g.cmd           = PCMD_NONE;
                    g.status        = LIB_AUDIO_STOPPED;
                    g.current_id    = -1;
                    g.position_secs = 0.0f;
                }
                pthread_mutex_unlock(&g.lock);
                break;
            }

            pthread_mutex_unlock(&g.lock);

            // Decodificar chunk
            dec_err = mpg123_read(mh, buf, sizeof(buf), &done);

            /* Fin de pista: la misma pista vuelve a empezar (bucle) hasta que
               llegue STOP o se elija otra. Se rebobina el decodificador en vez
               de reabrir el archivo y ALSA, asi la vuelta no deja hueco. Si la
               pista no alcanzo a sonar (archivo vacio o roto) se detiene, para
               no quedar girando sin reproducir nada. */
            if (dec_err == MPG123_DONE || done == 0) {
                pthread_mutex_lock(&g.lock);
                /* La pista "sono" si llego a reproducir mas de medio
                   segundo. */
                int sono = g.position_secs > 0.5f;
                /* Reproduciendo la playlist: se pasa a la siguiente pista,
                   salvo que el usuario ya haya pedido otra cosa. */
                if (sono && g.pl_activa && g.playlist_n > 0 && g.cmd == PCMD_NONE &&
                    pedir_siguiente_de_playlist()) {
                    /* Hay otra pista pedida: se sale para que la vuelta
                       exterior la abra. */
                    siguiente = 1;
                    pthread_mutex_unlock(&g.lock);
                    break;
                }
                pthread_mutex_unlock(&g.lock);

                /* Pista suelta: se rebobina y sigue desde el principio. */
                if (sono && mpg123_seek(mh, 0, SEEK_SET) >= 0) {
                    pthread_mutex_lock(&g.lock);
                    g.position_secs = 0.0f;
                    pthread_mutex_unlock(&g.lock);
                    continue;
                }

                /* No alcanzo a sonar, o no se pudo rebobinar: se detiene. */
                printf("[audio] Fin de pista id=%d\n", tid);
                pthread_mutex_lock(&g.lock);
                g.status        = LIB_AUDIO_STOPPED;
                g.current_id    = -1;
                g.position_secs = 0.0f;
                pthread_mutex_unlock(&g.lock);
                break;
            }

            /* Cualquier otro error del decodificador corta la pista. */
            if (dec_err != MPG123_OK && dec_err != MPG123_NEW_FORMAT) {
                fprintf(stderr, "[audio] Error de decodificación: %s\n",
                        mpg123_strerror(mh));
                break;
            }

            // Escribir chunk a ALSA
            mezclar_a_mono(buf, done, channels);
            /* Cuadros que hay en el bloque, puntero de avance y cuadros ya
               escritos. */
            snd_pcm_uframes_t frames   = snd_pcm_bytes_to_frames(pcm, (ssize_t)done);
            unsigned char    *ptr      = buf;
            snd_pcm_uframes_t written  = 0;

            /* Escribe hasta agotar el bloque. */
            while (frames > 0) {
                /* Entrega cuadros a ALSA; devuelve cuantos acepto, o un
                   error. */
                snd_pcm_sframes_t wr = snd_pcm_writei(pcm, ptr, frames);
                if (wr < 0) {
                    /* Intenta recuperarse, por ejemplo de un buffer que se
                       vacio. */
                    wr = snd_pcm_recover(pcm, (int)wr, 0);
                    if (wr < 0) {
                        fprintf(stderr, "[audio] Error ALSA: %s\n", snd_strerror((int)wr));
                        break;
                    }
                    continue;
                }
                /* Avanza el puntero y descuenta lo escrito. */
                ptr     += snd_pcm_frames_to_bytes(pcm, (snd_pcm_uframes_t)wr);
                frames  -= (snd_pcm_uframes_t)wr;
                written += (snd_pcm_uframes_t)wr;
            }

            // Actualizar posición una vez por chunk
            if (written > 0) {
                pthread_mutex_lock(&g.lock);
                /* Segundos = cuadros escritos / frecuencia de muestreo. */
                g.position_secs += (float)written / (float)rate;
                pthread_mutex_unlock(&g.lock);
            }
        }

        // Terminar la decodificacion
        pthread_mutex_lock(&g.lock);
        /* Si hay un PLAY pendiente, se esta cambiando de pista. */
        int switching = (g.cmd == PCMD_PLAY);
        pthread_mutex_unlock(&g.lock);

        /* Al cambiar de pista por orden del usuario se descarta lo que
           quedaba en
           el buffer; en cualquier otro caso se deja sonar hasta el final. */
        if (switching && !siguiente)
            snd_pcm_drop(pcm);  
        else
            snd_pcm_drain(pcm); 
        /* Cierra el archivo; el decodificador queda para la proxima pista. */
        mpg123_close(mh);
    }

    /* Al terminar el hilo se liberan la salida y el decodificador. */
    if (pcm) { snd_pcm_close(pcm); pcm = NULL; }
    if (mh)  { mpg123_delete(mh);  mh  = NULL; }
    return NULL;
}

/* ═══════════════════════════════════════════════════════════
   API - Inicializacion con parametros iniciales
═══════════════════════════════════════════════════════════ */

/* Inicializa el modulo: estado, hilo de reproduccion, volumen y escaneo.
   audio_dir: carpeta de los MP3, o NULL para la carpeta por defecto.
   Devuelve 0 si quedo listo y -1 si no. */
int lib_audio_init(const char *audio_dir)
{
    /*
     * Arranca el modulo.
     *
     * 1. Pone toda la estructura g en 0 y copia la carpeta (./audio si se pasa
     *    NULL).
     * 2. Valores iniciales: volumen 85 %, detenido, sin cancion, sin salto.
     * 3. mpg123_init() y crea el candado y la variable de condicion.
     * 4. Marca initialized = 1 antes de lanzar el hilo y de escanear, porque
     *    esas funciones lo revisan.
     * 5. Lanza el hilo reproductor, aplica el volumen y busca las canciones.
     */

    /* Llamarla dos veces no reinicializa. */
    if (g.initialized) return 0;

    /* Todo el estado en cero y la carpeta de audio copiada. */
    memset(&g, 0, sizeof(g));
    strncpy(g.audio_dir,
            audio_dir ? audio_dir : LIB_AUDIO_DIR_DEFAULT,
            sizeof(g.audio_dir) - 1);

    /* Valores iniciales: volumen al 85 %, detenido, sin pista y sin salto. */
    g.volume     = 85;
    g.status     = LIB_AUDIO_STOPPED;
    g.current_id = -1;
    g.seek_req   = -1.0f;
    // g.notify_pid = -1;

    /* Inicializa la biblioteca mpg123. */
    mpg123_init();

    /* Crea el mutex y la variable de condicion. */
    if (pthread_mutex_init(&g.lock, NULL) != 0) return -1;
    if (pthread_cond_init (&g.cond, NULL) != 0) return -1;

    /* Se marca antes de crear el hilo y de escanear: lib_audio_scan() lo
       exige. */
    g.initialized = 1;

    /* Lanza el hilo de reproduccion. */
    if (pthread_create(&g.thread, NULL, playback_thread, NULL) != 0) {
        fprintf(stderr, "[audio] No se pudo crear hilo de reproducción\n");
        return -1;
    }

    /* Aplica el volumen inicial y busca las pistas. */
    alsa_set_volume(g.volume);
    lib_audio_scan();

    printf("[audio] Inicializado — dir='%s'  pistas=%d  volumen=%d%%\n",
           g.audio_dir, g.track_count, g.volume);
    return 0;
}

/* Termina el hilo y libera los recursos del modulo. */
void lib_audio_destroy(void)
{
    /*
     * Deja la orden QUIT, despierta al hilo, lo espera con pthread_join,
     * destruye el candado y la condicion, y cierra mpg123.
     */

    if (!g.initialized) return;

    /* Le pide al hilo que termine y lo despierta. */
    pthread_mutex_lock(&g.lock);
    g.cmd = PCMD_QUIT;
    pthread_cond_signal(&g.cond);
    pthread_mutex_unlock(&g.lock);

    /* Espera a que salga y destruye el mutex y la condicion. */
    pthread_join(g.thread, NULL);
    pthread_mutex_destroy(&g.lock);
    pthread_cond_destroy(&g.cond);

    //if (g.notify_pid > 0) {
    //    kill(g.notify_pid, SIGTERM);
    //    waitpid(g.notify_pid, NULL, WNOHANG);
    // }

    /* Cierra mpg123 y marca el modulo como no inicializado. */
    mpg123_exit();
    g.initialized = 0;
    printf("[audio] destruido\n");
}

/* ═══════════════════════════════════════════════════════════
   API — Escaneo de audios / tracks
═══════════════════════════════════════════════════════════ */

/* Agrega a la lista los .mp3 de 'dir' (excepto notify_*). Con 'subdirs' entra
   un nivel en las subcarpetas: en el repositorio la playlist vive en
   audio/canciones/ y audio/music/, en el target la receta lo aplana todo en
   /opt/robot/audio/. Devuelve el nuevo total de pistas. */
static int scan_dir(const char *dir, int subdirs, LibAudioTrack *out, int count)
{
    /*
     * Agrega a la lista los .mp3 de una carpeta.
     *
     * Para cada entrada (readdir):
     *
     * - Salta las que empiezan con punto (ocultas, . y ..).
     * - Si es carpeta y subdirs es 1, se llama a si misma con subdirs = 0
     *   (entra un solo nivel). Esto se llama recursion.
     * - Solo acepta nombres que terminen en .mp3 (sin importar mayusculas:
     *   strcasecmp).
     * - Ignora los que empiezan con notify_ (son los avisos, no musica).
     * - Llena nombre, ruta completa y duracion.
     *
     * Devuelve el nuevo total.
     */

    /* Abre la carpeta; si no existe, la lista queda como estaba. */
    DIR *d = opendir(dir);
    if (!d) return count;

    /* Recorre las entradas mientras quepan pistas. */
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && count < LIB_AUDIO_TRACKS_MAX) {
        /* Nombre de la entrada, su largo y su ruta completa. */
        const char *name = entry->d_name;
        size_t      nlen = strlen(name);
        char        path[640];

        /* Se saltan los archivos ocultos y las entradas . y .. */
        if (name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", dir, name);

        /* Una subcarpeta se escanea sin bajar un nivel mas. */
        if (subdirs && entry->d_type == DT_DIR) {
            count = scan_dir(path, 0, out, count);
            continue;
        }

        // Solo archivos .mp3
        if (nlen < 5 || strcasecmp(name + nlen - 4, ".mp3") != 0) continue;
        // No tomar en cuenta los archivos que son para notificaciones
        if (strncmp(name, "notify_", 7) == 0) continue;

        /* Llena la pista: nombre, ruta y duracion. */
        LibAudioTrack *t = &out[count];
        strncpy(t->filename, name,         LIB_AUDIO_NAME_MAX - 1);
        t->filename[LIB_AUDIO_NAME_MAX - 1] = '\0';
        strncpy(t->filepath, path, sizeof(t->filepath) - 1);
        t->filepath[sizeof(t->filepath) - 1] = '\0';
        t->duration_secs = mp3_duration(t->filepath);

        /* Una pista mas. */
        count++;
    }
    closedir(d);
    return count;
}

/* Compara dos pistas por nombre de archivo, para qsort. */
static int cmp_track(const void *a, const void *b)
{
    /*
     * Compara dos canciones por nombre. La usa qsort para ordenar la lista.
     *
     * Para entender: qsort es la funcion de ordenamiento de C. Recibe otra
     * funcion que le dice como comparar dos elementos: debe devolver negativo
     * si el primero va antes, 0 si son iguales y positivo si va despues.
     * strcmp devuelve justamente eso para dos textos. Los const void * son
     * punteros "a cualquier cosa" que se convierten al tipo real con (const
     * LibAudioTrack *)a.
     */

    return strcmp(((const LibAudioTrack *)a)->filename,
                  ((const LibAudioTrack *)b)->filename);
}

/* Busca los MP3 de la carpeta de audio, los ordena, les asigna id y recarga
   la playlist. Devuelve cuantas pistas encontro, o -1. */
int lib_audio_scan(void)
{
    /*
     * Busca todas las canciones.
     *
     * 1. Revisa que la carpeta exista.
     * 2. scan_dir con subcarpetas, sobre un arreglo temporal static (64
     *    canciones con sus rutas ocupan ~50 KB; static lo saca de la pila).
     * 3. Ordena por nombre con qsort y numera desde 1. Como los archivos
     *    tienen prefijos 01_, 02_, el orden es el esperado.
     * 4. Con el candado, publica la lista y recarga la playlist.
     */

    if (!g.initialized) return -1;

    /* Lista temporal. Es static para no ocupar la pila: son 64 pistas con
       sus
       rutas. */
    static LibAudioTrack found[LIB_AUDIO_TRACKS_MAX];

    /* Comprueba que la carpeta exista antes de escanear. */
    DIR *dir = opendir(g.audio_dir);
    if (!dir) {
        fprintf(stderr, "[audio] No se pudo abrir '%s'\n", g.audio_dir);
        return -1;
    }
    closedir(dir);

    /* Escanea la carpeta y sus subcarpetas inmediatas. */
    int count = scan_dir(g.audio_dir, 1, found, 0);

    /* readdir no garantiza ningun orden; ordenar por nombre deja la playlist
       en el orden de los prefijos numericos (01_..., 02_...). Los ids se
       asignan despues de ordenar, asi son estables entre escaneos. */
    qsort(found, (size_t)count, sizeof(LibAudioTrack), cmp_track);
    for (int i = 0; i < count; i++) found[i].id = i + 1;

    /* Publica la lista nueva y recarga la playlist, bajo el candado. */
    pthread_mutex_lock(&g.lock);
    memcpy(g.tracks, found, (size_t)count * sizeof(LibAudioTrack));
    g.track_count = count;
    cargar_playlist();
    pthread_mutex_unlock(&g.lock);

    printf("[audio] Escaneo: %d pista(s) en '%s', playlist de %d\n",
           count, g.audio_dir, g.playlist_n);
    return count;
}

/* Copia la lista de pistas. Devuelve cuantas copio. */
int lib_audio_get_tracks(LibAudioTrack *out, int max)
{
    /*
     * Copia la lista de canciones al arreglo del que llama y devuelve cuantas
     * copio.
     */

    if (!g.initialized || !out || max <= 0) return 0;
    pthread_mutex_lock(&g.lock);
    /* No mas de las que caben. */
    int n = (g.track_count < max) ? g.track_count : max;
    memcpy(out, g.tracks, (size_t)n * sizeof(LibAudioTrack));
    pthread_mutex_unlock(&g.lock);
    return n;
}

/* ═══════════════════════════════════════════════════════════
   API - Control de reproduccion
═══════════════════════════════════════════════════════════ */

/* Pide reproducir una pista suelta, que se repite en bucle. Devuelve 0 si
   la pista existe y -1 si no. */
int lib_audio_play(int track_id)
{
    /*
     * Pide tocar una cancion suelta (que se repetira).
     *
     * 1. Busca la ruta del archivo con ese id; si no existe, devuelve -1.
     * 2. Llena el buzon: archivo, id, cmd = PCMD_PLAY, y desactiva la
     *    playlist.
     * 3. pthread_cond_signal despierta al hilo.
     */

    if (!g.initialized) return -1;

    pthread_mutex_lock(&g.lock);

    /* Busca la ruta del archivo de esa pista. */
    const char *fp = NULL;
    for (int i = 0; i < g.track_count; i++) {
        if (g.tracks[i].id == track_id) { fp = g.tracks[i].filepath; break; }
    }
    if (!fp) {
        pthread_mutex_unlock(&g.lock);
        fprintf(stderr, "[audio] Audio %d no encontrado\n", track_id);
        return -1;
    }

    /* Arma el comando PLAY y despierta al hilo. */
    strncpy(g.cmd_filepath, fp, sizeof(g.cmd_filepath) - 1);
    g.cmd_filepath[sizeof(g.cmd_filepath) - 1] = '\0';
    g.cmd_track_id = track_id;
    g.cmd          = PCMD_PLAY;
    g.pl_activa    = 0;          /* una pista suelta se repite en bucle */
    pthread_cond_signal(&g.cond);
    pthread_mutex_unlock(&g.lock);
    return 0;
}

/* Pide pausar la pista que suena. */
void lib_audio_pause(void)
{
    /*
     * Deja PAUSE en el buzon, pero solo si esta sonando y no hay otra orden
     * pendiente. Asi un aviso que llega justo despues de un STOP no lo pisa
     * (si no, la musica volveria a sonar al terminar el aviso).
     */

    if (!g.initialized) return;
    pthread_mutex_lock(&g.lock);
    /* Solo si no hay otro comando pendiente: una notificacion que llega justo
       despues de un STOP (p. ej. el sonido del cambio a manual) no debe
       pisarlo, o la musica se reanudaria al terminar la notificacion. */
    if (g.status == LIB_AUDIO_PLAYING && g.cmd == PCMD_NONE) {
        g.cmd = PCMD_PAUSE;
        pthread_cond_signal(&g.cond);
    }
    pthread_mutex_unlock(&g.lock);
}

/* Pide reanudar una pista en pausa. */
void lib_audio_resume(void)
{
    /* Deja RESUME, solo si esta en pausa y el buzon esta vacio. */

    if (!g.initialized) return;
    pthread_mutex_lock(&g.lock);
    /* Solo si esta en pausa y no hay otro comando pendiente. */
    if (g.status == LIB_AUDIO_PAUSED && g.cmd == PCMD_NONE) {
        g.cmd = PCMD_RESUME;
        pthread_cond_signal(&g.cond);
    }
    pthread_mutex_unlock(&g.lock);
}

/* Pide detener la reproduccion. */
void lib_audio_stop(void)
{
    /* Desactiva la playlist y, si algo suena o esta en pausa, deja STOP. */

    if (!g.initialized) return;
    pthread_mutex_lock(&g.lock);
    /* Detener tambien termina la playlist. */
    g.pl_activa = 0;
    /* Si ya estaba detenido no hace falta el comando. */
    if (g.status != LIB_AUDIO_STOPPED) {
        g.cmd = PCMD_STOP;
        pthread_cond_signal(&g.cond);
    }
    pthread_mutex_unlock(&g.lock);
}

/* Pide saltar a un segundo de la pista actual. Ver lib_audio.h. */
int lib_audio_seek(float segundos)
{
    /*
     * Pide saltar a un segundo de la cancion actual.
     *
     * 1. Si no hay cancion cargada, devuelve -1.
     * 2. Recorta el destino entre 0 y la duracion - 0,5 s (saltar al final
     *    exacto terminaria la cancion en el acto).
     * 3. Lo deja en seek_req y despierta al hilo.
     */

    if (!g.initialized) return -1;
    pthread_mutex_lock(&g.lock);
    /* Sin pista cargada no hay donde saltar. */
    if (g.status == LIB_AUDIO_STOPPED || g.current_id < 0) {
        pthread_mutex_unlock(&g.lock);
        return -1;
    }
    /* Medio segundo antes del final como tope: saltar al final exacto
       terminaria la pista en el acto. */
    const LibAudioTrack *t = pista_por_id(g.current_id);
    float tope = (t && t->duration_secs > 1) ? (float)t->duration_secs - 0.5f : segundos;
    /* Recorta el destino al rango valido. */
    if (segundos > tope) segundos = tope;
    if (segundos < 0.0f) segundos = 0.0f;
    /* Deja el salto pedido y despierta al hilo. */
    g.seek_req = segundos;
    pthread_cond_signal(&g.cond);
    pthread_mutex_unlock(&g.lock);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
   API - Volumen y estado
═══════════════════════════════════════════════════════════ */

/* Fija el volumen de la musica, de 0 a 100. */
void lib_audio_set_volume(int vol)
{
    /* Recorta a 0-100, lo guarda y lo aplica con alsa_set_volume. */

    if (vol < 0) vol = 0; if (vol > 100) vol = 100;
    /* Guarda el valor y lo aplica al mezclador. */
    pthread_mutex_lock(&g.lock);
    g.volume = vol;
    pthread_mutex_unlock(&g.lock);
    alsa_set_volume(vol);
}

/* Devuelve el volumen guardado. */
int lib_audio_get_volume(void)
{
    /* Devuelve el volumen guardado. */

    pthread_mutex_lock(&g.lock);
    int v = g.volume;
    pthread_mutex_unlock(&g.lock);
    return v;
}

/* Devuelve el estado del reproductor. */
LibAudioStatus lib_audio_get_status(void)
{
    /* Devuelve si esta detenido (0), sonando (1) o en pausa (2). */

    pthread_mutex_lock(&g.lock);
    LibAudioStatus s = g.status;
    pthread_mutex_unlock(&g.lock);
    return s;
}

/* Devuelve el id de la pista en curso, o -1. */
int lib_audio_get_current_id(void)
{
    /* Devuelve el id de la cancion actual, o -1. */

    pthread_mutex_lock(&g.lock);
    int id = g.current_id;
    pthread_mutex_unlock(&g.lock);
    return id;
}

/* Devuelve los segundos reproducidos de la pista en curso. */
float lib_audio_get_position(void)
{
    /* Devuelve los segundos reproducidos de la cancion actual. */

    pthread_mutex_lock(&g.lock);
    float p = g.position_secs;
    pthread_mutex_unlock(&g.lock);
    return p;
}

/* ═══════════════════════════════════════════════════════════
   API - Notificaciones
═══════════════════════════════════════════════════════════ */

/* Reproduce el sonido de un evento. Bloquea hasta que termina: decodifica
   y escribe en ALSA en el hilo que la llama. Si habia musica, la pausa
   antes y la reanuda despues. */
void lib_audio_notify(NotificationEvent event)
{
    /*
     * Toca el sonido de un evento (inicio, autonomo, obstaculo, manual, fin de
     * ciclo). Bloquea: no devuelve hasta que el sonido termina.
     *
     * 1. Una tabla static const char *NAMES[] da el archivo de cada evento; el
     *    evento sirve de indice.
     * 2. Si el archivo no existe (access(...) != 0), avisa y sale.
     * 3. Si habia musica sonando, la pausa.
     * 4. Guarda el volumen y lo sube a 92 %.
     * 5. Abre su propio decodificador y su propia salida ALSA, y decodifica y
     *    escribe bloque a bloque en el hilo que la llamo (no usa el hilo
     *    reproductor).
     * 6. snd_pcm_drain espera a que suene lo ultimo; cierra todo.
     * 7. Restaura el volumen y, si habia pausado la musica, la reanuda.
     *
     * Consecuencia: cuando el hilo de navegacion detecta un obstaculo y llama
     * a esta funcion, se queda esperando mientras suena el aviso.
     *
     * Para entender: (unsigned int)event >= NOTIFY_COUNT revisa que el evento
     * sea valido. Convertir a sin signo hace que un numero negativo se vuelva
     * enorme y tambien quede fuera.
     */

    /* Archivo de cada evento, en el orden de NotificationEvent. */
    static const char *NAMES[] = {
        "notify_startup.mp3",
        "notify_autonomous.mp3",
        "notify_obstacle.mp3",
        "notify_manual.mp3",
        "notify_cycle_end.mp3",
    };

    /* Un evento fuera de rango se ignora. */
    if ((unsigned int)event >= NOTIFY_COUNT) return;

    /* Ruta completa del archivo del evento. */
    char filepath[640];
    snprintf(filepath, sizeof(filepath), "%s/%s", g.audio_dir, NAMES[event]);

    /* Si el archivo no esta, se avisa y no suena nada. */
    if (access(filepath, F_OK) != 0) {
        printf("[audio] Notificación '%s' no encontrada\n", NAMES[event]);
        return;
    }

    printf("[audio] Notificación: %s\n", NAMES[event]);

    /* Si hay musica sonando, se pausa mientras dura el aviso. */
    int was_playing = (g.status == LIB_AUDIO_PLAYING);
    if (was_playing) lib_audio_pause();

    // Guardar volumen actual y forzar al maximo para notificaciones
    pthread_mutex_lock(&g.lock);
    int vol_original = g.volume;
    pthread_mutex_unlock(&g.lock);

    alsa_set_volume(92);  // ← forzar volumen

    /* Decodificador propio para el aviso. */
    int err_code;
    mpg123_handle *mh = mpg123_new(NULL, &err_code);
    /* Ante cualquier fallo se restaura el volumen y se reanuda la musica. */
    if (!mh) {
        alsa_set_volume(vol_original);
        if (was_playing) lib_audio_resume();
        return;
    }

    mpg123_param(mh, MPG123_QUIET, 1, 0);

    /* Abre el archivo del aviso. */
    if (mpg123_open(mh, filepath) != MPG123_OK) {
        fprintf(stderr, "[audio] No se pudo abrir notificación '%s'\n", filepath);
        mpg123_delete(mh);
        alsa_set_volume(vol_original);
        if (was_playing) lib_audio_resume();
        return;
    }

    /* Formato del aviso; la salida se fuerza a 16 bits con signo. */
    long rate = 44100;
    int channels = 2, encoding = MPG123_ENC_SIGNED_16;
    mpg123_getformat(mh, &rate, &channels, &encoding);
    mpg123_format_none(mh);
    mpg123_format(mh, rate, channels, MPG123_ENC_SIGNED_16);

    /* Abre una salida de audio propia para el aviso. */
    snd_pcm_t *pcm = alsa_open(rate, channels);
    if (!pcm) {
        mpg123_close(mh);
        mpg123_delete(mh);
        alsa_set_volume(vol_original);
        if (was_playing) lib_audio_resume();
        return;
    }

    /* Bloque decodificado, bytes obtenidos y codigo del decodificador. */
    unsigned char buf[8192];
    size_t done;
    int dec_err;

    /* Decodifica y escribe bloque a bloque hasta el final del archivo. */
    while (1) {
        dec_err = mpg123_read(mh, buf, sizeof(buf), &done);
        /* Fin del archivo o error: se termina. */
        if (dec_err == MPG123_DONE || done == 0) break;
        if (dec_err != MPG123_OK && dec_err != MPG123_NEW_FORMAT) break;

        /* La misma mezcla a mono y la misma escritura que usa la musica. */
        mezclar_a_mono(buf, done, channels);
        snd_pcm_uframes_t frames = snd_pcm_bytes_to_frames(pcm, (ssize_t)done);
        unsigned char *ptr = buf;

        while (frames > 0) {
            snd_pcm_sframes_t wr = snd_pcm_writei(pcm, ptr, frames);
            if (wr < 0) {
                wr = snd_pcm_recover(pcm, (int)wr, 0);
                if (wr < 0) break;
                continue;
            }
            ptr    += snd_pcm_frames_to_bytes(pcm, (snd_pcm_uframes_t)wr);
            frames -= (snd_pcm_uframes_t)wr;
        }
    }

    /* Espera a que suene lo que quedo en el buffer y libera todo. */
    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
    mpg123_close(mh);
    mpg123_delete(mh);

    // Restaurar volumen original
    alsa_set_volume(vol_original);  // ← restaurar

    /* Si se habia pausado la musica, vuelve a sonar. */
    if (was_playing) lib_audio_resume();
}