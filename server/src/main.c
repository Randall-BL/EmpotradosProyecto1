/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *

 */

/*
 * El programa principal del robot. Tiene cuatro partes:
 *
 * 1. Servidor web: el enrutador handle_request y funciones para servir
 *    archivos y respuestas de error.
 * 2. Hilos de apoyo: uptime y watchdog.
 * 3. El cerebro: el mapa, la deteccion de obstaculos, las maniobras de
 *    evasion, el ciclo de limpieza y el hilo de navegacion.
 * 4. main(): enciende todo, espera y apaga en orden.
 *
 * Constantes importantes (estan entre las funciones):
 *
 * - SERVER_PORT = 8080: Puerto web
 * - WWW_ROOT = ./www: Carpeta de las paginas (/opt/robot/www)
 * - MAX_UPLOAD_BYTES = 32 KB: Cuerpo maximo de un POST
 * - WATCHDOG_INTERVAL_SECS = 7: Cada cuanto revisa sesiones
 * - MAP_ALCANCE_CM = 150: Ecos mas lejanos no se marcan en el mapa
 * - TTC_OBSTACULO_S = 1,2: Tiempo de choque que cuenta como obstaculo
 * - CONO_FRONTAL_GRADOS = 60: Se miran los angulos 30 a 150
 * - ROBOT_RADIO_CM / PASILLO_CM = 10: Medio ancho del robot
 * - LECTURA_VIGENTE_S = 2: Edad maxima de una lectura
 * - VEL_CRUCERO, VEL_GIRO, VEL_RETROCESO = 210, 200, 180: Con velocidad fija,
 *   todas son "a tope"
 * - RETROCESO_MS / RETROCESO_CAIDA_MS = 500 / 800: Cuanto retrocede
 * - ESPACIO_LIBRE_CM = 40: Menos que esto en todo -> media vuelta
 * - MARGEN_EMPATE_CM = 25: Angulos asi de parecidos se sortean
 * - GIRO_TOLERANCIA = 5 grados: Precision de los giros
 *
 * Para entender: _Static_assert(condicion, "mensaje") es una revision que hace
 * el compilador: si RADAR_LECTURAS_MAX fuera menor que RADAR_N_ANGULOS, el
 * programa ni compilaria.
 *
 * Variables globales: g_running (1 mientras el servidor corre; volatile),
 * g_daemon (el servidor HTTP), g_mapa_seq (ultima lectura del radar ya puesta
 * en el mapa) y g_caida (ultima lectura de los sensores de desnivel).
 */

/* libmicrohttpd: el servidor HTTP embebido. */
#include <microhttpd.h>
/* printf, fprintf, snprintf y setvbuf. */
#include <stdio.h>
/* calloc, realloc, free, rand, srand y abs. */
#include <stdlib.h>
/* strcmp, strncmp, strrchr, strlen y memcpy. */
#include <string.h>
/* signal, SIGINT y SIGTERM, para el apagado ordenado. */
#include <signal.h>
/* sleep, usleep y close. */
#include <unistd.h>
/* Hilos y mutex. */
#include <pthread.h>
/* fstat, para conocer el tamano de un archivo estatico. */
#include <sys/stat.h>
/* open y O_RDONLY. */
#include <fcntl.h>
/* sin, cos, fabs, fmod, fmin y lround. */
#include <math.h>
/* clock_gettime, para medir el tiempo del ciclo de limpieza. */
#include <time.h>

/* Estado global que comparten el servidor, la navegacion y la API. */
#include "robot_state.h"
/* Audio: inicializacion y avisos de evento. */
#include "lib_audio.h"
/* LEDs indicadores. */
#include "lib_leds.h"
/* Sesiones: control de acceso a las rutas protegidas. */
#include "auth.h"
/* Manejadores de la API. */
#include "api.h"
/* Fachada del hardware: robot_init() y robot_shutdown(). */
#include "lib_robot.h"
/* Motores, para las maniobras de la navegacion. */
#include "lib_motors.h"
/* Odometria: posicion y rumbo del robot. */
#include "lib_odom.h"
/* Radar: lecturas, barrido y tiempo de choque. */
#include "lib_radar.h"
/* Sensores de desnivel. */
#include "lib_caida.h"

/* El estado global guarda una lectura por angulo del radar. */
_Static_assert(RADAR_LECTURAS_MAX >= RADAR_N_ANGULOS,
               "RADAR_LECTURAS_MAX (robot_state.h) no alcanza para el barrido del radar");

// Configuraciones del servidor iniciales
/* Puerto TCP en el que escucha el servidor. */
#define SERVER_PORT      8080
/* Carpeta de la interfaz web, relativa al directorio de trabajo. */
#define WWW_ROOT         "./www"
/* Tamano maximo del cuerpo de un POST: 32 KB. */
#define MAX_UPLOAD_BYTES (32 * 1024)

// Body del POST
typedef struct {
    /* Buffer que crece con cada trozo recibido. */
    char  *data;
    /* Bytes acumulados hasta ahora. */
    size_t used;
} UploadBuf;

// Serving File
/* Devuelve el tipo MIME que corresponde a la extension del archivo. */
static const char *mime_for(const char *path) {
    /*
     * Devuelve el tipo de contenido segun la extension del archivo (.html ->
     * text/html; charset=utf-8, .css, .js, .png, .svg...). strrchr busca el
     * ultimo punto del nombre.
     */

    /* Ultimo punto del nombre: de ahi en adelante esta la extension. */
    const char *d = strrchr(path, '.');
    /* Sin extension, o con una desconocida, se sirve como binario generico. */
    if (!d)                       return "application/octet-stream";
    if (strcmp(d, ".html") == 0)  return "text/html; charset=utf-8";
    if (strcmp(d, ".css")  == 0)  return "text/css";
    if (strcmp(d, ".js")   == 0)  return "application/javascript";
    if (strcmp(d, ".ico")  == 0)  return "image/x-icon";
    if (strcmp(d, ".png")  == 0)  return "image/png";
    if (strcmp(d, ".svg")  == 0)  return "image/svg+xml";
    return "application/octet-stream";
}

/* Sirve un archivo estatico de WWW_ROOT. rel: ruta relativa, con la barra
   inicial. Devuelve MHD_NO si el archivo no existe o no es regular. */
static enum MHD_Result serve_file(struct MHD_Connection *conn,
                                   const char *rel)
{
    /*
     * Envia un archivo de la carpeta www.
     *
     * 1. Arma la ruta: ./www + /login.html.
     * 2. Lo abre con open. Si no existe, devuelve MHD_NO.
     * 3. fstat da su tamano y confirma que es un archivo normal (no una
     *    carpeta).
     * 4. MHD_create_response_from_fd crea una respuesta que lee directo del
     *    archivo (libmicrohttpd lo cierra al terminar).
     * 5. Agrega el tipo y no-cache y la encola con codigo 200.
     */

    /* Ruta completa del archivo. */
    char path[512];
    snprintf(path, sizeof(path), "%s%s", WWW_ROOT, rel);

    /* Abre el archivo. */
    int fd = open(path, O_RDONLY);
    if (fd < 0) return MHD_NO;

    /* Solo se sirven archivos regulares; fstat da ademas el tamano. */
    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) { close(fd); return MHD_NO; }

    /* La respuesta lee directamente del descriptor, que libmicrohttpd cierra
       al terminar. */
    struct MHD_Response *r =
        MHD_create_response_from_fd((uint64_t)st.st_size, fd);
    /* Tipo del contenido, y sin cache para que el navegador vea siempre la
       version actual. */
    MHD_add_response_header(r, "Content-Type",  mime_for(path));
    MHD_add_response_header(r, "Cache-Control", "no-cache");
    /* Encola la respuesta con 200 y suelta la referencia propia. */
    enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, r);
    MHD_destroy_response(r);
    return ret;
}

/* Responde 302 con la cabecera Location, para mandar al navegador a otra
   ruta. */
static enum MHD_Result send_redirect(struct MHD_Connection *conn,
                                      const char *to)
{
    /*
     * Responde 302 con la cabecera Location, que hace que el navegador vaya a
     * otra direccion. Se usa para mandar al login a quien no tiene sesion.
     */

    struct MHD_Response *r =
        MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(r, "Location", to);
    enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_FOUND, r);
    MHD_destroy_response(r);
    return ret;
}

/* Responde 404 con un JSON de error. */
static enum MHD_Result send_404(struct MHD_Connection *conn) {
    /*
     * Responde 404 con {"error":"Not found"}. MHD_RESPMEM_PERSISTENT indica
     * que el texto es fijo y no hace falta copiarlo.
     */

    const char *b = "{\"error\":\"Not found\"}";
    struct MHD_Response *r =
        MHD_create_response_from_buffer(strlen(b), (void *)b,
                                        MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(r, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_NOT_FOUND, r);
    MHD_destroy_response(r);
    return ret;
}

/* Responde 401 con un JSON de error. */
static enum MHD_Result send_401(struct MHD_Connection *conn) {
    /* Responde 401 con {"error":"Unauthorized"}. */

    const char *b = "{\"error\":\"Unauthorized\"}";
    struct MHD_Response *r =
        MHD_create_response_from_buffer(strlen(b), (void *)b,
                                        MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(r, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_UNAUTHORIZED, r);
    MHD_destroy_response(r);
    return ret;
}

/* ══════════════════════════════════════════════════════════
   Main request handler / router
══════════════════════════════════════════════════════════ */
/* Manejador unico de libmicrohttpd. Lo llama varias veces por peticion: una
   al llegar las cabeceras, una por cada trozo del cuerpo y una ultima con
   el cuerpo completo, que es cuando se decide a que ruta corresponde. */
static enum MHD_Result handle_request(
    void *cls,
    struct MHD_Connection *conn,
    const char *url,
    const char *method,
    const char *version,
    const char *upload_data,
    size_t     *upload_data_size,
    void      **con_cls)
{
    /*
     * El enrutador: libmicrohttpd la llama varias veces por cada pedido.
     *
     * Para entender: Esta funcion no la llama el codigo del proyecto: se le
     * pasa a MHD_start_daemon como puntero a funcion (un "callback"), y
     * libmicrohttpd la llama cada vez que pasa algo en una conexion.
     *
     * Primera llamada (*con_cls == NULL): reserva con calloc un acumulador
     * UploadBuf para el cuerpo y lo deja asociado a la conexion en *con_cls.
     * Devuelve MHD_YES ("sigue").
     *
     * Llamadas con datos (*upload_data_size > 0): agranda el buffer con
     * realloc, copia el trozo y pone el terminador '\0'. Si pasa de 32 KB,
     * corta la conexion. Pone *upload_data_size = 0 para avisar que lo
     * consumio.
     *
     * Ultima llamada (cuerpo completo):
     *
     * 1. OPTIONS -> responde 204 con cabeceras CORS.
     * 2. GET / o /index.html -> login.html.
     * 3. POST /api/login -> api_login (la unica ruta de la API sin sesion).
     * 4. Control de acceso: si auth_check_request dice que no hay sesion ->
     *    401 para rutas /api/ o redireccion a / para paginas.
     * 5. GET /dashboard -> dashboard.html.
     * 6. Cada ruta de la API, comparando metodo y URL con strcmp, va a su
     *    funcion de api.c.
     * 7. Si nada coincidio -> 404.
     *
     * Para entender: void con_cls es un puntero a un puntero: libmicrohttpd le
     * da a la funcion un "casillero" por conexion donde guardar lo que quiera
     * entre llamadas.
     *
     * Para entender: calloc(1, tamano) es como malloc pero deja la memoria en
     * 0. realloc(p, nuevo) agranda un bloque ya reservado, copiando lo que
     * tenia.
     */

    /* Parametros que no se usan. */
    (void)cls; (void)version;

    // First call para los acumuladores
    if (*con_cls == NULL) {
        /* Reserva el acumulador del cuerpo y lo deja asociado a la conexion. */
        UploadBuf *buf = calloc(1, sizeof(UploadBuf));
        if (!buf) return MHD_NO;
        *con_cls = buf;
        return MHD_YES;
    }

    /* Acumulador de esta peticion. */
    UploadBuf *ub = *con_cls;

    /* Acumular los POST body */
    if (*upload_data_size > 0) {
        /* Espacio necesario: lo ya recibido, el trozo nuevo y el terminador. */
        size_t need = ub->used + *upload_data_size + 1;
        /* Un cuerpo mayor que el maximo corta la conexion. */
        if (need > MAX_UPLOAD_BYTES) return MHD_NO;
        /* Agranda el buffer, copia el trozo y mantiene el terminador de
           cadena. */
        ub->data = realloc(ub->data, need);
        if (!ub->data) return MHD_NO;
        memcpy(ub->data + ub->used, upload_data, *upload_data_size);
        ub->used             += *upload_data_size;
        ub->data[ub->used]   = '\0';
        /* Le avisa a libmicrohttpd que el trozo ya se consumio. */
        *upload_data_size    = 0;
        return MHD_YES;
    }

    /* Todos los body recibidos */
    /* Metodo de la peticion. */
    int is_get  = strcmp(method, "GET")  == 0;
    int is_post = strcmp(method, "POST") == 0;
    /* Cuerpo completo; cadena vacia si no hubo. */
    const char *body = ub->data ? ub->data : "";

    /* Establecer los CORS de seguridad */
    if (strcmp(method, "OPTIONS") == 0) {
        /* Respuesta 204 sin cuerpo, con las cabeceras que permiten el acceso
           desde
           otro origen. */
        struct MHD_Response *r =
            MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(r, "Access-Control-Allow-Origin",  "*");
        MHD_add_response_header(r, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        MHD_add_response_header(r, "Access-Control-Allow-Headers", "Content-Type");
        enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_NO_CONTENT, r);
        MHD_destroy_response(r);
        return ret;
    }

    /* ── Rutas publicas ── */
    /* La raiz sirve la pagina de login. */
    if (is_get && (strcmp(url, "/") == 0 || strcmp(url, "/index.html") == 0))
        return serve_file(conn, "/login.html");

    /* El login es el unico endpoint de la API que no exige sesion. */
    if (is_post && strcmp(url, "/api/login") == 0)
        return api_login(conn, body, ub->used);

    /* ── Autenticacion ── */
    /* Sin sesion valida, la API responde 401 y las paginas redirigen al
       login. */
    if (!auth_check_request(conn)) {
        if (strncmp(url, "/api/", 5) == 0) return send_401(conn);
        return send_redirect(conn, "/");
    }

    /* ── Rutas de paginas protegidas ── */
    /* El panel de control. */
    if (is_get && strcmp(url, "/dashboard") == 0)
        return serve_file(conn, "/dashboard.html");

    /* ── Rutas de API protegidas ── */
    /* Cada ruta de la API se despacha a su manejador en api.c. */
    if (is_post && strcmp(url, "/api/logout") == 0)
        return api_logout(conn);

    if (is_get  && strcmp(url, "/api/status") == 0)
        return api_status(conn);

    if (is_post && strcmp(url, "/api/mode") == 0)
        return api_set_mode(conn, body, ub->used);

    if (is_post && strcmp(url, "/api/move") == 0)
        return api_move(conn, body, ub->used);

    if (is_get  && strcmp(url, "/api/audio/list") == 0)
        return api_audio_list(conn);

    if (is_post && strcmp(url, "/api/audio/control") == 0)
        return api_audio_control(conn, body, ub->used);

    if (is_post && strcmp(url, "/api/audio/volume") == 0)
        return api_audio_volume(conn, body, ub->used);

    if (is_get  && strcmp(url, "/api/audio/playlist") == 0)
        return api_audio_playlist_get(conn);

    if (is_post && strcmp(url, "/api/audio/playlist") == 0)
        return api_audio_playlist_set(conn, body, ub->used);

    if (is_post && strcmp(url, "/api/ciclo") == 0)
        return api_ciclo(conn, body, ub->used);
    if (is_post && strcmp(url, "/api/umbral") == 0)
        return api_umbral(conn, body, ub->used);

    /* Ninguna ruta coincidio. */
    return send_404(conn);
}

/* La llama libmicrohttpd al terminar cada peticion: libera el acumulador
   del cuerpo. */
static void request_done(void *cls, struct MHD_Connection *conn,
                          void **con_cls,
                          enum MHD_RequestTerminationCode toe)
{
    /*
     * La llama libmicrohttpd cuando termina cada pedido. Libera el cuerpo
     * acumulado y el UploadBuf, y deja el casillero en NULL.
     */

    /* Parametros que no se usan. */
    (void)cls; (void)conn; (void)toe;
    UploadBuf *ub = *con_cls;
    if (ub) { free(ub->data); free(ub); *con_cls = NULL; }
}

/* ══════════════════════════════════════════════════════════
   Hilo de actualizacion
══════════════════════════════════════════════════════════ */
/* 1 mientras el servidor esta en marcha. Lo baja el manejador de senales y
   lo consultan todos los hilos. volatile porque se lee sin candado. */
static volatile int g_running = 1;

/* Hilo que cuenta los segundos que lleva encendido el servidor. */
static void *uptime_thread(void *arg) {
    /*
     * Hilo: mientras el servidor corra, duerme 1 s y suma 1 a rs->uptime_secs
     * con el candado.
     */

    (void)arg;
    while (g_running) {
        /* Una vez por segundo suma uno al contador, bajo el candado. */
        sleep(1);
        RobotState *rs = robot_state_get();
        if (rs) {
            pthread_mutex_lock(&rs->lock);
            rs->uptime_secs++;
            pthread_mutex_unlock(&rs->lock);
        }
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════
   Hilo watchdog, para volver al modo autonomo una vez
   no hay usuarios logueados en el servidor
══════════════════════════════════════════════════════════ */
/* Cada cuantos segundos revisa el watchdog si quedan sesiones. */
#define WATCHDOG_INTERVAL_SECS 7

/* Pasa a modo autonomo si no lo estaba, y avisa con el sonido.
   reason: texto para el journal. */
static void set_auto_mode(const char *reason) {
    /*
     * Si el modo era manual, lo cambia a autonomo, actualiza los LEDs en la
     * pizarra, suelta el candado y toca el aviso de autonomo. Si ya era
     * autonomo, no hace nada.
     */

    /* Estado global. */
    RobotState *rs = robot_state_get();
    if (!rs) return;
    pthread_mutex_lock(&rs->lock);
    /* Solo actua si el modo era manual. */
    if (rs->mode != MODE_AUTONOMOUS) {
        rs->mode            = MODE_AUTONOMOUS;
        rs->leds.autonomous = 1;
        rs->leds.manual     = 0;
        /* Suelta el candado antes del aviso, que bloquea mientras suena. */
        pthread_mutex_unlock(&rs->lock);     
        printf("[watchdog] %s → modo AUTONOMO\n", reason);
        lib_audio_notify(NOTIFY_AUTONOMOUS);  
        return;
    }
    pthread_mutex_unlock(&rs->lock);
}

/* Hilo watchdog: si nadie tiene sesion abierta, el robot no debe quedar en
   manual esperando ordenes. */
static void *watchdog_thread(void *arg) {
    /*
     * Hilo: cada 7 s cuenta las sesiones activas. Si hay 0, llama a
     * set_auto_mode. Asi el robot no se queda en manual esperando ordenes de
     * alguien que ya cerro la pagina.
     */

    (void)arg;
    while (g_running) {
        /* Espera el intervalo y cuenta las sesiones activas. */
        sleep(WATCHDOG_INTERVAL_SECS);
        int sesiones = auth_active_sessions();
        // printf("[watchdog] tick — sesiones activas: %d\n", sesiones);
        if (sesiones == 0)
            set_auto_mode("sin sesiones activas");
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════
   Mapa de recorrido
══════════════════════════════════════════════════════════ */

/* Cada celda mide MAP_CELDA_CM (30 cm, ver robot_state.h): del orden del
   diametro del robot, asi que "celda visitada" es "el robot paso por aca". */

/* Mas alla de esta distancia un eco no marca obstaculo: de lejos, el cono del
   HC-SR04 ya abarca varias celdas y la posicion del eco es poco precisa. */
#define MAP_ALCANCE_CM 150.0

/* Pasa de cm del mundo (este y norte positivos) a la grilla: el origen queda
   en el centro del mapa y la fila 0 apunta al norte. 0 si cae afuera. */
static int celda_de(double x_cm, double y_cm, int *col, int *fil) {
    /*
     * Convierte una posicion en centimetros a columna y fila del mapa.
     * Devuelve 1 si cae dentro de la grilla.
     *
     *     columna = 15 + redondear(x / 30)
     *     fila    = 15 - redondear(y / 30)
     *
     * Se resta en la fila porque en la pantalla la fila 0 esta arriba (el
     * norte), y y crece hacia el norte.
     *
     * Ejemplo: x = 65, y = -20 -> columna 15 + 2 = 17, fila 15 - (-1) = 16.
     *
     * Para entender: lround redondea al entero mas cercano: 2,17 -> 2; -0,67
     * -> -1.
     */

    /* Columna: crece hacia el este desde el centro. */
    *col = MAP_COLS / 2 + (int)lround(x_cm / MAP_CELDA_CM);
    /* Fila: crece hacia el sur, por eso se resta. */
    *fil = MAP_ROWS / 2 - (int)lround(y_cm / MAP_CELDA_CM);
    /* 1 si la celda esta dentro de la grilla. */
    return *col >= 0 && *col < MAP_COLS && *fil >= 0 && *fil < MAP_ROWS;
}

/* Proyecta una lectura del radar sobre la grilla, con la pose que tenia el
   robot al medirla. Se llama con rs->lock tomado. */
static void map_marcar_lectura(RobotState *rs, const RadarLectura *l) {
    /*
     * Pone una lectura del radar en el mapa. Se llama con el candado tomado.
     *
     * 1. Sin eco -> no concluye nada y sale.
     * 2. radar_rayo da el origen y el rumbo del rayo. Calcula el vector del
     *    rayo: dx = sin(rumbo), dy = cos(rumbo).
     * 3. Limpia el camino: recorre el rayo en pasos de 10 cm hasta media celda
     *    antes del eco (o hasta 150 cm). Toda celda que estaba como obstaculo
     *    vuelve a "desconocida", porque el sonido paso por ahi sin rebotar.
     * 4. Si el eco esta a mas de 150 cm, no lo marca (de lejos el sensor es
     *    impreciso).
     * 5. Calcula la celda del eco: origen + distancia x (dx, dy).
     * 6. Si cae en la celda del propio robot, no la marca (el robot se
     *    encerraria solo).
     * 7. Marca esa celda como obstaculo.
     */

    /* Sin eco puede no haber nada, o puede ser una pared oblicua que desvio
       el pulso: no se concluye nada. */
    if (l->distancia_cm <= 0.0) return;

    /* Origen y direccion del rayo, en coordenadas del mundo. */
    double ox, oy, rumbo;
    radar_rayo(l, &ox, &oy, &rumbo);
    /* Vector unitario del rayo: seno hacia el este, coseno hacia el norte. */
    double dx = sin(rumbo * M_PI / 180.0), dy = cos(rumbo * M_PI / 180.0);

    /* Celda donde estaba el robot al medir. */
    int rc, rf;
    celda_de(l->x_cm, l->y_cm, &rc, &rf);

    /* Las celdas que el rayo cruza antes del eco estan libres. Si alguna
       habia quedado como obstaculo —un eco falso, o algo que se movio— vuelve
       a desconocida. Se deja media celda de margen antes del eco. */
    double libre = fmin(l->distancia_cm, MAP_ALCANCE_CM) - MAP_CELDA_CM / 2.0;
    /* Recorre el rayo en pasos de un tercio de celda. */
    for (double t = 0.0; t < libre; t += MAP_CELDA_CM / 3.0) {
        int c, f;
        /* Si el punto cae fuera del mapa, se deja de recorrer. */
        if (!celda_de(ox + t * dx, oy + t * dy, &c, &f)) break;
        if (rs->map.grid[f][c] == CELL_OBSTACLE) rs->map.grid[f][c] = CELL_UNKNOWN;
    }

    /* Un eco mas lejano que el alcance util no se marca. */
    if (l->distancia_cm > MAP_ALCANCE_CM) return;

    /* Celda donde cayo el eco. */
    int oc, of;
    if (!celda_de(ox + l->distancia_cm * dx, oy + l->distancia_cm * dy, &oc, &of))
        return;

    /* Un eco que cae en la celda del propio robot no se marca, o el robot se
       encerraria solo. */
    if (oc == rc && of == rf) return;

    /* Marca el obstaculo. */
    rs->map.grid[of][oc] = CELL_OBSTACLE;
}

/* ══════════════════════════════════════════════════════════
   Estado de los sensores
══════════════════════════════════════════════════════════ */

/* Un obstaculo al frente se detecta por cualquiera de dos vias:
   - distancia: alguna lectura del cono frontal (servo de 30 a 150 grados)
     que caiga en el camino del cuerpo, a menos del umbral hacia adelante
     (en_camino());
   - tiempo: el tiempo antes de chocar del radar por debajo de
     TTC_OBSTACULO_S. Es la via que reacciona antes cuanto mas rapido va el
     robot: a 25 cm/s saltaria a unos 30 cm de la pared.
   El robot real va a unos 2 cm/s, asi que en la practica manda la distancia:
   a esa velocidad el tiempo de choque solo bajaria de TTC_OBSTACULO_S a
   2.4 cm, ya dentro del umbral de distancia. */
/* La distancia por debajo de la cual hay obstaculo al frente se ajusta desde
   el panel (rs->movimiento.umbral_obstaculo_cm); arranca en
   UMBRAL_OBSTACULO_DEFECTO_CM, 10 cm. */
/* Tiempo de choque, en s, por debajo del cual hay obstaculo. */
#define TTC_OBSTACULO_S    1.2
/* Semiapertura del cono frontal: cuentan los angulos a 60 grados o menos
   del frente (servo a 30, 60, 90, 120 y 150). Los de 30 y 150 ven lo que el
   robot tiene casi al costado: un obstaculo que va a rozar con el cuerpo sale
   del haz de 60/120 grados al acercarse y aparece en ellos. */
#define CONO_FRONTAL_GRADOS 60

/* Radio del robot, en cm: mide unos 20 cm de diametro. El sensor va en el
   borde delantero (RADAR_EJE_ADELANTE_CM). */
#define ROBOT_RADIO_CM     10.0
/* Holgura a cada lado del cuerpo. En 0, un hueco de 20 cm entre dos
   obstaculos ya deja pasar al robot; subirla si roza por no ir recto. */
#define MARGEN_LATERAL_CM   0.0
/* Mitad del ancho del pasillo que barre el cuerpo al avanzar. */
#define PASILLO_CM (ROBOT_RADIO_CM + MARGEN_LATERAL_CM)

/* Una lectura del cono frontal cuenta solo si es reciente y si el robot sigue
   mirando hacia donde miraba al tomarla: tras un giro, "lo que habia al
   frente" es otra cosa. */
#define LECTURA_VIGENTE_S       2.0
#define LECTURA_GIRO_MAX_GRADOS 20.0

static uint32_t g_mapa_seq = 0;   /* ultima lectura del radar ya mapeada */

/* Ultima lectura de los sensores de desnivel (bits CAIDA_IZQ y CAIDA_DER).
   La escribe actualizar_estado(), que corre en el hilo de navegacion. */
static volatile int g_caida = 0;

/* Diferencia entre dos rumbos en grados, entre 0 y 180. */
static double diferencia_rumbo(double a, double b) {
    /*
     * La misma que en lib_radar.c: diferencia entre dos rumbos entre 0 y 180,
     * con el truco del +540.
     */

    return fabs(fmod(a - b + 540.0, 360.0) - 180.0);
}

/* 1 si el punto que vio una lectura esta en el camino del robot.

   La lectura se pasa al marco del robot: con el desvio respecto del frente,
   el punto queda d * sin(desvio) a un costado de la linea de avance y
   d * cos(desvio) por delante del sensor. Estorba solo si cae dentro del
   pasillo que barre el cuerpo (a menos de PASILLO_CM de la linea, es decir,
   el hueco que deja es de menos de 20 cm) y a menos de @p umbral_cm por
   delante. Asi, dos obstaculos con 20 cm o mas entre ellos no detienen al
   robot, y uno en el camino pero lejos tampoco. */
static int en_camino(double d, int desvio, double umbral_cm) {
    /*
     * Una lectura esta en el camino del robot?
     *
     * Descompone la distancia d segun el angulo respecto del frente (desvio):
     *
     *     lateral  = d x sin(|desvio|)    que tan al costado
     *     adelante = d x cos(|desvio|)    que tan adelante
     *
     * Devuelve 1 si lateral < 10 cm (dentro del ancho del robot) y adelante <
     * umbral.
     *
     * Lectura (umbral de 10 cm)        lateral   adelante   Estorba?
     * ------------------------------   -------   --------   ----------------
     * 90 grados (desvio 0) a 8 cm      0         8          Si
     * 120 grados (desvio 30) a 15 cm   7,5       13         No todavia
     * 120 grados a 10 cm               5         8,7        Si
     * 150 grados (desvio 60) a 18 cm   15,6      9          No: pasa al lado
     */

    double rad      = fabs((double)desvio) * M_PI / 180.0;
    double lateral  = d * sin(rad);
    double adelante = d * cos(rad);
    return lateral < PASILLO_CM && adelante < umbral_cm;
}

/* 1 si alguna lectura vigente del cono frontal esta en el camino del robot,
   a menos de @p umbral_cm por delante. */
static int obstaculo_al_frente(const RadarLectura *l, int n, double rumbo,
                               double umbral_cm) {
    /*
     * Revisa las lecturas del radar y devuelve 1 si alguna estorba. Descarta
     * las que:
     *
     * - estan fuera del cono (desvio mayor a 60 grados, o sea 0 grados y 180
     *   grados);
     * - nunca se midieron o no tuvieron eco;
     * - tienen mas de 2 s;
     * - se tomaron con un rumbo que difiere mas de 20 grados del actual (el
     *   robot giro y eso ya no esta al frente).
     *
     * Para las que quedan, pregunta a en_camino.
     *
     * Para entender: continue dentro de un for salta directo a la siguiente
     * vuelta.
     */

    for (int i = 0; i < n; i++) {
        /* Desvio de la lectura respecto del frente. */
        int desvio = l[i].angulo - RADAR_FRENTE;
        /* Descarta las lecturas fuera del cono, las nunca medidas o sin eco,
           las viejas y las tomadas con otro rumbo. */
        if (abs(desvio) > CONO_FRONTAL_GRADOS)                     continue;
        if (l[i].seq == 0 || l[i].distancia_cm <= 0.0)             continue;
        if (l[i].edad_s > LECTURA_VIGENTE_S)                       continue;
        if (diferencia_rumbo(l[i].rumbo_grados, rumbo) > LECTURA_GIRO_MAX_GRADOS) continue;
        if (en_camino(l[i].distancia_cm, desvio, umbral_cm)) return 1;
    }
    return 0;
}

/* Distancia de la lectura de un angulo dado, o -1 si no esta. */
static double lectura_en(const RadarLectura *l, int n, int angulo) {
    /*
     * Busca en las lecturas la de un angulo dado y devuelve su distancia, o
     * -1.
     */

    for (int i = 0; i < n; i++)
        if (l[i].angulo == angulo) return l[i].distancia_cm;
    return -1.0;
}

/* Lee el radar y la odometria y lo vuelca todo al estado global: sensores,
   velocidad, tiempo de choque, LED de obstaculo y mapa. Devuelve 1 si hay un
   obstaculo al frente.

   No mueve motores ni suena nada: se llama tambien en medio de las maniobras,
   para que el panel y el mapa sigan vivos mientras el robot retrocede o gira. */
static int actualizar_estado(RobotState *rs) {
    /*
     * Lee todos los sensores y escribe en la pizarra. Devuelve 1 si hay
     * obstaculo. No mueve motores: se llama tambien en medio de las maniobras
     * para que la web y el mapa sigan vivos.
     *
     * 1. Copia las lecturas del radar, el estado del tiempo de choque, la
     *    posicion y el umbral.
     * 2. Obstaculo = obstaculo_al_frente(...) o tiempo de choque entre 0 y 1,2
     *    s.
     * 3. Lee los sensores de desnivel y los guarda en g_caida.
     * 4. Con el candado de la pizarra:
     *    - Distancias al frente (90 grados), izquierda (180 grados) y derecha
     *      (0 grados).
     *    - Angulo del servo y las 7 lecturas para la web.
     *    - Velocidad, tiempo de choque y si viene del MPU.
     *    - Estado de cada sensor de desnivel ((caida & CAIDA_IZQ) != 0).
     *    - LED rojo = obstaculo o desnivel; recuerda si cambio.
     *    - Mapa: pone cada lectura nueva (su seq es mayor que g_mapa_seq) y
     *      actualiza g_mapa_seq. Asi cada lectura se dibuja una sola vez.
     *    - Marca la celda del robot como visitada y guarda su rumbo.
     * 5. Fuera del candado, si el LED rojo cambio, lo prende o apaga.
     */

    /* Copia de las lecturas del radar. */
    RadarLectura l[RADAR_N_ANGULOS];
    int n = radar_lecturas(l, RADAR_N_ANGULOS);

    /* Estado del tiempo de choque. */
    RadarChoque choque;
    radar_estado_choque(&choque);

    /* Pose del robot. */
    double x_cm, y_cm, rumbo;
    odom_get(&x_cm, &y_cm, &rumbo);

    /* Umbral de obstaculo que fijo el panel. */
    pthread_mutex_lock(&rs->lock);
    double umbral_cm = rs->movimiento.umbral_obstaculo_cm;
    pthread_mutex_unlock(&rs->lock);

    /* Hay obstaculo si algo del cono frontal esta en el camino del cuerpo o
       si el choque es inminente. */
    int obstaculo = obstaculo_al_frente(l, n, rumbo, umbral_cm) ||
                    (choque.ttc_s >= 0.0 && choque.ttc_s < TTC_OBSTACULO_S);

    /* Se lee en cada actualizacion, tambien en medio de las maniobras: a
       25 cm/s el robot avanza menos de 3 cm entre dos lecturas. */
    int caida = caida_leer();
    g_caida = caida;

    /* Desde aqui se escribe el estado global. */
    pthread_mutex_lock(&rs->lock);

    /* Distancias al frente, a la izquierda y a la derecha. */
    rs->sensors.front_cm = (float)lectura_en(l, n, RADAR_FRENTE);
    rs->sensors.left_cm  = (float)lectura_en(l, n, 180);
    rs->sensors.right_cm = (float)lectura_en(l, n, 0);

    /* Angulo del servo y copia de las lecturas para el panel. */
    rs->radar.angulo_servo = radar_angulo_actual();
    rs->radar.n = n < RADAR_LECTURAS_MAX ? n : RADAR_LECTURAS_MAX;
    for (int i = 0; i < rs->radar.n; i++) {
        rs->radar.lecturas[i].angulo       = l[i].angulo;
        rs->radar.lecturas[i].distancia_cm = (float)l[i].distancia_cm;
        rs->radar.lecturas[i].edad_s       = (float)l[i].edad_s;
    }

    /* Velocidad, tiempo de choque y de donde sale la velocidad. */
    rs->movimiento.velocidad_cm_s = (float)odom_velocidad_cm_s();
    rs->movimiento.ttc_s          = (float)choque.ttc_s;
    rs->movimiento.imu            = odom_usa_imu();

    /* Estado de cada sensor de desnivel. */
    rs->caida.izq = (caida & CAIDA_IZQ) != 0;
    rs->caida.der = (caida & CAIDA_DER) != 0;

    /* El LED de alerta se enciende igual ante un obstaculo o un desnivel. */
    int alerta = obstaculo || caida;
    /* Se recuerda si el LED cambia, para tocar el pin solo en ese caso. */
    int cambio_led = rs->leds.obstacle != alerta;
    rs->leds.obstacle = alerta;

    /* Cada lectura nueva del radar, una sola vez. */
    /* Mayor numero de secuencia visto en esta pasada. */
    uint32_t mayor = g_mapa_seq;
    for (int i = 0; i < n; i++) {
        /* Las lecturas ya mapeadas se saltan. */
        if (l[i].seq <= g_mapa_seq) continue;
        map_marcar_lectura(rs, &l[i]);
        if (l[i].seq > mayor) mayor = l[i].seq;
    }
    /* La proxima pasada solo mapeara lecturas mas nuevas. */
    g_mapa_seq = mayor;

    /* La celda del robot, al final: el robot esta ahi, asi que no puede ser
       un obstaculo aunque un eco lo haya marcado antes. */
    int c, f;
    if (celda_de(x_cm, y_cm, &c, &f)) {
        rs->map.robot_x = c;
        rs->map.robot_y = f;
        rs->map.grid[f][c] = CELL_VISITED;
    }
    /* Rumbo redondeado a grados enteros, de 0 a 359. */
    rs->map.robot_heading = ((int)lround(rumbo)) % 360;

    pthread_mutex_unlock(&rs->lock);

    /* El LED fisico se actualiza fuera del candado. */
    if (cambio_led) lib_leds_set(LED_OBSTACLE, alerta);
    return obstaculo;
}

/* ══════════════════════════════════════════════════════════
   Maniobras
══════════════════════════════════════════════════════════ */

/* Velocidad de avance normal, sobre 255. */
#define VEL_CRUCERO    210
/* Velocidad de los giros. */
#define VEL_GIRO       200
/* Velocidad del retroceso. */
#define VEL_RETROCESO  180
/* Duracion del retroceso ante un obstaculo, en ms. */
#define RETROCESO_MS   500
/* Resolucion de las esperas: cada cuanto se refresca el estado. */
#define PASO_MS         50

/* Con menos espacio que esto en todas las direcciones, se da media vuelta. */
#define ESPACIO_LIBRE_CM 40.0

/* Lecturas parecidas a la mejor, dentro de este margen, compiten al azar. */
#define MARGEN_EMPATE_CM 25.0

/* Velocidad de un giro en el lugar segun el modelo de los motores, en
   grados/s: las dos llantas a ODOM_VEL_MAX_CM_S en sentidos opuestos. Con los
   2 cm/s del robot real son unos 15 grados/s; antes se suponian 180. */
#define GIRO_GRADOS_S (2.0 * ODOM_VEL_MAX_CM_S / ODOM_ENTRE_EJES_CM * 180.0 / M_PI)
/* Tope de tiempo para un giro: el doble de lo que deberia tardar, para dejar
   margen a un piso resbaloso o a la bateria baja. */
#define GIRO_MARGEN_TOPE 2.0
/* Grados de tolerancia: un giro menor no se ejecuta, y uno en curso termina
   al quedar a esa distancia del objetivo. */
#define GIRO_TOLERANCIA    5.0

/* Devuelve el modo de operacion, leido bajo el candado. */
static OperationMode modo_actual(RobotState *rs) {
    /* Devuelve el modo leido con el candado tomado. */

    pthread_mutex_lock(&rs->lock);
    OperationMode m = rs->mode;
    pthread_mutex_unlock(&rs->lock);
    return m;
}

/* Espera troceada que mantiene vivos el panel y el mapa durante una maniobra.
   Devuelve 0 si hay que abortarla: el servidor se apaga o el usuario paso a
   modo manual. La odometria ya no depende de esto: corre en su propio hilo
   dentro de librobot. */
static int esperar_ms(RobotState *rs, int ms) {
    /*
     * Una espera "despierta" para usar en medio de las maniobras.
     *
     * Duerme en trozos de 50 ms y despues de cada uno:
     *
     * - llama a actualizar_estado (la web y el mapa siguen al dia mientras el
     *   robot retrocede o gira);
     * - si el servidor se esta apagando o el usuario paso a manual, devuelve 0
     *   para abortar la maniobra.
     *
     * Si completo la espera, devuelve 1.
     */

    /* Duerme en trozos de PASO_MS y refresca el estado en cada uno. */
    for (int t = 0; t < ms; t += PASO_MS) {
        usleep(PASO_MS * 1000);
        actualizar_estado(rs);
        /* Aborta si el servidor se apaga o si se salio del modo autonomo. */
        if (!g_running || modo_actual(rs) != MODE_AUTONOMOUS) return 0;
    }
    return 1;
}

/* Gira sobre su eje hasta cambiar el rumbo en @p grados (positivo: a la
   derecha). Lazo cerrado sobre el rumbo de la odometria —el giroscopo del
   MPU-6050 cuando esta—, no sobre un tiempo fijo. */
static int girar_grados(RobotState *rs, double grados) {
    /*
     * Gira sobre su eje hasta cambiar el rumbo en grados (positivo = derecha).
     * Mira el rumbo real (el giroscopio), no un tiempo fijo.
     *
     * 1. Si el giro es menor a 5 grados, no lo hace.
     * 2. Guarda el rumbo de partida y arranca el giro hacia el lado que indica
     *    el signo.
     * 3. Calcula un tiempo tope: |grados| / 14,3  grados/s x 2 + 1 s (14,3
     *    grados/s es lo que gira el robot real). Para 90 grados son ~13,6 s.
     * 4. Cada 50 ms (esperar_ms):
     *    - Si hay que abortar o aparece un desnivel, para.
     *    - Lee el rumbo y suma lo girado desde la vuelta anterior, con el
     *      truco de +540 para no saltar de 359 a 0, con signo.
     *    - Si ya giro lo pedido menos 5 grados, termina.
     * 5. Siempre termina frenando.
     */

    /* Un giro mas chico que la tolerancia no vale la pena. */
    if (fabs(grados) < GIRO_TOLERANCIA) return 1;

    /* Rumbo de partida y grados girados hasta ahora. */
    double anterior;
    odom_get(NULL, NULL, &anterior);
    double girado = 0.0;

    /* Arranca el giro hacia el lado que indica el signo. */
    if (grados > 0.0) motores_girar_derecha(VEL_GIRO);
    else              motores_girar_izquierda(VEL_GIRO);

    /* Tiempo maximo del giro, por si el rumbo no avanza. */
    int tope_ms = (int)(fabs(grados) / GIRO_GRADOS_S * 1000.0 * GIRO_MARGEN_TOPE) + 1000;
    /* 1 mientras el giro no se aborte. */
    int ok = 1;
    for (int t = 0; t < tope_ms; t += PASO_MS) {
        /* Espera un paso; si hay que abortar, sale. */
        if (!esperar_ms(rs, PASO_MS)) { ok = 0; break; }
        /* Girando en el lugar una esquina puede asomarse al borde: se
           detiene y el lazo principal se encarga del desnivel. */
        if (g_caida) { ok = 0; break; }

        /* Rumbo actual. */
        double r;
        odom_get(NULL, NULL, &r);
        girado  += fmod(r - anterior + 540.0, 360.0) - 180.0;   /* con signo */
        anterior = r;
        /* Termina al llegar al objetivo, dentro de la tolerancia. */
        if (fabs(girado) >= fabs(grados) - GIRO_TOLERANCIA) break;
    }
    /* Siempre termina frenando. */
    motores_detener();
    return ok;
}

/* Elige hacia donde seguir con un barrido fresco del radar: el angulo con mas
   espacio libre, sin contar el frente, que es donde estaba el obstaculo.
   Entre angulos parecidos decide al azar, para no repetir siempre la misma
   trayectoria: es la parte aleatoria del algoritmo de rebote.

   Devuelve el giro en grados de brujula: positivo a la derecha. */
static double elegir_giro(void) {
    /*
     * Elige hacia donde girar despues de un obstaculo. Es la parte "aleatoria"
     * del algoritmo de rebote.
     *
     * 1. Toma las 7 lecturas. Sin eco cuenta como 400 cm (lo mas libre
     *    posible).
     * 2. Busca la mayor distancia sin contar el frente.
     * 3. Si la mejor es menor a 40 cm -> media vuelta (180).
     * 4. Candidatos: los angulos (sin el frente) con distancia >= mejor - 25
     *    cm y >= 40 cm.
     * 5. Sortea uno con rand() % cantidad.
     * 6. Devuelve 90 - angulo: el servo a 0 grados (derecha) da +90; a 180
     *    grados (izquierda) da -90.
     *
     * Ejemplo: 0 grados=120, 30 grados=80, 60 grados=30, 90 grados=8, 120
     * grados=25, 150 grados=60, 180 grados=110. Mejor = 120. Candidatos: 0
     * grados y 180 grados. Sale +90 o -90 al azar.
     *
     * Para entender: rand() da un numero pseudoaleatorio; % nc lo lleva a
     * 0..nc-1. La semilla se fija en main con srand(time(NULL)), para que cada
     * arranque sortee distinto.
     */

    /* Lecturas del barrido. */
    RadarLectura l[RADAR_N_ANGULOS];
    int n = radar_lecturas(l, RADAR_N_ANGULOS);

    /* Sin eco = nada dentro de los 4 m del HC-SR04: lo mas libre posible. */
    double espacio[RADAR_N_ANGULOS];
    /* Mayor espacio libre encontrado fuera del frente. */
    double mejor = -1.0;
    for (int i = 0; i < n; i++) {
        espacio[i] = l[i].distancia_cm > 0.0 ? l[i].distancia_cm : 400.0;
        if (l[i].angulo != RADAR_FRENTE && espacio[i] > mejor) mejor = espacio[i];
    }

    /* Sin espacio en ninguna direccion: media vuelta. */
    if (mejor < ESPACIO_LIBRE_CM) return 180.0;

    /* Indices de los angulos que compiten por parecerse al mejor. Tambien
       tienen que estar despejados: si no, el sorteo podia volver a apuntar a
       la diagonal donde estaba el obstaculo. */
    int candidatos[RADAR_N_ANGULOS], nc = 0;
    for (int i = 0; i < n; i++)
        if (l[i].angulo != RADAR_FRENTE && espacio[i] >= mejor - MARGEN_EMPATE_CM &&
            espacio[i] >= ESPACIO_LIBRE_CM)
            candidatos[nc++] = i;

    /* Sorteo entre los candidatos. */
    int elegido = candidatos[rand() % nc];
    /* Servo a 0 = derecha = +90 de brujula; a 180 = izquierda = -90. */
    return (double)(RADAR_FRENTE - l[elegido].angulo);
}

/* Rutina ante un obstaculo: detenerse, retroceder, mirar alrededor con un
   barrido completo del radar y girar hacia el lado mas despejado. */
static void evadir(RobotState *rs) {
    /*
     * La maniobra ante un obstaculo.
     *
     * 1. Frena y espera 100 ms.
     * 2. Retrocede 500 ms y frena. Si la espera se aborto, sale.
     * 3. Guarda desde = radar_seq() y espera (hasta 3 s, en trozos de 50 ms) a
     *    que el radar complete un barrido nuevo: las lecturas anteriores se
     *    tomaron desde otro lugar.
     * 4. elegir_giro(), lo imprime y girar_grados().
     */

    /* Frena y espera un instante. */
    motores_detener();
    if (!esperar_ms(rs, 100)) return;

    /* Retrocede durante RETROCESO_MS y vuelve a frenar. */
    motores_retroceder(VEL_RETROCESO);
    int ok = esperar_ms(rs, RETROCESO_MS);
    motores_detener();
    /* Si la espera se aborto, se deja la maniobra. */
    if (!ok) return;

    /* Un barrido que empiece despues de detenerse: las lecturas de antes se
       tomaron desde otra posicion. Toma poco mas de un segundo. */
    uint32_t desde = radar_seq();
    for (int t = 0; t < 3000 && !radar_barrido_completo(desde); t += PASO_MS)
        if (!esperar_ms(rs, PASO_MS)) return;

    /* Elige el lado mas despejado y gira hacia el. */
    double giro = elegir_giro();
    printf("[auto] obstaculo: retrocede y gira %+.0f grados\n", giro);
    girar_grados(rs, giro);
}

/* Rutina ante un desnivel: el HC-SR04 no ve una grada, asi que no hay barrido
   que consultar. Retrocede mas que ante un obstaculo —el borde esta debajo
   del robot, no a unos centimetros— y gira hacia el lado contrario al del sensor que
   perdio el piso; con los dos, media vuelta. */
#define RETROCESO_CAIDA_MS 800

static void evadir_caida(RobotState *rs, int caida) {
    /*
     * La maniobra ante un desnivel. El ultrasonico no ve una grada, asi que no
     * consulta el radar.
     *
     * 1. Frena, espera 100 ms y retrocede 800 ms (mas que con un obstaculo: el
     *    borde esta debajo del robot).
     * 2. Elige el giro: borde a la izquierda -> +120 (a la derecha); a la
     *    derecha -> -120; los dos -> 180.
     * 3. girar_grados.
     */

    /* Frena y espera un instante. */
    motores_detener();
    if (!esperar_ms(rs, 100)) return;

    /* Retrocede mas que ante un obstaculo. */
    motores_retroceder(VEL_RETROCESO);
    int ok = esperar_ms(rs, RETROCESO_CAIDA_MS);
    motores_detener();
    if (!ok) return;

    /* Por defecto, media vuelta: los dos sensores perdieron el piso. */
    double giro = 180.0;
    if (caida == CAIDA_IZQ) giro =  120.0;   /* borde a la izquierda: a la derecha */
    if (caida == CAIDA_DER) giro = -120.0;
    printf("[auto] desnivel (%s): retrocede y gira %+.0f grados\n",
           caida == (CAIDA_IZQ | CAIDA_DER) ? "ambos" : caida == CAIDA_IZQ ? "izq" : "der",
           giro);
    /* Ejecuta el giro. */
    girar_grados(rs, giro);
}

/* ══════════════════════════════════════════════════════════
   Ciclo de limpieza
══════════════════════════════════════════════════════════ */

/* Segundos del reloj monotono, con fraccion. */
static double reloj_s(void) {
    /* La hora del reloj monotonico en segundos con decimales. */

    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

/* Suma al ciclo el tiempo transcurrido en modo autonomo y el area nueva
   recorrida. Devuelve 1 la vez que el ciclo se completa. */
static int avanzar_ciclo(RobotState *rs, OperationMode modo, double dt) {
    /*
     * Suma tiempo y area al ciclo de limpieza. Devuelve 1 solo en la vuelta en
     * que el ciclo se completa.
     *
     * Solo cuenta en modo autonomo y si el ciclo no termino:
     *
     * - segundos += dt.
     * - area = (visitadas ahora - visitadas al inicio) x 0,3 m x 0,3 m.
     * - Si hay meta y el progreso llego a 1: marca completo, suma uno a
     *   completados e imprime.
     *
     * Ejemplo: meta 4 m2 -> hacen falta ~45 celdas nuevas.
     */

    /* 1 solo en la llamada en que el ciclo se completa. */
    int termino = 0;
    pthread_mutex_lock(&rs->lock);
    /* Atajo al estado del ciclo. */
    CicloState *c = &rs->ciclo;
    /* Solo cuenta en modo autonomo y mientras el ciclo no haya terminado. */
    if (modo == MODE_AUTONOMOUS && !c->completo) {
        /* Suma el tiempo transcurrido. */
        c->segundos += dt;
        /* Area: celdas nuevas desde el inicio del ciclo por el area de una
           celda. */
        double celda_m = MAP_CELDA_CM / 100.0;
        int nuevas = robot_mapa_visitadas(rs) - c->visitadas_inicio;
        c->area_m2 = (nuevas > 0 ? nuevas : 0) * celda_m * celda_m;
        /* Con una meta configurada y alcanzada, el ciclo se marca completo. */
        if (c->tipo != CICLO_OFF && robot_ciclo_progreso(rs) >= 1.0) {
            c->completo = 1;
            c->completados++;
            termino = 1;
            printf("[ciclo] ciclo %u completado: %.0f s en autonomo, %.2f m2 recorridos\n",
                   c->completados, c->segundos, c->area_m2);
        }
    }
    pthread_mutex_unlock(&rs->lock);
    return termino;
}

/* 1 si los dos motores empujan hacia adelante. */
static int avanzando(void) {
    /*
     * Devuelve 1 si los dos motores tienen orden positiva (el robot va hacia
     * adelante).
     */

    /* Ultima orden de cada motor. */
    int izq, der;
    motores_get(&izq, &der);
    /* Avanza solo si las dos son positivas. */
    return izq > 0 && der > 0;
}

/* 1 si el ciclo de limpieza ya termino. */
static int ciclo_completo(RobotState *rs) {
    /* Devuelve si el ciclo termino, leido con el candado. */

    pthread_mutex_lock(&rs->lock);
    int c = rs->ciclo.completo;
    pthread_mutex_unlock(&rs->lock);
    return c;
}

/* ══════════════════════════════════════════════════════════
   Hilo de Navegación Autónoma y Lectura de Sensores
══════════════════════════════════════════════════════════ */
/* Hilo de navegacion. Diez veces por segundo refresca el estado, reacciona
   a obstaculos y desniveles, lleva el ciclo de limpieza y, en modo
   autonomo, decide el movimiento. */
static void *autonomous_thread(void *arg) {
    /*
     * El cerebro del robot: el hilo de navegacion, 10 vueltas por segundo.
     *
     * Guarda tres valores de la vuelta anterior: modo_anterior,
     * obstaculo_anterior, caida_anterior, y la hora.
     *
     * En cada vuelta:
     *
     * 1. Consigue la pizarra (si todavia no existe, espera 100 ms).
     * 2. obstaculo = actualizar_estado(rs) y lee el modo.
     * 3. Reaccion al obstaculo, solo cuando aparece (obstaculo &&
     *    !obstaculo_anterior): si iba hacia adelante, frena; toca el aviso.
     *    Pasa en cualquier modo, tambien en manual.
     * 4. Lo mismo con el desnivel, y ademas suma uno a rs->caida.eventos.
     * 5. Ciclo de limpieza: avanzar_ciclo con el tiempo real transcurrido. Si
     *    se completo, frena y toca el aviso de fin.
     * 6. Cambio de modo: si cambio desde la vuelta anterior, frena.
     * 7. Solo en autonomo, en este orden:
     *    - ciclo completo -> frenado;
     *    - desnivel -> evadir_caida;
     *    - obstaculo -> evadir;
     *    - si no -> motores_avanzar(210).
     * 8. Duerme 100 ms.
     *
     * Al apagarse el servidor (g_running = 0) sale del ciclo y frena.
     *
     * Para entender: "Detectar el flanco" (comparar con la vuelta anterior)
     * evita que el aviso suene 10 veces por segundo mientras el obstaculo
     * sigue ahi.
     */

    /* El argumento del hilo no se usa. */
    (void)arg;

    /* Valores de la vuelta anterior, para detectar cambios. */
    OperationMode modo_anterior = MODE_AUTONOMOUS;
    int obstaculo_anterior = 0;
    int caida_anterior = 0;
    /* Instante de la vuelta anterior. */
    double t_anterior = reloj_s();

    while (g_running) {
        /* Estado global; si todavia no existe, se espera. */
        RobotState *rs = robot_state_get();
        if (!rs) {
            usleep(100000);
            continue;
        }

        // 1. Sensores, velocidad, tiempo de choque y mapa, al estado global
        int obstaculo = actualizar_estado(rs);
        /* Modo de operacion en esta vuelta. */
        OperationMode modo = modo_actual(rs);

        /* 2. El sonido y la detencion son de la APARICION del obstaculo, en
           cualquier modo: tambien frenan al usuario que maneja a mano. Al
           despejarse el camino solo se apaga el LED. */
        /* Solo se frena si el robot va hacia adelante: el obstaculo y el
           desnivel estan al frente, y cortar un retroceso o un giro impediria
           justamente alejarse de ellos. El panel manda la orden una vez al
           presionar, asi que un freno a destiempo dejaba el robot quieto. */
        int va_adelante = avanzando();

        /* Solo en el instante en que aparece el obstaculo. */
        if (obstaculo && !obstaculo_anterior) {
            if (va_adelante) motores_detener();
            lib_audio_notify(NOTIFY_OBSTACLE);
        }
        obstaculo_anterior = obstaculo;

        /* Igual con un desnivel. El sonido es el del obstaculo: avisa que el
           robot freno por algo que tiene delante. */
        int caida = g_caida;
        /* Solo en el instante en que aparece el desnivel. */
        if (caida && !caida_anterior) {
            if (va_adelante) motores_detener();
            /* Cuenta el evento para el panel. */
            pthread_mutex_lock(&rs->lock);
            rs->caida.eventos++;
            pthread_mutex_unlock(&rs->lock);
            printf("[caida] sin piso bajo %s\n",
                   caida == (CAIDA_IZQ | CAIDA_DER) ? "las dos esquinas"
                   : caida == CAIDA_IZQ ? "la esquina izquierda" : "la esquina derecha");
            lib_audio_notify(NOTIFY_OBSTACLE);
        }
        caida_anterior = caida;

        /* El ciclo cuenta tambien el tiempo de las maniobras: dt sale del
           reloj, no del periodo del lazo. */
        /* Tiempo transcurrido desde la vuelta anterior. */
        double ahora = reloj_s();
        /* Si el ciclo se completo en esta vuelta, frena y avisa. */
        if (avanzar_ciclo(rs, modo, ahora - t_anterior)) {
            motores_detener();
            lib_audio_notify(NOTIFY_CYCLE_END);
        }
        t_anterior = ahora;

        /* ── Detener motores inmediatamente al cambiar de modo ── */
        if (modo_anterior != modo) {
            motores_detener();
            printf("[auto] Cambio de modo: %s → %s, motores detenidos\n",
                   modo_anterior == MODE_AUTONOMOUS ? "AUTO" : "MANUAL",
                   modo          == MODE_AUTONOMOUS ? "AUTO" : "MANUAL");
            modo_anterior = modo;
        }

        // 3. Lógica reactiva, solo en modo autónomo
        if (modo == MODE_AUTONOMOUS) {
            /* Con el ciclo completo el robot espera quieto hasta que el
               usuario empiece otro desde el panel. */
            if (ciclo_completo(rs)) motores_detener();
            else if (caida)         evadir_caida(rs, caida);
            else if (obstaculo)     evadir(rs);
            else                    motores_avanzar(VEL_CRUCERO);
        }

        // Lazo de decisión a 10 Hz; el radar barre a su ritmo en librobot
        usleep(100000);
    }

    /* Al terminar el hilo los motores quedan frenados. */
    motores_detener();
    return NULL;
}

// Handler de la signal
/* Servidor HTTP; lo detiene el manejador de senales. */
static struct MHD_Daemon *g_daemon = NULL;
/* Manejador de SIGINT y SIGTERM: baja la bandera para que los hilos
   terminen y detiene el servidor HTTP. */
static void on_signal(int s) {
    /*
     * Se ejecuta cuando llega SIGINT (Ctrl+C) o SIGTERM (cuando systemd
     * detiene el servicio). Pone g_running = 0 para que los hilos terminen y
     * detiene el servidor HTTP.
     *
     * Para entender: Una senal es un aviso que el sistema operativo manda a un
     * proceso. signal(SIGTERM, on_signal) en main dice "cuando llegue esta
     * senal, ejecuta esta funcion".
     */

    (void)s;
    g_running = 0;
    if (g_daemon) MHD_stop_daemon(g_daemon);
    printf("\n[server] desconectando servidor\n");
}

/* ══════════════════════════════════════════════════════════
   Servidor MAIN
══════════════════════════════════════════════════════════ */
int main(void) {
    /*
     * Donde empieza el programa.
     *
     * 1. setvbuf(stdout, NULL, _IOLBF, 0) - los printf salen linea por linea.
     *    Sin esto, bajo systemd se juntarian en bloques de 4 KB y el journal
     *    los mostraria tarde.
     * 2. Imprime el encabezado.
     * 3. robot_state_init() crea la pizarra.
     * 4. srand(time(NULL)) fija la semilla del sorteo de giros.
     * 5. robot_init() enciende el hardware. Si falla, sale con 1 (systemd lo
     *    reintentara).
     * 6. lib_audio_init(NULL) y auth_init() (si fallan, solo avisan).
     * 7. Fija el modo autonomo.
     * 8. Registra on_signal para SIGINT y SIGTERM.
     * 9. MHD_start_daemon(...) arranca el servidor HTTP. Si falla (por
     *    ejemplo, con el puerto ocupado), libera todo y sale con 1. Sus
     *    opciones:
     *    - un hilo por conexion;
     *    - puerto 8080;
     *    - handle_request como manejador;
     *    - request_done al terminar cada pedido;
     *    - 30 s de tiempo maximo por conexion.
     * 10. Toca los avisos de inicio y de autonomo (~6 s, bloquea) y sincroniza
     *     los LEDs.
     * 11. Lanza los hilos de uptime, watchdog y navegacion.
     * 12. while (g_running) sleep(1); - el hilo principal solo espera.
     * 13. Apagado: espera a los tres hilos con pthread_join y libera cada
     *     modulo: sesiones, audio, hardware, pizarra, LEDs.
     *
     * Para entender: MHD_start_daemon recibe una lista de opciones de largo
     * variable, terminada en MHD_OPTION_END. Cada opcion es un par
     * nombre-valor.
     */

    /* Bajo systemd la salida va a un pipe, no a una terminal, y printf la
       acumularia en bloques de 4 KB: journalctl mostraria los eventos tarde
       o nunca si el proceso muere. Linea a linea, como en la consola. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("╔════════════════════════════════════╗\n");
    printf("║  Servidor Vaccum Robot             ║\n");
    printf("╚════════════════════════════════════╝\n\n");

    /* Crea el estado global. */
    if (robot_state_init() < 0)  { fprintf(stderr, "[main] estado inicial fallo\n");}

    /* La eleccion de rumbo tras un obstaculo desempata al azar. */
    srand((unsigned)time(NULL));

    /* --- INICIALIZAR EL HARDWARE ---
       robot_init() abre la sesion con pigpiod, deja listos motores y LEDs,
       calibra el MPU-6050 y arranca la odometria y el barrido del radar. El
       servidor no toca GPIO: todo pasa por librobot. */
    if (robot_init() < 0) {
        fprintf(stderr, "[main] hardware init failed (requiere pigpiod corriendo)\n");
        robot_state_destroy();
        return 1;
    }

    /* Audio y sesiones. Si fallan se avisa, pero el servidor sigue. */
    if (lib_audio_init(NULL) < 0)  { fprintf(stderr, "[main] audio init failed\n");}
    if (auth_init()        < 0)  { fprintf(stderr, "[main] autorizacion inicial fallo\n");}

    // Iniciar en modo autonomo. Se fija antes de abrir el puerto para que un
    // cambio de modo pedido desde el panel no quede pisado despues.
    {
        RobotState *rs = robot_state_get();
        if (rs) {
            pthread_mutex_lock(&rs->lock);
            rs->mode = MODE_AUTONOMOUS;
            pthread_mutex_unlock(&rs->lock);
        }
    }

    /* Ctrl+C y la senal de parada de systemd apagan el servidor en orden. */
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    /* El servidor HTTP se abre antes de los sonidos de inicio: cada
       lib_audio_notify() bloquea hasta que termina el MP3 (unos 6 s entre los
       dos), y el panel debe ser alcanzable cuanto antes. MHD atiende en sus
       propios hilos, asi que no espera al main. */
    /* Argumentos: un hilo por conexion, el puerto, sin filtro de clientes,
       el
       manejador de peticiones, el aviso de fin de peticion y 30 s de tiempo
       de
       espera por conexion. */
    g_daemon = MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION | MHD_USE_INTERNAL_POLLING_THREAD,
        SERVER_PORT,
        NULL, NULL,
        handle_request, NULL,
        MHD_OPTION_NOTIFY_COMPLETED, request_done, NULL,
        MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)30,
        MHD_OPTION_END
    );

    /* Si el puerto no se pudo abrir, se libera todo y se sale con error. */
    if (!g_daemon) {
        fprintf(stderr, "[main] fallo al iniciar el daemon en puerto %d\n", SERVER_PORT);
        auth_destroy();
        lib_audio_destroy();
        robot_shutdown();
        robot_state_destroy();
        lib_leds_destroy();
        return 1;
    }

    /* El servidor ya atiende. */
    printf("[server] Servidor escuchando en puerto %d\n\n", SERVER_PORT);
    printf("  Dev:               http://localhost:%d\n", SERVER_PORT);
    printf("  Red local:         http://<hostname -I>:%d\n",   SERVER_PORT);

    // Notificacion de encendido
    lib_audio_notify(NOTIFY_STARTUP);

    printf("[main] Modo inicial: AUTONOMO\n");
    lib_audio_notify(NOTIFY_AUTONOMOUS);

    /* Lleva los LEDs fisicos al estado inicial. */
    lib_leds_sync_from_state();

    // Inicializar Threads
    pthread_t tid_uptime, tid_watchdog;
    /* Hilos del contador de tiempo encendido y del watchdog. */
    pthread_create(&tid_uptime,   NULL, uptime_thread,   NULL);
    pthread_create(&tid_watchdog, NULL, watchdog_thread, NULL);

    // --- INICIAR HILO DE NAVEGACIÓN AUTÓNOMA ---
    pthread_t auto_tid;
    pthread_create(&auto_tid, NULL, autonomous_thread, NULL);

    /* El hilo principal duerme hasta que llegue una senal. */
    while (g_running) sleep(1);

    // --- SECUENCIA DE APAGADO ---
    g_running = 0;
    /* Espera a que terminen los tres hilos. */
    pthread_join(tid_uptime,   NULL);
    pthread_join(tid_watchdog, NULL);
    pthread_join(auto_tid,     NULL); // Esperar a que el hilo autónomo termine
    
    /* Libera cada modulo. */
    auth_destroy();
    lib_audio_destroy();
    robot_shutdown(); // Detener motores, apagar LEDs y cerrar la sesion de pigpiod
    robot_state_destroy();
    lib_leds_destroy(); // Apagar LEDs físicos
    
    printf("[server] Close.\n");
    return 0;
}
