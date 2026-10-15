/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *

 */

/* Prototipos de los manejadores de cada endpoint. */
#include "api.h"
/* Sesiones, validacion de credenciales y proteccion por IP. */
#include "auth.h"
/* Estado global del robot. */
#include "robot_state.h"
/* Reproduccion, volumen, playlist y avisos sonoros. */
#include "lib_audio.h"
/* LEDs indicadores. */
#include "lib_leds.h"
/* Motores, para el control manual. */
#include "lib_motors.h"

/* printf y snprintf. */
#include <stdio.h>
/* malloc, free, atoi, strtod y strtol. */
#include <stdlib.h>
/* strlen, strstr, strchr y strcmp. */
#include <string.h>
/* struct sockaddr y AF_INET, para leer la direccion del cliente. */
#include <sys/socket.h>
/* struct sockaddr_in, la direccion IPv4. */
#include <netinet/in.h>

/* ═══════════════════════════════════════════════════════════
   Definicion de helpers internos
═══════════════════════════════════════════════════════════ */

/* Encola una respuesta JSON con el codigo HTTP indicado. Devuelve lo que
   devuelve libmicrohttpd: MHD_YES si la encolo. */
static enum MHD_Result send_json(struct MHD_Connection *conn,
                                  unsigned int status,
                                  const char *body)
{
    /* Crea la respuesta. MUST_COPY: libmicrohttpd copia el texto, asi el
       buffer del llamador puede ser local. */
    struct MHD_Response *r =
        MHD_create_response_from_buffer(strlen(body), (void *)body,
                                        MHD_RESPMEM_MUST_COPY);
    /* Tipo del contenido, y que el navegador no guarde la respuesta en
       cache. */
    MHD_add_response_header(r, "Content-Type",  "application/json");
    MHD_add_response_header(r, "Cache-Control", "no-cache");
    /* Encola la respuesta y suelta la referencia propia. */
    enum MHD_Result ret = MHD_queue_response(conn, status, r);
    MHD_destroy_response(r);
    return ret;
}

/* ── Extractores de campos JSON mínimos ── */

// Strings
/* Busca "key" en el JSON y copia en out su valor de texto. Devuelve 1 si
   lo encontro y 0 si no. No es un analizador completo: alcanza para los
   cuerpos planos que manda el panel. */
static int json_str(const char *json, const char *key,
                    char *out, size_t out_len)
{
    /* La clave tal como aparece en el JSON, entre comillas. */
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    /* Busca la clave. */
    const char *p = strstr(json, needle);
    if (!p) return 0;
    /* Salta la clave, los espacios y los dos puntos. */
    p += strlen(needle);
    while (*p == ' ' || *p == ':' || *p == ' ') p++;
    /* El valor tiene que empezar con comillas. */
    if (*p != '"') return 0;
    p++;
    /* Copia hasta la comilla de cierre o hasta llenar out. */
    size_t i = 0;
    while (*p && *p != '"' && i < out_len - 1) out[i++] = *p++;
    out[i] = '\0';
    return 1;
}

// Enteros
/* Busca "key" y convierte su valor a entero. Devuelve 1 si lo encontro. */
static int json_int(const char *json, const char *key, int *out)
{
    /* La clave entre comillas y un buffer para los digitos. */
    char needle[128], buf[32];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    /* Salta la clave, los espacios y los dos puntos. */
    p += strlen(needle);
    while (*p == ' ' || *p == ':') p++;
    /* Acepta el numero escrito entre comillas. */
    if (*p == '"') p++;
    /* Copia el signo y los digitos. */
    size_t i = 0;
    while (*p && ((*p >= '0' && *p <= '9') || *p == '-') && i < sizeof(buf)-1)
        buf[i++] = *p++;
    buf[i] = '\0';
    /* Sin digitos no hay numero. */
    if (i == 0) return 0;
    /* Convierte el texto a entero. */
    *out = atoi(buf);
    return 1;
}

// Numeros con decimales
/* Busca "key" y convierte su valor a double. Devuelve 1 si lo encontro. */
static int json_num(const char *json, const char *key, double *out)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '"') p++;
    /* strtod convierte y deja en fin el primer caracter que no uso. */
    char *fin;
    double v = strtod(p, &fin);
    /* Si no avanzo, ahi no habia un numero. */
    if (fin == p) return 0;
    *out = v;
    return 1;
}

// Arreglo de enteros: "key":[1,2,3]. Devuelve cuantos leyo, -1 si no esta.
static int json_int_array(const char *json, const char *key, int *out, int max)
{
    /* La clave entre comillas. */
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    /* Busca el corchete de apertura del arreglo. */
    p = strchr(p + strlen(needle), '[');
    if (!p) return -1;
    p++;
    /* Cantidad de enteros leidos. */
    int n = 0;
    /* Recorre hasta el corchete de cierre. */
    while (*p && *p != ']') {
        /* Intenta leer un entero en la posicion actual. */
        char *fin;
        long v = strtol(p, &fin, 10);
        if (fin == p) { p++; continue; }   /* comas y espacios */
        /* Mas elementos de los que caben: error. */
        if (n >= max) return -1;
        /* Guarda el entero y sigue despues de el. */
        out[n++] = (int)v;
        p = fin;
    }
    /* El arreglo tiene que cerrar con corchete. */
    return *p == ']' ? n : -1;
}

/* ═══════════════════════════════════════════════════════════
   POST /api/login
═══════════════════════════════════════════════════════════ */
/* Valida las credenciales y, si son correctas, abre una sesion y manda su
   token en una cookie. */
enum MHD_Result api_login(struct MHD_Connection *conn,
                           const char *body, size_t len)
{
    /* El largo no se usa: el cuerpo llega terminado en cero. */
    (void)len;
    /* Sin cuerpo no hay credenciales. */
    if (!body || !*body)
        return send_json(conn, MHD_HTTP_BAD_REQUEST,
                         "{\"error\":\"Empty body\"}");

    /* Extrae usuario y contrasena del JSON. */
    char username[64] = {0}, password[128] = {0};
    json_str(body, "username", username, sizeof(username));
    json_str(body, "password", password, sizeof(password));

    /* ── Protección fuerza bruta: obtener IP del cliente ── */
    /* Direccion del cliente, segun libmicrohttpd. */
    const union MHD_ConnectionInfo *ci =
        MHD_get_connection_info(conn, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
    /* Texto de la IP; queda "unknown" si no se puede obtener. */
    char client_ip[46] = "unknown";
    /* Solo se formatea IPv4: los cuatro bytes de la direccion, con puntos. */
    if (ci && ci->client_addr) {
        struct sockaddr *sa = (struct sockaddr *)ci->client_addr;
        if (sa->sa_family == AF_INET) {
            struct sockaddr_in *s4 = (struct sockaddr_in *)sa;
            unsigned char *b = (unsigned char *)&s4->sin_addr;
            snprintf(client_ip, sizeof(client_ip),
                     "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
        }
    }

    /* Una IP bloqueada por demasiados fallos recibe 429 sin evaluar la
       clave. */
    if (!auth_ip_allowed(client_ip))
        return send_json(conn, 429,
                         "{\"error\":\"Demasiados intentos. Intente mas tarde.\"}");

    /* Credenciales incorrectas: se anota el fallo de esa IP y se responde
       401. */
    if (!auth_validate(username, password)) {
        printf("[api] login FALLIDO para '%s' desde %s\n", username, client_ip);
        auth_ip_failed(client_ip);
        return send_json(conn, MHD_HTTP_UNAUTHORIZED,
                         "{\"error\":\"Credenciales invalidas\"}");
    }

    /* ── Crear sesión ── */
    /* Token de la sesion nueva. */
    char token[SESSION_TOKEN_LEN + 1];
    AuthResult res = auth_create_session(username, token);

    /* Sin casillas de sesion libres se responde 503. */
    if (res == AUTH_ERR_FULL)
        return send_json(conn, 503,
                         "{\"error\":\"Servidor lleno. Maximo de usuarios activos alcanzado.\"}");

    /* Login correcto: se limpian los fallos de esa IP. */
    auth_ip_success(client_ip);

    /* Cookie de sesion. HttpOnly impide que JavaScript la lea; Max-Age la
       hace
       vencer junto con la sesion. */
    char cookie[160];
    snprintf(cookie, sizeof(cookie),
             "session=%s; HttpOnly; Path=/; Max-Age=%d",
             token, SESSION_TTL_SECS);

    /* Cuerpo de la respuesta. */
    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"username\":\"%s\"}", username);

    /* La respuesta se arma a mano, y no con send_json, para poder agregar la
       cabecera Set-Cookie. */
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
/* Cierra la sesion del cliente y detiene la musica. */
enum MHD_Result api_logout(struct MHD_Connection *conn)
{
    /* Token de la cookie, si el cliente la mando. */
    const char *cookie =
        MHD_lookup_connection_value(conn, MHD_COOKIE_KIND, "session");
    /* Invalida esa sesion. */
    if (cookie) auth_invalidate(cookie);

    /* Al salir del panel se detiene el audio. */
    lib_audio_stop();

    /* Respuesta fija. PERSISTENT: el texto es una constante y no hace falta
       copiarlo. */
    struct MHD_Response *r =
        MHD_create_response_from_buffer(11, (void *)"{\"ok\":true}",
                                        MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(r, "Content-Type", "application/json");
    /* Una cookie vacia con Max-Age=0 hace que el navegador la borre. */
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
    /* Estado global. */
    RobotState *rs = robot_state_get();
    if (!rs) return send_json(conn, 500, "{\"error\":\"State unavailable\"}");

    /* Leer audio desde lib_audio — fuente de verdad real.
       rs->audio.volume no se actualiza en tiempo real. */
    int   real_volume   = lib_audio_get_volume();
    int   real_status   = (int)lib_audio_get_status();
    int   real_track_id = lib_audio_get_current_id();
    float real_position = lib_audio_get_position();
    int   real_pl_pos   = lib_audio_playlist_pos();
    /* Construir grid JSON y calcular estadisticas */
    /* Tamano: cada celda ocupa a lo sumo un digito y una coma, mas los
       corchetes de cada fila. */
    char grid_json[MAP_ROWS * MAP_COLS * 3 + 128];
    /* gn: bytes ya escritos en grid_json. */
    int gn = 0;
    /* Contadores de celdas visitadas y con obstaculo, e indices de fila y
       columna. */
    int visited = 0;
    int obstacles = 0;
    int r, c;

    /* Desde aqui se lee el estado bajo su candado. */
    pthread_mutex_lock(&rs->lock);
    /* Serializar grilla */
    gn += snprintf(grid_json + gn, sizeof(grid_json) - gn, "[");

    /* Una fila del arreglo por cada fila del mapa. */
    for (r = 0; r < MAP_ROWS; r++) {
        gn += snprintf(grid_json + gn, sizeof(grid_json) - gn, "[");
        /* Cada celda es su estado (0, 1 o 2), separada por comas. */
        for (c = 0; c < MAP_COLS; c++) {
            gn += snprintf(grid_json + gn, sizeof(grid_json) - gn,
                        "%d%s", rs->map.grid[r][c],
                        c < MAP_COLS - 1 ? "," : "");
        }
        /* Cierra la fila; lleva coma si no es la ultima. */
        gn += snprintf(grid_json + gn, sizeof(grid_json) - gn,
                    "]%s", r < MAP_ROWS - 1 ? "," : "");
    }
    /* Cierra el arreglo de filas. */
    snprintf(grid_json + gn, sizeof(grid_json) - gn, "]");

    /* Estadisticas */
    /* Recorre el mapa otra vez para contar. */
    for (r = 0; r < MAP_ROWS; r++)
        for (c = 0; c < MAP_COLS; c++) {
            if (rs->map.grid[r][c] == CELL_VISITED)  visited++;
            if (rs->map.grid[r][c] == CELL_OBSTACLE) obstacles++;
        }

    /* Radar: la ultima lectura de cada angulo del barrido. Distancia -1 = sin
       eco; edad -1 = ese angulo todavia no se midio. */
    /* Buffer para el arreglo de lecturas del radar. */
    char radar_json[RADAR_LECTURAS_MAX * 48 + 64];
    /* rn: bytes ya escritos en radar_json. */
    int rn = snprintf(radar_json, sizeof(radar_json), "[");
    /* Un objeto por angulo: a = angulo, d = distancia, edad = segundos. */
    for (int i = 0; i < rs->radar.n; i++) {
        const RadarPunto *p = &rs->radar.lecturas[i];
        rn += snprintf(radar_json + rn, sizeof(radar_json) - rn,
                       "%s{\"a\":%d,\"d\":%.1f,\"edad\":%.2f}",
                       i > 0 ? "," : "", p->angulo, p->distancia_cm, p->edad_s);
    }
    /* Cierra el arreglo. */
    snprintf(radar_json + rn, sizeof(radar_json) - rn, "]");

    /* Nombre de cada tipo de ciclo, en el orden del enum CicloTipo. */
    static const char *TIPOS_CICLO[] = { "off", "tiempo", "area" };

    /* Buffer del JSON completo. */
    char buf[12288];
    /* Arma el JSON de una sola vez. Cada grupo de la cadena de formato
       corresponde, en el mismo orden, a un grupo de los argumentos que
       siguen:
       modo y uptime, sensores, radar, movimiento, LEDs, audio, ciclo,
       desnivel
       y mapa. */
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
          "\"radar\":{"
            "\"angulo\":%d,"
            "\"lecturas\":%s"
          "},"
          "\"movimiento\":{"
            "\"velocidad\":%.1f,"
            "\"ttc\":%.2f,"
            "\"imu\":%s,"
            "\"umbral\":%.1f"
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
            "\"volume\":%d,"
            "\"playlist_pos\":%d"
          "},"
          "\"ciclo\":{"
            "\"tipo\":\"%s\","
            "\"meta\":%.2f,"
            "\"segundos\":%.1f,"
            "\"area\":%.2f,"
            "\"progreso\":%.3f,"
            "\"completo\":%s,"
            "\"completados\":%u"
          "},"
          "\"caida\":{"
            "\"izq\":%s,"
            "\"der\":%s,"
            "\"eventos\":%u"
          "},"
          "\"map\":{"
            "\"celda_cm\":%d,"
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
        rs->radar.angulo_servo, radar_json,
        rs->movimiento.velocidad_cm_s, rs->movimiento.ttc_s,
        rs->movimiento.imu ? "true" : "false",
        rs->movimiento.umbral_obstaculo_cm,
        rs->leds.power     ? "true" : "false",
        rs->leds.autonomous? "true" : "false",
        rs->leds.manual    ? "true" : "false",
        rs->leds.obstacle  ? "true" : "false",
        real_status, real_track_id,
        real_position, real_volume, real_pl_pos,
        TIPOS_CICLO[rs->ciclo.tipo], rs->ciclo.meta, rs->ciclo.segundos,
        rs->ciclo.area_m2, robot_ciclo_progreso(rs),
        rs->ciclo.completo ? "true" : "false", rs->ciclo.completados,
        rs->caida.izq ? "true" : "false", rs->caida.der ? "true" : "false",
        rs->caida.eventos,
        MAP_CELDA_CM,
        rs->map.robot_x, rs->map.robot_y, rs->map.robot_heading,
        visited, obstacles, grid_json
    );

    /* Fin de la lectura del estado. */
    pthread_mutex_unlock(&rs->lock);
    /* Responde 200 con el JSON. */
    return send_json(conn, MHD_HTTP_OK, buf);
}

/* ═══════════════════════════════════════════════════════════
   Metodo POST para seleccion de modo en /api/mode 
═══════════════════════════════════════════════════════════ */
/* Cambia entre modo manual y autonomo, y actualiza los LEDs y el aviso
   sonoro. */
enum MHD_Result api_set_mode(struct MHD_Connection *conn,
                              const char *body, size_t len)
{
    (void)len;
    /* Modo pedido: "manual", o cualquier otro texto, que se toma como
       autonomo. */
    char mode_str[32] = {0};
    json_str(body, "mode", mode_str, sizeof(mode_str));

    /* Estado global. */
    RobotState *rs = robot_state_get();
    if (!rs) return send_json(conn, 500, "{\"error\":\"State unavailable\"}");

    pthread_mutex_lock(&rs->lock);

    // FALTA AGREGAR EL lib_audio_notify(NOTIFY_OBSTACLE) nada mas
    // pero todavia no esta definido eso de los obstaculos
    //    lib_leds_set(LED_OBSTACLE, 1);  
    //    lib_leds_set(LED_OBSTACLE, 0);  

    if (strcmp(mode_str, "manual") == 0) {
        /* Cambia el modo y deja anotado como deben quedar los LEDs. */
        rs->mode            = MODE_MANUAL;
        rs->leds.autonomous = 0;
        rs->leds.manual     = 1;
        printf("[api] mode -> MANUAL\n");
        /* Se suelta el candado antes del aviso: lib_audio_notify() bloquea
           mientras suena. */
        pthread_mutex_unlock(&rs->lock);          
        lib_audio_notify(NOTIFY_MANUAL);
        /* Aplica los LEDs fisicos. */
        lib_leds_set(LED_MANUAL,     1);
        lib_leds_set(LED_AUTONOMOUS, 0);          
        // Funcionalidad de modo MANUAL
    } else {
        /* Cambia el modo y deja anotado como deben quedar los LEDs. */
        rs->mode            = MODE_AUTONOMOUS;
        rs->leds.autonomous = 1;
        rs->leds.manual     = 0;
        /* Pasar a autonomo desde el panel empieza un ciclo de limpieza nuevo,
           tambien si el anterior ya habia terminado. */
        robot_ciclo_reiniciar(rs);
        printf("[api] mode -> AUTONOMOUS\n");
        /* Se suelta el candado antes del aviso, que bloquea mientras suena. */
        pthread_mutex_unlock(&rs->lock);          
        lib_audio_notify(NOTIFY_AUTONOMOUS);
        /* Aplica los LEDs fisicos. */
        lib_leds_set(LED_AUTONOMOUS, 1);
        lib_leds_set(LED_MANUAL,     0);      
    }

    /* Responde 200. */
    return send_json(conn, MHD_HTTP_OK, "{\"ok\":true}");
}

/* ═══════════════════════════════════════════════════════════
   Metodo POST para la posicion /api/move 
   Tambien cuenta con velocidad 0-100
   Solo se activa con el modo manual
═══════════════════════════════════════════════════════════ */
/* Mueve el robot en modo manual, segun la direccion y la velocidad pedidas. */
enum MHD_Result api_move(struct MHD_Connection *conn,
                          const char *body, size_t len)
{
    (void)len;
    /* Estado global. */
    RobotState *rs = robot_state_get();
    if (!rs) return send_json(conn, 500, "{\"error\":\"State unavailable\"}");

    /* Copia bajo el candado el modo y si algun sensor de desnivel no ve
       piso. */
    pthread_mutex_lock(&rs->lock);
    OperationMode current_mode = rs->mode;
    int sin_piso = rs->caida.izq || rs->caida.der;
    pthread_mutex_unlock(&rs->lock);

    /* Fuera del modo manual el movimiento esta prohibido: 403. */
    if (current_mode != MODE_MANUAL)
        return send_json(conn, MHD_HTTP_FORBIDDEN,
                         "{\"error\":\"Not in manual mode\"}");

    /* Direccion y velocidad pedidas; sin velocidad se usa 50 %. */
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
        /* Con un desnivel al frente no se avanza, tampoco a mano: se puede
           retroceder o girar para salir del borde. */
        if (sin_piso) {
            /* Frena y responde 409. */
            motores_detener();
            return send_json(conn, 409, "{\"error\":\"Desnivel al frente\"}");
        }
        /* Avanza. */
        motores_avanzar(pwm_speed);
    } else if (strcmp(direction, "backward") == 0 || strcmp(direction, "down") == 0) {
        /* Retrocede. */
        motores_retroceder(pwm_speed);
    } else if (strcmp(direction, "left") == 0) {
        /* Gira a la izquierda. */
        motores_girar_izquierda(pwm_speed);
    } else if (strcmp(direction, "right") == 0) {
        /* Gira a la derecha. */
        motores_girar_derecha(pwm_speed);
    } else if (strcmp(direction, "stop") == 0) {
        /* Frena. */
        motores_detener();
    } else {
        /* Cualquier otra direccion es un error del cliente: 400. */
        return send_json(conn, MHD_HTTP_BAD_REQUEST, "{\"error\":\"Direccion desconocida\"}");
    }

    return send_json(conn, MHD_HTTP_OK, "{\"ok\":true}");
}

/* ═══════════════════════════════════════════════════════════
   Metodo GET para /api/audio/list
═══════════════════════════════════════════════════════════ */

/* Devuelve la lista de pistas con su id, nombre y duracion. */
enum MHD_Result api_audio_list(struct MHD_Connection *conn)
{
    /* Copia la lista de pistas de la biblioteca. */
    LibAudioTrack tracks[LIB_AUDIO_TRACKS_MAX];
    int count = lib_audio_get_tracks(tracks, LIB_AUDIO_TRACKS_MAX);
 
    /* Sin pistas se responde un arreglo vacio. */
    if (count <= 0)
        return send_json(conn, MHD_HTTP_OK, "{\"tracks\":[]}");
 
    /* Buffer a medida: por pista, el nombre mas lo que ocupan el id, la
       duracion y la sintaxis. */
    size_t bufsz = (size_t)count * (LIB_AUDIO_NAME_MAX + 64) + 32;
    char  *buf   = malloc(bufsz);
    /* Sin memoria no se puede responder. */
    if (!buf) return MHD_NO;
 
    /* Abre el objeto y el arreglo; n lleva los bytes escritos. */
    int n = snprintf(buf, bufsz, "{\"tracks\":[");
    /* Un objeto por pista, con la duracion en minutos:segundos. */
    for (int i = 0; i < count; i++) {
        LibAudioTrack *t = &tracks[i];
        n += snprintf(buf + n, bufsz - (size_t)n,
            "%s{\"id\":%d,\"name\":\"%s\",\"duration\":\"%d:%02d\"}",
            i > 0 ? "," : "",
            t->id, t->filename,
            t->duration_secs / 60, t->duration_secs % 60);
    }
    /* Cierra el arreglo y el objeto. */
    snprintf(buf + n, bufsz - (size_t)n, "]}");
 
    /* Responde y libera el buffer; send_json ya copio el texto. */
    enum MHD_Result ret = send_json(conn, MHD_HTTP_OK, buf);
    free(buf);
    return ret;
}

/* ═══════════════════════════════════════════════════════════
   Metodo POST para /api/audio/control
   Controlar musica, pausa, play, siguiente, etc
═══════════════════════════════════════════════════════════ */

/* Ejecuta una accion de audio: play, play_playlist, seek, pause, resume o
   stop. */
enum MHD_Result api_audio_control(struct MHD_Connection *conn,
                                   const char *body, size_t len)
{
    (void)len;
    /* Accion pedida e id de pista, que solo usa play. */
    char action[32] = {0};
    int  track_id   = -1;
    json_str(body, "action",   action,   sizeof(action));
    json_int(body, "track_id", &track_id);
 
    if (strcmp(action, "play") == 0) {
        // Funcionalidad de Play
        /* Pide reproducir la pista suelta; si el id no existe, 400. */
        if (lib_audio_play(track_id) < 0)
            return send_json(conn, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"Pista no encontrada\"}");
        printf("[api] audio -> PLAY  track_id=%d\n", track_id);
 
    } else if (strcmp(action, "play_playlist") == 0) {
        /* Reproduce la playlist desde la posicion index; si esta vacia, 400. */
        int index = 0;
        json_int(body, "index", &index);
        if (lib_audio_play_playlist(index) < 0)
            return send_json(conn, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"La playlist esta vacia\"}");
        printf("[api] audio -> PLAYLIST desde %d\n", index);

    } else if (strcmp(action, "seek") == 0) {
        // Salto desde la barra de progreso del panel
        /* Posicion pedida, en segundos. */
        double pos = -1.0;
        /* Tiene que venir y no ser negativa. */
        if (!json_num(body, "position", &pos) || pos < 0.0)
            return send_json(conn, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"position debe ser >= 0\"}");
        /* Sin pista cargada no hay donde saltar: 409. */
        if (lib_audio_seek((float)pos) < 0)
            return send_json(conn, 409,
                             "{\"error\":\"No hay una pista sonando\"}");
        printf("[api] audio -> SEEK %.1fs\n", pos);

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
        /* Accion desconocida: 400. */
        return send_json(conn, MHD_HTTP_BAD_REQUEST,
                         "{\"error\":\"Accion desconocida\"}");
    }
 
    /* Responde 200. */
    return send_json(conn, MHD_HTTP_OK, "{\"ok\":true}");
}

/* ═══════════════════════════════════════════════════════════
    Metodo POST para /api/audio/volume
    Control de volumen 0-100
═══════════════════════════════════════════════════════════ */
/* Fija el volumen de la musica. */
enum MHD_Result api_audio_volume(struct MHD_Connection *conn,
                                  const char *body, size_t len)
{
    (void)len;
    // Volumen por defecto
    int vol = 50;
    json_int(body, "volume", &vol);
    /* Limita el valor a 0-100. */
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

/* ═══════════════════════════════════════════════════════════
    GET /api/audio/playlist
    Ids de la playlist persistente, en orden
═══════════════════════════════════════════════════════════ */
/* Devuelve los ids de la playlist, en orden, y la posicion que suena. */
enum MHD_Result api_audio_playlist_get(struct MHD_Connection *conn)
{
    /* Copia la playlist de la biblioteca. */
    int ids[LIB_AUDIO_TRACKS_MAX];
    int n = lib_audio_playlist_get(ids, LIB_AUDIO_TRACKS_MAX);

    /* Buffer para el JSON; k lleva los bytes escritos. */
    char buf[LIB_AUDIO_TRACKS_MAX * 8 + 64];
    int k = snprintf(buf, sizeof(buf), "{\"ids\":[");
    /* Los ids, separados por comas. */
    for (int i = 0; i < n; i++)
        k += snprintf(buf + k, sizeof(buf) - (size_t)k, "%s%d", i ? "," : "", ids[i]);
    /* Cierra el arreglo y agrega la posicion que suena (-1 si no es la
       playlist). */
    snprintf(buf + k, sizeof(buf) - (size_t)k, "],\"pos\":%d}", lib_audio_playlist_pos());
    return send_json(conn, MHD_HTTP_OK, buf);
}

/* ═══════════════════════════════════════════════════════════
    POST /api/audio/playlist   {"ids":[3,1,2]}
    Reemplaza la playlist y la guarda en la SD
═══════════════════════════════════════════════════════════ */
/* Reemplaza la playlist por los ids recibidos. */
enum MHD_Result api_audio_playlist_set(struct MHD_Connection *conn,
                                        const char *body, size_t len)
{
    (void)len;
    /* Lee el arreglo "ids" del cuerpo. */
    int ids[LIB_AUDIO_TRACKS_MAX];
    int n = json_int_array(body ? body : "", "ids", ids, LIB_AUDIO_TRACKS_MAX);
    /* Sin un arreglo valido: 400. */
    if (n < 0)
        return send_json(conn, MHD_HTTP_BAD_REQUEST,
                         "{\"error\":\"Se esperaba {\\\"ids\\\":[...]}\"}");
    /* La biblioteca valida los ids y guarda; si falla, 400. */
    if (lib_audio_playlist_set(ids, n) < 0)
        return send_json(conn, MHD_HTTP_BAD_REQUEST,
                         "{\"error\":\"Pista inexistente o no se pudo guardar\"}");
    return send_json(conn, MHD_HTTP_OK, "{\"ok\":true}");
}

/* ═══════════════════════════════════════════════════════════
    POST /api/ciclo
    {"tipo":"tiempo","minutos":5}   termina tras 5 min en autonomo
    {"tipo":"area","m2":4}          termina tras recorrer 4 m2
    {"tipo":"off"}                  sin limite
    {"accion":"nuevo"}              empieza otro ciclo con la misma meta
    Configurar la meta tambien empieza un ciclo nuevo.
═══════════════════════════════════════════════════════════ */
/* Configura la meta del ciclo de limpieza o empieza un ciclo nuevo. */
enum MHD_Result api_ciclo(struct MHD_Connection *conn,
                           const char *body, size_t len)
{
    (void)len;
    /* Un cuerpo ausente se trata como vacio. */
    if (!body) body = "";
    /* Estado global. */
    RobotState *rs = robot_state_get();
    if (!rs) return send_json(conn, 500, "{\"error\":\"State unavailable\"}");

    /* Tipo de ciclo y accion pedidos. */
    char tipo[16] = {0}, accion[16] = {0};
    json_str(body, "tipo",   tipo,   sizeof(tipo));
    json_str(body, "accion", accion, sizeof(accion));

    /* Tipo y meta nuevos, que se aplican al final. */
    CicloTipo nuevo_tipo;
    double    meta = 0.0;

    /* Accion "nuevo": se reinician los contadores con la meta que ya habia. */
    if (strcmp(accion, "nuevo") == 0) {
        pthread_mutex_lock(&rs->lock);
        robot_ciclo_reiniciar(rs);
        pthread_mutex_unlock(&rs->lock);
        printf("[api] ciclo -> nuevo\n");
        return send_json(conn, MHD_HTTP_OK, "{\"ok\":true}");
    }

    if (strcmp(tipo, "off") == 0) {
        /* Sin limite. */
        nuevo_tipo = CICLO_OFF;
    } else if (strcmp(tipo, "tiempo") == 0) {
        /* Por tiempo: minutos entre 0 y 600, que se guardan en segundos. */
        double min = 0.0;
        if (!json_num(body, "minutos", &min) || min <= 0.0 || min > 600.0)
            return send_json(conn, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"minutos debe estar entre 0 y 600\"}");
        nuevo_tipo = CICLO_TIEMPO;
        meta = min * 60.0;
    } else if (strcmp(tipo, "area") == 0) {
        /* Por area: metros cuadrados entre 0 y 1000. */
        double m2 = 0.0;
        if (!json_num(body, "m2", &m2) || m2 <= 0.0 || m2 > 1000.0)
            return send_json(conn, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"m2 debe estar entre 0 y 1000\"}");
        nuevo_tipo = CICLO_AREA;
        meta = m2;
    } else {
        /* Cualquier otro tipo: 400. */
        return send_json(conn, MHD_HTTP_BAD_REQUEST,
                         "{\"error\":\"tipo debe ser off, tiempo o area\"}");
    }

    /* Guarda el tipo y la meta, y empieza un ciclo nuevo. */
    pthread_mutex_lock(&rs->lock);
    rs->ciclo.tipo = nuevo_tipo;
    rs->ciclo.meta = meta;
    robot_ciclo_reiniciar(rs);
    pthread_mutex_unlock(&rs->lock);

    printf("[api] ciclo -> %s, meta %.1f %s\n", tipo, nuevo_tipo == CICLO_TIEMPO ? meta / 60.0 : meta,
           nuevo_tipo == CICLO_TIEMPO ? "min" : nuevo_tipo == CICLO_AREA ? "m2" : "");
    return send_json(conn, MHD_HTTP_OK, "{\"ok\":true}");
}

/* ═══════════════════════════════════════════════════════════
    POST /api/umbral
    {"cm":10}   hay obstaculo cuando algo queda a menos de 10 cm al frente
═══════════════════════════════════════════════════════════ */
/* Cambia el umbral de obstaculo. Rige desde la siguiente vuelta del hilo de
   navegacion y vuelve al valor por defecto al reiniciar el servidor. */
enum MHD_Result api_umbral(struct MHD_Connection *conn,
                           const char *body, size_t len)
{
    (void)len;
    /* Un cuerpo ausente se trata como vacio. */
    if (!body) body = "";
    /* Estado global. */
    RobotState *rs = robot_state_get();
    if (!rs) return send_json(conn, 500, "{\"error\":\"State unavailable\"}");

    /* Distancia pedida, dentro de los limites. */
    double cm = 0.0;
    if (!json_num(body, "cm", &cm) ||
        cm < UMBRAL_OBSTACULO_MIN_CM || cm > UMBRAL_OBSTACULO_MAX_CM) {
        char err[96];
        snprintf(err, sizeof(err), "{\"error\":\"cm debe estar entre %.0f y %.0f\"}",
                 UMBRAL_OBSTACULO_MIN_CM, UMBRAL_OBSTACULO_MAX_CM);
        return send_json(conn, MHD_HTTP_BAD_REQUEST, err);
    }

    pthread_mutex_lock(&rs->lock);
    rs->movimiento.umbral_obstaculo_cm = (float)cm;
    pthread_mutex_unlock(&rs->lock);

    printf("[api] umbral de obstaculo -> %.1f cm\n", cm);
    return send_json(conn, MHD_HTTP_OK, "{\"ok\":true}");
}
