/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *

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
    struct MHD_Response *r =
        MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(r, "Location", to);
    enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_FOUND, r);
    MHD_destroy_response(r);
    return ret;
}

/* Responde 404 con un JSON de error. */
static enum MHD_Result send_404(struct MHD_Connection *conn) {
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

    /* Ninguna ruta coincidio. */
    return send_404(conn);
}

/* La llama libmicrohttpd al terminar cada peticion: libera el acumulador
   del cuerpo. */
static void request_done(void *cls, struct MHD_Connection *conn,
                          void **con_cls,
                          enum MHD_RequestTerminationCode toe)
{
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
   - distancia: alguna lectura del cono frontal (servo a 60, 90 o 120 grados)
     por debajo de DIST_OBSTACULO_CM;
   - tiempo: el tiempo antes de chocar del radar por debajo de
     TTC_OBSTACULO_S. Es la via que reacciona antes cuanto mas rapido va el
     robot: a 25 cm/s salta a unos 30 cm de la pared.
   La distancia cubre al robot quieto o muy lento, donde no hay tiempo de
   choque que calcular. */
/* Distancia, en cm, por debajo de la cual hay obstaculo. */
#define DIST_OBSTACULO_CM 20.0
/* Tiempo de choque, en s, por debajo del cual hay obstaculo. */
#define TTC_OBSTACULO_S    1.2
/* Semiapertura del cono frontal: cuentan los angulos a 30 grados o menos
   del frente. */
#define CONO_FRONTAL_GRADOS 30

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
    return fabs(fmod(a - b + 540.0, 360.0) - 180.0);
}

/* Menor distancia vigente en el cono frontal, o -1 si no hay ninguna. */
static double distancia_al_frente(const RadarLectura *l, int n, double rumbo) {
    /* Menor distancia encontrada; -1 mientras no haya ninguna. */
    double d = -1.0;
    for (int i = 0; i < n; i++) {
        /* Descarta las lecturas fuera del cono, las nunca medidas o sin eco,
           las
           viejas y las tomadas con otro rumbo. */
        if (abs(l[i].angulo - RADAR_FRENTE) > CONO_FRONTAL_GRADOS) continue;
        if (l[i].seq == 0 || l[i].distancia_cm <= 0.0)             continue;
        if (l[i].edad_s > LECTURA_VIGENTE_S)                       continue;
        if (diferencia_rumbo(l[i].rumbo_grados, rumbo) > LECTURA_GIRO_MAX_GRADOS) continue;
        /* Se queda con la menor. */
        if (d < 0.0 || l[i].distancia_cm < d) d = l[i].distancia_cm;
    }
    return d;
}

/* Distancia de la lectura de un angulo dado, o -1 si no esta. */
static double lectura_en(const RadarLectura *l, int n, int angulo) {
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
    /* Copia de las lecturas del radar. */
    RadarLectura l[RADAR_N_ANGULOS];
    int n = radar_lecturas(l, RADAR_N_ANGULOS);

    /* Estado del tiempo de choque. */
    RadarChoque choque;
    radar_estado_choque(&choque);

    /* Pose del robot. */
    double x_cm, y_cm, rumbo;
    odom_get(&x_cm, &y_cm, &rumbo);

    /* Menor distancia vigente al frente. */
    double d_frente  = distancia_al_frente(l, n, rumbo);
    /* Hay obstaculo si esta muy cerca o si el choque es inminente. */
    int    obstaculo = (d_frente > 0.0 && d_frente < DIST_OBSTACULO_CM) ||
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

/* Tope de tiempo para un giro: a VEL_GIRO el robot gira unos 180 grados/s;
   el tope deja margen para piso resbaloso o bateria baja. */
#define GIRO_MS_POR_GRADO 20
/* Grados de tolerancia: un giro menor no se ejecuta, y uno en curso termina
   al quedar a esa distancia del objetivo. */
#define GIRO_TOLERANCIA    5.0

/* Devuelve el modo de operacion, leido bajo el candado. */
static OperationMode modo_actual(RobotState *rs) {
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
    int tope_ms = (int)(fabs(grados) * GIRO_MS_POR_GRADO) + 1000;
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

    /* Indices de los angulos que compiten por parecerse al mejor. */
    int candidatos[RADAR_N_ANGULOS], nc = 0;
    for (int i = 0; i < n; i++)
        if (l[i].angulo != RADAR_FRENTE && espacio[i] >= mejor - MARGEN_EMPATE_CM)
            candidatos[nc++] = i;

    /* Sorteo entre los candidatos. */
    int elegido = candidatos[rand() % nc];
    /* Servo a 0 = derecha = +90 de brujula; a 180 = izquierda = -90. */
    return (double)(RADAR_FRENTE - l[elegido].angulo);
}

/* Rutina ante un obstaculo: detenerse, retroceder, mirar alrededor con un
   barrido completo del radar y girar hacia el lado mas despejado. */
static void evadir(RobotState *rs) {
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
   del robot, no a 20 cm— y gira hacia el lado contrario al del sensor que
   perdio el piso; con los dos, media vuelta. */
#define RETROCESO_CAIDA_MS 800

static void evadir_caida(RobotState *rs, int caida) {
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
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

/* Suma al ciclo el tiempo transcurrido en modo autonomo y el area nueva
   recorrida. Devuelve 1 la vez que el ciclo se completa. */
static int avanzar_ciclo(RobotState *rs, OperationMode modo, double dt) {
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
    /* Ultima orden de cada motor. */
    int izq, der;
    motores_get(&izq, &der);
    /* Avanza solo si las dos son positivas. */
    return izq > 0 && der > 0;
}

/* 1 si el ciclo de limpieza ya termino. */
static int ciclo_completo(RobotState *rs) {
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
    (void)s;
    g_running = 0;
    if (g_daemon) MHD_stop_daemon(g_daemon);
    printf("\n[server] desconectando servidor\n");
}

/* ══════════════════════════════════════════════════════════
   Servidor MAIN
══════════════════════════════════════════════════════════ */
int main(void) {
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
