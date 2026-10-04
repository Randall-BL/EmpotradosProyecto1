/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>

#include "robot_state.h"
#include "lib_audio.h"
#include "lib_leds.h"
#include "auth.h"
#include "api.h"
#include "lib_robot.h"
#include "lib_motors.h"
#include "lib_odom.h"
#include "lib_radar.h"

/* El estado global guarda una lectura por angulo del radar. */
_Static_assert(RADAR_LECTURAS_MAX >= RADAR_N_ANGULOS,
               "RADAR_LECTURAS_MAX (robot_state.h) no alcanza para el barrido del radar");

// Configuraciones del servidor iniciales
#define SERVER_PORT      8080
#define WWW_ROOT         "./www"
#define MAX_UPLOAD_BYTES (32 * 1024)

// Body del POST
typedef struct {
    char  *data;
    size_t used;
} UploadBuf;

// Serving File
static const char *mime_for(const char *path) {
    const char *d = strrchr(path, '.');
    if (!d)                       return "application/octet-stream";
    if (strcmp(d, ".html") == 0)  return "text/html; charset=utf-8";
    if (strcmp(d, ".css")  == 0)  return "text/css";
    if (strcmp(d, ".js")   == 0)  return "application/javascript";
    if (strcmp(d, ".ico")  == 0)  return "image/x-icon";
    if (strcmp(d, ".png")  == 0)  return "image/png";
    if (strcmp(d, ".svg")  == 0)  return "image/svg+xml";
    return "application/octet-stream";
}

static enum MHD_Result serve_file(struct MHD_Connection *conn,
                                   const char *rel)
{
    char path[512];
    snprintf(path, sizeof(path), "%s%s", WWW_ROOT, rel);

    int fd = open(path, O_RDONLY);
    if (fd < 0) return MHD_NO;

    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) { close(fd); return MHD_NO; }

    struct MHD_Response *r =
        MHD_create_response_from_fd((uint64_t)st.st_size, fd);
    MHD_add_response_header(r, "Content-Type",  mime_for(path));
    MHD_add_response_header(r, "Cache-Control", "no-cache");
    enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, r);
    MHD_destroy_response(r);
    return ret;
}

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
    (void)cls; (void)version;

    // First call para los acumuladores
    if (*con_cls == NULL) {
        UploadBuf *buf = calloc(1, sizeof(UploadBuf));
        if (!buf) return MHD_NO;
        *con_cls = buf;
        return MHD_YES;
    }

    UploadBuf *ub = *con_cls;

    /* Acumular los POST body */
    if (*upload_data_size > 0) {
        size_t need = ub->used + *upload_data_size + 1;
        if (need > MAX_UPLOAD_BYTES) return MHD_NO;
        ub->data = realloc(ub->data, need);
        if (!ub->data) return MHD_NO;
        memcpy(ub->data + ub->used, upload_data, *upload_data_size);
        ub->used             += *upload_data_size;
        ub->data[ub->used]   = '\0';
        *upload_data_size    = 0;
        return MHD_YES;
    }

    /* Todos los body recibidos */
    int is_get  = strcmp(method, "GET")  == 0;
    int is_post = strcmp(method, "POST") == 0;
    const char *body = ub->data ? ub->data : "";

    /* Establecer los CORS de seguridad */
    if (strcmp(method, "OPTIONS") == 0) {
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
    if (is_get && (strcmp(url, "/") == 0 || strcmp(url, "/index.html") == 0))
        return serve_file(conn, "/login.html");

    if (is_post && strcmp(url, "/api/login") == 0)
        return api_login(conn, body, ub->used);

    /* ── Autenticacion ── */
    if (!auth_check_request(conn)) {
        if (strncmp(url, "/api/", 5) == 0) return send_401(conn);
        return send_redirect(conn, "/");
    }

    /* ── Rutas de paginas protegidas ── */
    if (is_get && strcmp(url, "/dashboard") == 0)
        return serve_file(conn, "/dashboard.html");

    /* ── Rutas de API protegidas ── */
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

    return send_404(conn);
}

static void request_done(void *cls, struct MHD_Connection *conn,
                          void **con_cls,
                          enum MHD_RequestTerminationCode toe)
{
    (void)cls; (void)conn; (void)toe;
    UploadBuf *ub = *con_cls;
    if (ub) { free(ub->data); free(ub); *con_cls = NULL; }
}

/* ══════════════════════════════════════════════════════════
   Hilo de actualizacion
══════════════════════════════════════════════════════════ */
static volatile int g_running = 1;

static void *uptime_thread(void *arg) {
    (void)arg;
    while (g_running) {
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
#define WATCHDOG_INTERVAL_SECS 7

static void set_auto_mode(const char *reason) {
    RobotState *rs = robot_state_get();
    if (!rs) return;
    pthread_mutex_lock(&rs->lock);
    if (rs->mode != MODE_AUTONOMOUS) {
        rs->mode            = MODE_AUTONOMOUS;
        rs->leds.autonomous = 1;
        rs->leds.manual     = 0;
        pthread_mutex_unlock(&rs->lock);     
        printf("[watchdog] %s → modo AUTONOMO\n", reason);
        lib_audio_notify(NOTIFY_AUTONOMOUS);  
        return;
    }
    pthread_mutex_unlock(&rs->lock);
}

static void *watchdog_thread(void *arg) {
    (void)arg;
    while (g_running) {
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
    *col = MAP_COLS / 2 + (int)lround(x_cm / MAP_CELDA_CM);
    *fil = MAP_ROWS / 2 - (int)lround(y_cm / MAP_CELDA_CM);
    return *col >= 0 && *col < MAP_COLS && *fil >= 0 && *fil < MAP_ROWS;
}

/* Proyecta una lectura del radar sobre la grilla, con la pose que tenia el
   robot al medirla. Se llama con rs->lock tomado. */
static void map_marcar_lectura(RobotState *rs, const RadarLectura *l) {
    /* Sin eco puede no haber nada, o puede ser una pared oblicua que desvio
       el pulso: no se concluye nada. */
    if (l->distancia_cm <= 0.0) return;

    double ox, oy, rumbo;
    radar_rayo(l, &ox, &oy, &rumbo);
    double dx = sin(rumbo * M_PI / 180.0), dy = cos(rumbo * M_PI / 180.0);

    int rc, rf;
    celda_de(l->x_cm, l->y_cm, &rc, &rf);

    /* Las celdas que el rayo cruza antes del eco estan libres. Si alguna
       habia quedado como obstaculo —un eco falso, o algo que se movio— vuelve
       a desconocida. Se deja media celda de margen antes del eco. */
    double libre = fmin(l->distancia_cm, MAP_ALCANCE_CM) - MAP_CELDA_CM / 2.0;
    for (double t = 0.0; t < libre; t += MAP_CELDA_CM / 3.0) {
        int c, f;
        if (!celda_de(ox + t * dx, oy + t * dy, &c, &f)) break;
        if (rs->map.grid[f][c] == CELL_OBSTACLE) rs->map.grid[f][c] = CELL_UNKNOWN;
    }

    if (l->distancia_cm > MAP_ALCANCE_CM) return;

    int oc, of;
    if (!celda_de(ox + l->distancia_cm * dx, oy + l->distancia_cm * dy, &oc, &of))
        return;

    /* Un eco que cae en la celda del propio robot no se marca, o el robot se
       encerraria solo. */
    if (oc == rc && of == rf) return;

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
#define DIST_OBSTACULO_CM 20.0
#define TTC_OBSTACULO_S    1.2
#define CONO_FRONTAL_GRADOS 30

/* Una lectura del cono frontal cuenta solo si es reciente y si el robot sigue
   mirando hacia donde miraba al tomarla: tras un giro, "lo que habia al
   frente" es otra cosa. */
#define LECTURA_VIGENTE_S       2.0
#define LECTURA_GIRO_MAX_GRADOS 20.0

static uint32_t g_mapa_seq = 0;   /* ultima lectura del radar ya mapeada */

static double diferencia_rumbo(double a, double b) {
    return fabs(fmod(a - b + 540.0, 360.0) - 180.0);
}

/* Menor distancia vigente en el cono frontal, o -1 si no hay ninguna. */
static double distancia_al_frente(const RadarLectura *l, int n, double rumbo) {
    double d = -1.0;
    for (int i = 0; i < n; i++) {
        if (abs(l[i].angulo - RADAR_FRENTE) > CONO_FRONTAL_GRADOS) continue;
        if (l[i].seq == 0 || l[i].distancia_cm <= 0.0)             continue;
        if (l[i].edad_s > LECTURA_VIGENTE_S)                       continue;
        if (diferencia_rumbo(l[i].rumbo_grados, rumbo) > LECTURA_GIRO_MAX_GRADOS) continue;
        if (d < 0.0 || l[i].distancia_cm < d) d = l[i].distancia_cm;
    }
    return d;
}

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
    RadarLectura l[RADAR_N_ANGULOS];
    int n = radar_lecturas(l, RADAR_N_ANGULOS);

    RadarChoque choque;
    radar_estado_choque(&choque);

    double x_cm, y_cm, rumbo;
    odom_get(&x_cm, &y_cm, &rumbo);

    double d_frente  = distancia_al_frente(l, n, rumbo);
    int    obstaculo = (d_frente > 0.0 && d_frente < DIST_OBSTACULO_CM) ||
                       (choque.ttc_s >= 0.0 && choque.ttc_s < TTC_OBSTACULO_S);

    pthread_mutex_lock(&rs->lock);

    rs->sensors.front_cm = (float)lectura_en(l, n, RADAR_FRENTE);
    rs->sensors.left_cm  = (float)lectura_en(l, n, 180);
    rs->sensors.right_cm = (float)lectura_en(l, n, 0);

    rs->radar.angulo_servo = radar_angulo_actual();
    rs->radar.n = n < RADAR_LECTURAS_MAX ? n : RADAR_LECTURAS_MAX;
    for (int i = 0; i < rs->radar.n; i++) {
        rs->radar.lecturas[i].angulo       = l[i].angulo;
        rs->radar.lecturas[i].distancia_cm = (float)l[i].distancia_cm;
        rs->radar.lecturas[i].edad_s       = (float)l[i].edad_s;
    }

    rs->movimiento.velocidad_cm_s = (float)odom_velocidad_cm_s();
    rs->movimiento.ttc_s          = (float)choque.ttc_s;
    rs->movimiento.imu            = odom_usa_imu();

    int cambio_led = rs->leds.obstacle != obstaculo;
    rs->leds.obstacle = obstaculo;

    /* Cada lectura nueva del radar, una sola vez. */
    uint32_t mayor = g_mapa_seq;
    for (int i = 0; i < n; i++) {
        if (l[i].seq <= g_mapa_seq) continue;
        map_marcar_lectura(rs, &l[i]);
        if (l[i].seq > mayor) mayor = l[i].seq;
    }
    g_mapa_seq = mayor;

    /* La celda del robot, al final: el robot esta ahi, asi que no puede ser
       un obstaculo aunque un eco lo haya marcado antes. */
    int c, f;
    if (celda_de(x_cm, y_cm, &c, &f)) {
        rs->map.robot_x = c;
        rs->map.robot_y = f;
        rs->map.grid[f][c] = CELL_VISITED;
    }
    rs->map.robot_heading = ((int)lround(rumbo)) % 360;

    pthread_mutex_unlock(&rs->lock);

    if (cambio_led) lib_leds_set(LED_OBSTACLE, obstaculo);
    return obstaculo;
}

/* ══════════════════════════════════════════════════════════
   Maniobras
══════════════════════════════════════════════════════════ */

#define VEL_CRUCERO    210
#define VEL_GIRO       200
#define VEL_RETROCESO  180
#define RETROCESO_MS   500
#define PASO_MS         50

/* Con menos espacio que esto en todas las direcciones, se da media vuelta. */
#define ESPACIO_LIBRE_CM 40.0

/* Lecturas parecidas a la mejor, dentro de este margen, compiten al azar. */
#define MARGEN_EMPATE_CM 25.0

/* Tope de tiempo para un giro: a VEL_GIRO el robot gira unos 180 grados/s;
   el tope deja margen para piso resbaloso o bateria baja. */
#define GIRO_MS_POR_GRADO 20
#define GIRO_TOLERANCIA    5.0

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
    for (int t = 0; t < ms; t += PASO_MS) {
        usleep(PASO_MS * 1000);
        actualizar_estado(rs);
        if (!g_running || modo_actual(rs) != MODE_AUTONOMOUS) return 0;
    }
    return 1;
}

/* Gira sobre su eje hasta cambiar el rumbo en @p grados (positivo: a la
   derecha). Lazo cerrado sobre el rumbo de la odometria —el giroscopo del
   MPU-6050 cuando esta—, no sobre un tiempo fijo. */
static int girar_grados(RobotState *rs, double grados) {
    if (fabs(grados) < GIRO_TOLERANCIA) return 1;

    double anterior;
    odom_get(NULL, NULL, &anterior);
    double girado = 0.0;

    if (grados > 0.0) motores_girar_derecha(VEL_GIRO);
    else              motores_girar_izquierda(VEL_GIRO);

    int tope_ms = (int)(fabs(grados) * GIRO_MS_POR_GRADO) + 1000;
    int ok = 1;
    for (int t = 0; t < tope_ms; t += PASO_MS) {
        if (!esperar_ms(rs, PASO_MS)) { ok = 0; break; }

        double r;
        odom_get(NULL, NULL, &r);
        girado  += fmod(r - anterior + 540.0, 360.0) - 180.0;   /* con signo */
        anterior = r;
        if (fabs(girado) >= fabs(grados) - GIRO_TOLERANCIA) break;
    }
    motores_detener();
    return ok;
}

/* Elige hacia donde seguir con un barrido fresco del radar: el angulo con mas
   espacio libre, sin contar el frente, que es donde estaba el obstaculo.
   Entre angulos parecidos decide al azar, para no repetir siempre la misma
   trayectoria: es la parte aleatoria del algoritmo de rebote.

   Devuelve el giro en grados de brujula: positivo a la derecha. */
static double elegir_giro(void) {
    RadarLectura l[RADAR_N_ANGULOS];
    int n = radar_lecturas(l, RADAR_N_ANGULOS);

    /* Sin eco = nada dentro de los 4 m del HC-SR04: lo mas libre posible. */
    double espacio[RADAR_N_ANGULOS];
    double mejor = -1.0;
    for (int i = 0; i < n; i++) {
        espacio[i] = l[i].distancia_cm > 0.0 ? l[i].distancia_cm : 400.0;
        if (l[i].angulo != RADAR_FRENTE && espacio[i] > mejor) mejor = espacio[i];
    }

    if (mejor < ESPACIO_LIBRE_CM) return 180.0;

    int candidatos[RADAR_N_ANGULOS], nc = 0;
    for (int i = 0; i < n; i++)
        if (l[i].angulo != RADAR_FRENTE && espacio[i] >= mejor - MARGEN_EMPATE_CM)
            candidatos[nc++] = i;

    int elegido = candidatos[rand() % nc];
    /* Servo a 0 = derecha = +90 de brujula; a 180 = izquierda = -90. */
    return (double)(RADAR_FRENTE - l[elegido].angulo);
}

/* Rutina ante un obstaculo: detenerse, retroceder, mirar alrededor con un
   barrido completo del radar y girar hacia el lado mas despejado. */
static void evadir(RobotState *rs) {
    motores_detener();
    if (!esperar_ms(rs, 100)) return;

    motores_retroceder(VEL_RETROCESO);
    int ok = esperar_ms(rs, RETROCESO_MS);
    motores_detener();
    if (!ok) return;

    /* Un barrido que empiece despues de detenerse: las lecturas de antes se
       tomaron desde otra posicion. Toma poco mas de un segundo. */
    uint32_t desde = radar_seq();
    for (int t = 0; t < 3000 && !radar_barrido_completo(desde); t += PASO_MS)
        if (!esperar_ms(rs, PASO_MS)) return;

    double giro = elegir_giro();
    printf("[auto] obstaculo: retrocede y gira %+.0f grados\n", giro);
    girar_grados(rs, giro);
}

/* ══════════════════════════════════════════════════════════
   Hilo de Navegación Autónoma y Lectura de Sensores
══════════════════════════════════════════════════════════ */
static void *autonomous_thread(void *arg) {
    (void)arg;

    OperationMode modo_anterior = MODE_AUTONOMOUS;
    int obstaculo_anterior = 0;

    while (g_running) {
        RobotState *rs = robot_state_get();
        if (!rs) {
            usleep(100000);
            continue;
        }

        // 1. Sensores, velocidad, tiempo de choque y mapa, al estado global
        int obstaculo = actualizar_estado(rs);
        OperationMode modo = modo_actual(rs);

        /* 2. El sonido y la detencion son de la APARICION del obstaculo, en
           cualquier modo: tambien frenan al usuario que maneja a mano. Al
           despejarse el camino solo se apaga el LED. */
        if (obstaculo && !obstaculo_anterior) {
            motores_detener();
            lib_audio_notify(NOTIFY_OBSTACLE);
        }
        obstaculo_anterior = obstaculo;

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
            if (obstaculo) evadir(rs);
            else           motores_avanzar(VEL_CRUCERO);
        }

        // Lazo de decisión a 10 Hz; el radar barre a su ritmo en librobot
        usleep(100000);
    }

    motores_detener();
    return NULL;
}

// Handler de la signal
static struct MHD_Daemon *g_daemon = NULL;
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

    if (lib_audio_init(NULL) < 0)  { fprintf(stderr, "[main] audio init failed\n");}
    if (auth_init()        < 0)  { fprintf(stderr, "[main] autorizacion inicial fallo\n");}

    // Notificacion de encendido
    lib_audio_notify(NOTIFY_STARTUP); 

    // Iniciar en modo autonomo
    {
        RobotState *rs = robot_state_get();
        if (rs) {
            pthread_mutex_lock(&rs->lock);
            rs->mode = MODE_AUTONOMOUS;
            pthread_mutex_unlock(&rs->lock);
        }
        printf("[main] Modo inicial: AUTONOMO\n");
        lib_audio_notify(NOTIFY_AUTONOMOUS);
    }

    lib_leds_sync_from_state();
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    // Inicializar Threads
    pthread_t tid_uptime, tid_watchdog;
    pthread_create(&tid_uptime,   NULL, uptime_thread,   NULL);
    pthread_create(&tid_watchdog, NULL, watchdog_thread, NULL);

    // --- INICIAR HILO DE NAVEGACIÓN AUTÓNOMA ---
    pthread_t auto_tid;
    pthread_create(&auto_tid, NULL, autonomous_thread, NULL);

    g_daemon = MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION | MHD_USE_INTERNAL_POLLING_THREAD,
        SERVER_PORT,
        NULL, NULL,
        handle_request, NULL,
        MHD_OPTION_NOTIFY_COMPLETED, request_done, NULL,
        MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)30,
        MHD_OPTION_END
    );

    if (!g_daemon) {
        fprintf(stderr, "[main] fallo al iniciar el daemon en puerto %d\n", SERVER_PORT);
        g_running = 0;
        pthread_join(tid_uptime, NULL);
        pthread_join(tid_watchdog, NULL);
        pthread_join(auto_tid, NULL);
        auth_destroy();
        lib_audio_destroy();
        robot_shutdown();
        robot_state_destroy();
        lib_leds_destroy();
        return 1;
    }

    printf("[server] Servidor escuchando en puerto %d\n\n", SERVER_PORT);
    printf("  Dev:               http://localhost:%d\n", SERVER_PORT);
    printf("  Red local:         http://<hostname -I>:%d\n",   SERVER_PORT);

    while (g_running) sleep(1);

    // --- SECUENCIA DE APAGADO ---
    g_running = 0;
    pthread_join(tid_uptime,   NULL);
    pthread_join(tid_watchdog, NULL);
    pthread_join(auto_tid,     NULL); // Esperar a que el hilo autónomo termine
    
    auth_destroy();
    lib_audio_destroy();
    robot_shutdown(); // Detener motores, apagar LEDs y cerrar la sesion de pigpiod
    robot_state_destroy();
    lib_leds_destroy(); // Apagar LEDs físicos
    
    printf("[server] Close.\n");
    return 0;
}
