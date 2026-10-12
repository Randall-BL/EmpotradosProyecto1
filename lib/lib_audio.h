/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 */

/* Guarda de inclusion: el contenido se procesa una sola vez por archivo. */
#ifndef LIB_AUDIO_H
#define LIB_AUDIO_H
// sudo apt install libmpg123-dev libasound2-dev mpg123
/* Enteros de ancho fijo. */
#include <stdint.h>

// Configuracion
#define LIB_AUDIO_DIR_DEFAULT   "./audio"   // directorio para guardar los tracks / audios  

/* Playlist persistente, relativa a audio_dir. En la Raspberry cae en la
   particion de la musica (ext4 de lectura y escritura), asi sobrevive a los
   reinicios y no toca la particion raiz. Un nombre de archivo por linea. */
#define LIB_AUDIO_PLAYLIST_FILE "canciones/playlist.txt"

/* Salida PWM analogica de la RPi4 (card 1, "Headphones"). El overlay audremap
   la saca por GPIO 18 hacia el filtro RC y el amplificador PAM8403, en vez de
   por el jack de 3.5 mm. Ver docs/hardware-sensores.md. */
#define LIB_AUDIO_ALSA_DEVICE   "hw:1,0"
/* Tarjeta cuyo mezclador controla el volumen, y nombre de ese control. */
#define LIB_AUDIO_MIXER_CARD    "hw:1"            
#define LIB_AUDIO_MIXER_CTRL    "PCM"    // "PCM" Rasp, "Master" PC

/* El robot tiene un solo parlante, colgado de un solo canal del PAM8403.
   Con 1 cada pista estereo se mezcla a mono (L+R)/2 antes de salir, y los dos
   canales llevan la misma senal: no se pierde lo que viene solo por la
   derecha y da igual a cual de los dos GPIO quedo cableado el amplificador. */
#define LIB_AUDIO_MONO          1
/* Maximo de pistas en la lista y largo maximo de un nombre de archivo. */
#define LIB_AUDIO_TRACKS_MAX     64
#define LIB_AUDIO_NAME_MAX       128

/* Tipos */
/* Estado del reproductor. */
typedef enum {
    LIB_AUDIO_STOPPED = 0,
    LIB_AUDIO_PLAYING = 1,
    LIB_AUDIO_PAUSED  = 2,
} LibAudioStatus;

/* Eventos que tienen sonido propio. El valor es el indice del archivo en
   la tabla de lib_audio_notify(). */
typedef enum {
    /* Arranque del sistema. */
    NOTIFY_STARTUP    = 0,  
    /* Entrada al modo autonomo. */
    NOTIFY_AUTONOMOUS = 1,  
    /* Obstaculo o desnivel detectado. */
    NOTIFY_OBSTACLE   = 2, 
    /* Cambio a modo manual. */
    NOTIFY_MANUAL     = 3, 
    NOTIFY_CYCLE_END  = 4,   /* fin del ciclo de limpieza */
    /* Cantidad de eventos; no es un evento. */
    NOTIFY_COUNT
} NotificationEvent;

// Parametros de cada Audio
typedef struct {
    /* Identificador de la pista: su posicion, desde 1, en la lista ordenada
       por nombre. */
    int  id;
    /* Nombre del archivo, sin la carpeta. */
    char filename[LIB_AUDIO_NAME_MAX];  
    /* Ruta completa para abrirlo. */
    char filepath[640];                 
    /* Duracion en segundos; 0 si no se pudo medir. */
    int  duration_secs;                 
} LibAudioTrack;

/* ─────────────────────────────────────────────────
   API
───────────────────────────────────────────────── */

/* Inicializa el modulo y lanza el hilo de reproduccion. audio_dir: carpeta
   de los MP3, o NULL para LIB_AUDIO_DIR_DEFAULT. Devuelve 0 o -1. */
int  lib_audio_init   (const char *audio_dir);
/* Termina el hilo y libera los recursos. */
void lib_audio_destroy(void);

/* Vuelve a buscar los MP3 de la carpeta. Devuelve cuantos encontro, o -1. */
int lib_audio_scan(void);

/* Copia la lista de pistas en out. Devuelve cuantas copio. */
int lib_audio_get_tracks(LibAudioTrack *out, int max);

// Reproduccion del audio
/* Reproduce una pista suelta en bucle. Devuelve 0, o -1 si el id no existe. */
int  lib_audio_play   (int track_id);
/* Pausa la pista que suena. */
void lib_audio_pause  (void);
/* Reanuda una pista en pausa. */
void lib_audio_resume (void);
/* Detiene la reproduccion y sale de la playlist. */
void lib_audio_stop   (void);

/**
 * @brief Salta a @p segundos dentro de la pista actual, sonando o en pausa.
 * Se recorta a la duracion de la pista. En pausa queda en pausa, en la
 * posicion nueva.
 * @return 0 si quedo pedido, -1 si no hay pista cargada.
 */
int  lib_audio_seek   (float segundos);

// Volumen: 0-100
void lib_audio_set_volume(int vol);
int  lib_audio_get_volume(void);

// Estado actual
/* Reproduciendo, en pausa o detenido. */
LibAudioStatus lib_audio_get_status    (void);
/* Id de la pista en curso, o -1 si no hay. */
int            lib_audio_get_current_id(void);
/* Segundos reproducidos de la pista en curso. */
float          lib_audio_get_position  (void);  

// Notificaciones
/* Reproduce el sonido de un evento y bloquea hasta que termina. Pausa la
   musica mientras suena y la reanuda despues. */
void lib_audio_notify(NotificationEvent event);

/* ── Playlist persistente ─────────────────────────────────────────────────────
 * Lista ordenada de pistas, editable desde la interfaz web. Se guarda por
 * nombre de archivo en LIB_AUDIO_PLAYLIST_FILE, asi que sobrevive a los
 * reinicios aunque cambien los ids. Sin archivo, la playlist son todas las
 * pistas en el orden del escaneo.
 *
 * lib_audio_play() reproduce una pista suelta en bucle, como siempre;
 * lib_audio_play_playlist() recorre la playlist en orden y vuelve a empezar al
 * llegar al final.
 */

/** Copia los ids de la playlist, en orden. @return cuantos copio. */
int lib_audio_playlist_get(int *ids, int max);

/**
 * @brief Reemplaza la playlist y la guarda en disco.
 * @return 0 si se guardo; -1 si algun id no existe o no se pudo escribir.
 */
int lib_audio_playlist_set(const int *ids, int n);

/** Reproduce la playlist desde la posicion @p pos (0 = la primera). */
int lib_audio_play_playlist(int pos);

/** Posicion de la playlist que suena, o -1 si no se esta reproduciendo la playlist. */
int lib_audio_playlist_pos(void);

#endif
