/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#include "api.h"
#include "auth.h"
#include "robot_state.h"
#include "lib_audio.h"
#include "lib_leds.h"
#include "lib_motors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* ═══════════════════════════════════════════════════════════
   Definicion de helpers internos
═══════════════════════════════════════════════════════════ */

static enum MHD_Result send_json(struct MHD_Connection *conn,
                                  unsigned int status,
                                  const char *body)
{
    struct MHD_Response *r =
        MHD_create_response_from_buffer(strlen(body), (void *)body,
                                        MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(r, "Content-Type",  "application/json");
    MHD_add_response_header(r, "Cache-Control", "no-cache");
    enum MHD_Result ret = MHD_queue_response(conn, status, r);
    MHD_destroy_response(r);
    return ret;
}

/* ── Extractores de campos JSON mínimos ── */

// Strings
static int json_str(const char *json, const char *key,
                    char *out, size_t out_len)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ' || *p == ':' || *p == ' ') p++;
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_len - 1) out[i++] = *p++;
    out[i] = '\0';
    return 1;
}

// Enteros
static int json_int(const char *json, const char *key, int *out)
{
    char needle[128], buf[32];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '"') p++;
    size_t i = 0;
    while (*p && ((*p >= '0' && *p <= '9') || *p == '-') && i < sizeof(buf)-1)
        buf[i++] = *p++;
    buf[i] = '\0';
    if (i == 0) return 0;
    *out = atoi(buf);
    return 1;
}

/* ═══════════════════════════════════════════════════════════
   POST /api/login
═══════════════════════════════════════════════════════════ */
enum MHD_Result api_login(struct MHD_Connection *conn,
                           const char *body, size_t len)
{
    (void)len;
    if (!body || !*body)
        return send_json(conn, MHD_HTTP_BAD_REQUEST,
                         "{\"error\":\"Empty body\"}");

    char username[64] = {0}, password[128] = {0};
    json_str(body, "username", username, sizeof(username));
    json_str(body, "password", password, sizeof(password));

    /* ── Protección fuerza bruta: obtener IP del cliente ── */
    const union MHD_ConnectionInfo *ci =
        MHD_get_connection_info(conn, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
    char client_ip[46] = "unknown";
    if (ci && ci->client_addr) {
        struct sockaddr *sa = (struct sockaddr *)ci->client_addr;
        if (sa->sa_family == AF_INET) {
            struct sockaddr_in *s4 = (struct sockaddr_in *)sa;
            unsigned char *b = (unsigned char *)&s4->sin_addr;
            snprintf(client_ip, sizeof(client_ip),
                     "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
        }
    }

    if (!auth_ip_allowed(client_ip))
        return send_json(conn, 429,
                         "{\"error\":\"Demasiados intentos. Intente mas tarde.\"}");

    if (!auth_validate(username, password)) {
        printf("[api] login FALLIDO para '%s' desde %s\n", username, client_ip);
        auth_ip_failed(client_ip);
        return send_json(conn, MHD_HTTP_UNAUTHORIZED,
                         "{\"error\":\"Credenciales invalidas\"}");
    }

    /* ── Crear sesión ── */
    char token[SESSION_TOKEN_LEN + 1];
    AuthResult res = auth_create_session(username, token);

    if (res == AUTH_ERR_FULL)
        return send_json(conn, 503,
                         "{\"error\":\"Servidor lleno. Maximo de usuarios activos alcanzado.\"}");

    auth_ip_success(client_ip);

    char cookie[160];
    snprintf(cookie, sizeof(cookie),
             "session=%s; HttpOnly; Path=/; Max-Age=%d",
             token, SESSION_TTL_SECS);

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"username\":\"%s\"}", username);

    struct MHD_Response *r =
        MHD_create_response_from_buffer(strlen(resp), (void *)resp,
                                        MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(r, "Content-Type",  "application/json");
    MHD_add_response_header(r, "Set-Cookie",    cookie);
    MHD_add_response_header(r, "Cache-Control", "no-cache");
    enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, r);
    MHD_destroy_response(r);
    return ret;
}

/* ═══════════════════════════════════════════════════════════
   POST /api/logout
═══════════════════════════════════════════════════════════ */
enum MHD_Result api_logout(struct MHD_Connection *conn)
{
    const char *cookie =
        MHD_lookup_connection_value(conn, MHD_COOKIE_KIND, "session");
    if (cookie) auth_invalidate(cookie);

    lib_audio_stop();

    struct MHD_Response *r =
        MHD_create_response_from_buffer(11, (void *)"{\"ok\":true}",
                                        MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(r, "Content-Type", "application/json");
    MHD_add_response_header(r, "Set-Cookie",
                            "session=; Max-Age=0; Path=/");
    enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, r);
    MHD_destroy_response(r);
    return ret;
}

/* ═══════════════════════════════════════════════════════════
   Metodo GET para /api/status
   Retorna el estado actual del robot en formato JSON
═══════════════════════════════════════════════════════════ */
enum MHD_Result api_status(struct MHD_Connection *conn)
{
    RobotState *rs = robot_state_get();
    if (!rs) return send_json(conn, 500, "{\"error\":\"State unavailable\"}");

    /* Leer audio desde lib_audio — fuente de verdad real.
       rs->audio.volume no se actualiza en tiempo real. */
    int   real_volume   = lib_audio_get_volume();
    int   real_status   = (int)lib_audio_get_status();
    int   real_track_id = lib_audio_get_current_id();
    float real_position = lib_audio_get_position();
    /* Construir grid JSON y calcular estadisticas */
    char grid_json[MAP_ROWS * MAP_COLS * 3 + 128];
    int gn = 0;
    int visited = 0;
    int obstacles = 0;
    int r, c;

    pthread_mutex_lock(&rs->lock);
    /* Serializar grilla */
    gn += snprintf(grid_json + gn, sizeof(grid_json) - gn, "[");

    for (r = 0; r < MAP_ROWS; r++) {
        gn += snprintf(grid_json + gn, sizeof(grid_json) - gn, "[");
        for (c = 0; c < MAP_COLS; c++) {
            gn += snprintf(grid_json + gn, sizeof(grid_json) - gn,
                        "%d%s", rs->map.grid[r][c],
                        c < MAP_COLS - 1 ? "," : "");
        }
        gn += snprintf(grid_json + gn, sizeof(grid_json) - gn,
                    "]%s", r < MAP_ROWS - 1 ? "," : "");
    }
    snprintf(grid_json + gn, sizeof(grid_json) - gn, "]");

    /* Estadisticas */
    for (r = 0; r < MAP_ROWS; r++)
        for (c = 0; c < MAP_COLS; c++) {
            if (rs->map.grid[r][c] == CELL_VISITED)  visited++;
            if (rs->map.grid[r][c] == CELL_OBSTACLE) obstacles++;
        }

    char buf[8192];
    snprintf(buf, sizeof(buf),
        "{"
          "\"mode\":\"%s\","
          "\"uptime\":%llu,"
          "\"sensors\":{"
            "\"front\":%.1f,"
            "\"back\":%.1f,"
            "\"left\":%.1f,"
            "\"right\":%.1f"
          "},"
          "\"leds\":{"
            "\"power\":%s,"
            "\"autonomous\":%s,"
            "\"manual\":%s,"
            "\"obstacle\":%s"
          "},"
          "\"audio\":{"
            "\"status\":%d,"
            "\"current_track_id\":%d,"
            "\"position\":%.1f,"
            "\"volume\":%d"
          "},"
          "\"map\":{"
            "\"robot_x\":%d,"
            "\"robot_y\":%d,"
            "\"robot_heading\":%d,"
            "\"visited\":%d,"
            "\"obstacles\":%d,"
            "\"grid\":%s"
          "}"
        "}",
        rs->mode == MODE_AUTONOMOUS ? "autonomous" : "manual",
        (unsigned long long)rs->uptime_secs,
        rs->sensors.front_cm, rs->sensors.back_cm,
        rs->sensors.left_cm,  rs->sensors.right_cm,
        rs->leds.power     ? "true" : "false",
        rs->leds.autonomous? "true" : "false",
        rs->leds.manual    ? "true" : "false",
        rs->leds.obstacle  ? "true" : "false",
        real_status, real_track_id,
        real_position, real_volume,
        rs->map.robot_x, rs->map.robot_y, rs->map.robot_heading,
        visited, obstacles, grid_json
    );

    pthread_mutex_unlock(&rs->lock);
    return send_json(conn, MHD_HTTP_OK, buf);
}

/* ═══════════════════════════════════════════════════════════
   Metodo POST para seleccion de modo en /api/mode 
═══════════════════════════════════════════════════════════ */
enum MHD_Result api_set_mode(struct MHD_Connection *conn,
                              const char *body, size_t len)
{
    (void)len;
    char mode_str[32] = {0};
    json_str(body, "mode", mode_str, sizeof(mode_str));

    RobotState *rs = robot_state_get();
    if (!rs) return send_json(conn, 500, "{\"error\":\"State unavailable\"}");

    pthread_mutex_lock(&rs->lock);

    // FALTA AGREGAR EL lib_audio_notify(NOTIFY_OBSTACLE) nada mas
    // pero todavia no esta definido eso de los obstaculos
    //    lib_leds_set(LED_OBSTACLE, 1);  
    //    lib_leds_set(LED_OBSTACLE, 0);  

    if (strcmp(mode_str, "manual") == 0) {
        rs->mode            = MODE_MANUAL;
        rs->leds.autonomous = 0;
        rs->leds.manual     = 1;
        printf("[api] mode -> MANUAL\n");
        pthread_mutex_unlock(&rs->lock);          
        lib_audio_notify(NOTIFY_MANUAL);
        lib_leds_set(LED_MANUAL,     1);
        lib_leds_set(LED_AUTONOMOUS, 0);          
        // Funcionalidad de modo MANUAL
    } else {
        rs->mode            = MODE_AUTONOMOUS;
        rs->leds.autonomous = 1;
        rs->leds.manual     = 0;
        printf("[api] mode -> AUTONOMOUS\n");
        pthread_mutex_unlock(&rs->lock);          
        lib_audio_notify(NOTIFY_AUTONOMOUS);
        lib_leds_set(LED_AUTONOMOUS, 1);
        lib_leds_set(LED_MANUAL,     0);      
    }

    return send_json(conn, MHD_HTTP_OK, "{\"ok\":true}");
}

/* ═══════════════════════════════════════════════════════════
   Metodo POST para la posicion /api/move 
   Tambien cuenta con velocidad 0-100
   Solo se activa con el modo manual
═══════════════════════════════════════════════════════════ */
enum MHD_Result api_move(struct MHD_Connection *conn,
                          const char *body, size_t len)
{
    (void)len;
    RobotState *rs = robot_state_get();
    if (!rs) return send_json(conn, 500, "{\"error\":\"State unavailable\"}");

    pthread_mutex_lock(&rs->lock);
    OperationMode current_mode = rs->mode;
    pthread_mutex_unlock(&rs->lock);

    if (current_mode != MODE_MANUAL)
        return send_json(conn, MHD_HTTP_FORBIDDEN,
                         "{\"error\":\"Not in manual mode\"}");

    char direction[32] = {0};
    int  speed         = 50;
    json_str(body, "direction", direction, sizeof(direction));
    json_int(body, "speed",    &speed);

    printf("[api] move -> direction='%s'  speed=%d%%\n", direction, speed);

    // 1. Escalar la velocidad de la Web (0-100) al rango PWM del hardware (0-255)
    int pwm_speed = (speed * 255) / 100;
    if (pwm_speed > 255) pwm_speed = 255;
    if (pwm_speed < 0)   pwm_speed = 0;

    // 2. Ejecutar comando de hardware utilizando la biblioteca de motores
    if (strcmp(direction, "forward") == 0 || strcmp(direction, "up") == 0) {
        motores_avanzar(pwm_speed);
    } else if (strcmp(direction, "backward") == 0 || strcmp(direction, "down") == 0) {
        motores_retroceder(pwm_speed);
    } else if (strcmp(direction, "left") == 0) {
        motores_girar_izquierda(pwm_speed);
    } else if (strcmp(direction, "right") == 0) {
        motores_girar_derecha(pwm_speed);
    } else if (strcmp(direction, "stop") == 0) {
        motores_detener();
    } else {
        return send_json(conn, MHD_HTTP_BAD_REQUEST, "{\"error\":\"Direccion desconocida\"}");
    }

    return send_json(conn, MHD_HTTP_OK, "{\"ok\":true}");
}

/* ═══════════════════════════════════════════════════════════
   Metodo GET para /api/audio/list
═══════════════════════════════════════════════════════════ */

enum MHD_Result api_audio_list(struct MHD_Connection *conn)
{
    LibAudioTrack tracks[LIB_AUDIO_TRACKS_MAX];
    int count = lib_audio_get_tracks(tracks, LIB_AUDIO_TRACKS_MAX);
 
    if (count <= 0)
        return send_json(conn, MHD_HTTP_OK, "{\"tracks\":[]}");
 
    size_t bufsz = (size_t)count * (LIB_AUDIO_NAME_MAX + 64) + 32;
    char  *buf   = malloc(bufsz);
    if (!buf) return MHD_NO;
 
    int n = snprintf(buf, bufsz, "{\"tracks\":[");
    for (int i = 0; i < count; i++) {
        LibAudioTrack *t = &tracks[i];
        n += snprintf(buf + n, bufsz - (size_t)n,
            "%s{\"id\":%d,\"name\":\"%s\",\"duration\":\"%d:%02d\"}",
            i > 0 ? "," : "",
            t->id, t->filename,
            t->duration_secs / 60, t->duration_secs % 60);
    }
    snprintf(buf + n, bufsz - (size_t)n, "]}");
 
    enum MHD_Result ret = send_json(conn, MHD_HTTP_OK, buf);
    free(buf);
    return ret;
}

/* ═══════════════════════════════════════════════════════════
   Metodo POST para /api/audio/control
   Controlar musica, pausa, play, siguiente, etc
═══════════════════════════════════════════════════════════ */

enum MHD_Result api_audio_control(struct MHD_Connection *conn,
                                   const char *body, size_t len)
{
    (void)len;
    char action[32] = {0};
    int  track_id   = -1;
    json_str(body, "action",   action,   sizeof(action));
    json_int(body, "track_id", &track_id);
 
    if (strcmp(action, "play") == 0) {
        // Funcionalidad de Play
        if (lib_audio_play(track_id) < 0)
            return send_json(conn, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"Pista no encontrada\"}");
        printf("[api] audio -> PLAY  track_id=%d\n", track_id);
 
    } else if (strcmp(action, "pause") == 0) {
        // Funcionalidad de pause
        lib_audio_pause();
        printf("[api] audio -> PAUSE\n");
 
    } else if (strcmp(action, "resume") == 0) {
        // Funcionalidad para continuar con el track
        lib_audio_resume();
        printf("[api] audio -> RESUME\n");
 
    } else if (strcmp(action, "stop") == 0) {
        // Funcionalidad para detener el audio
        lib_audio_stop();
        printf("[api] audio -> STOP\n");
 
    } else {
        return send_json(conn, MHD_HTTP_BAD_REQUEST,
                         "{\"error\":\"Accion desconocida\"}");
    }
 
    return send_json(conn, MHD_HTTP_OK, "{\"ok\":true}");
}

/* ═══════════════════════════════════════════════════════════
    Metodo POST para /api/audio/volume
    Control de volumen 0-100
═══════════════════════════════════════════════════════════ */
enum MHD_Result api_audio_volume(struct MHD_Connection *conn,
                                  const char *body, size_t len)
{
    (void)len;
    // Volumen por defecto
    int vol = 50;
    json_int(body, "volume", &vol);
    if (vol < 0) {
        vol = 0;
    }
    if (vol > 100) {
        vol = 100;
    }
 
    // Utilizar la funcion de la lib de audio
    lib_audio_set_volume(vol);
    printf("[api] audio -> VOLUME %d%%\n", vol);
 
    return send_json(conn, MHD_HTTP_OK, "{\"ok\":true}");
}
