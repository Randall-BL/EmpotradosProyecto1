/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 */

/* Constantes de seguridad, AuthResult y prototipos. */
#include "auth.h"
/* sha256_hex(), para comparar la contrasena contra su resumen. */
#include "sha256.h"

/* printf y snprintf. */
#include <stdio.h>
/* rand(), el respaldo si /dev/urandom no esta disponible. */
#include <stdlib.h>
/* memset, strcmp, strncpy y strlen. */
#include <string.h>
/* time(), para el vencimiento de sesiones y los bloqueos. */
#include <time.h>
/* Mutex que protege las tablas. */
#include <pthread.h>
/* open() y O_RDONLY, para leer /dev/urandom. */
#include <fcntl.h>
/* read() y close(). */
#include <unistd.h>

/* ═══════════════════════════════════════════════════════════
   TABLA DE USUARIOS
═══════════════════════════════════════════════════════════ */
/* Un usuario: su nombre y el SHA-256 de su contrasena, en hexadecimal. La
   contrasena en claro no se guarda en ningun lado. */
typedef struct { const char *user; const char *pass_hash; } UserEntry;

/* Usuarios registrados. El resumen de una clave nueva sale de:
   echo -n 'clave' | sha256sum */
static const UserEntry USERS[] = {
    { "user1", "0b14d501a594442a01c6859541bcb3e8164d183d32937b851835442f69d5c94e" },
};
/* Cantidad de usuarios de la tabla. */
static const int NUM_USERS = (int)(sizeof(USERS) / sizeof(USERS[0]));

/* ═══════════════════════════════════════════════════════════
   SESIONES
═══════════════════════════════════════════════════════════ */
/* Una sesion abierta. */
typedef struct {
    /* Token aleatorio que viaja en la cookie del navegador. */
    char   token[SESSION_TOKEN_LEN + 1];
    /* Usuario al que pertenece. */
    char   username[64];
    /* Ultima peticion recibida con este token; de aqui sale el vencimiento. */
    time_t last_seen;
    /* 1 si la casilla esta en uso. */
    int    active;
} Session;

/* Tabla de sesiones, de tamano fijo. */
static Session         g_sessions[MAX_SESSIONS];
/* Protege las sesiones y la tabla de intentos fallidos. */
static pthread_mutex_t g_lock;

/* ═══════════════════════════════════════════════════════════
   PROTECCION DE IPS
═══════════════════════════════════════════════════════════ */
/* Seguimiento de los logins fallidos de una direccion IP. */
typedef struct {
    /* Direccion en texto; 46 caracteres alcanzan para una IPv6. */
    char   ip[46];
    /* Fallos acumulados en la ventana actual. */
    int    attempts;
    /* Cuando empezo la ventana en la que se cuentan los fallos. */
    time_t window_start;
    /* Hasta cuando esta bloqueada; 0 si no lo esta. */
    time_t blocked_until;
} BruteEntry;

/* Tabla de direcciones vigiladas, de tamano fijo. */
static BruteEntry g_brute[BRUTE_TRACK_IPS];

/* ═══════════════════════════════════════════════════════════
   Helpers internos
═══════════════════════════════════════════════════════════ */

// Generar Token
static void gen_token(char *buf) {
    /* Bytes aleatorios: cada uno da dos caracteres hexadecimales del token. */
    unsigned char raw[SESSION_TOKEN_LEN / 2];
    /* 1 si se leyeron todos los bytes del generador del sistema. */
    int ok = 0;
    /* Fuente de aleatoriedad del kernel. */
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ok = (read(fd, raw, sizeof(raw)) == (ssize_t)sizeof(raw));
        close(fd);
    }
    /* Con bytes aleatorios buenos, cada uno se escribe como dos digitos hex. */
    if (ok) {
        for (int i = 0; i < (int)sizeof(raw); i++)
            snprintf(buf + i*2, 3, "%02x", raw[i]);
    } else {
        /* Respaldo si no se pudo leer /dev/urandom: digitos hex sacados de
           rand(). */
        const char hex[] = "0123456789abcdef";
        for (int i = 0; i < SESSION_TOKEN_LEN; i++)
            buf[i] = hex[rand() % 16];
    }
    /* Terminador de cadena. */
    buf[SESSION_TOKEN_LEN] = '\0';
}

// Comparación en tiempo constante de tokens
/* Acumula con OR las diferencias de todos los bytes. Recorre siempre el
   largo completo, asi el tiempo que tarda no revela en que posicion
   difieren. Devuelve 0 si son iguales. */
static int const_time_cmp(const char *a, const char *b, size_t len) {
    unsigned char diff = 0;
    for (size_t i = 0; i < len; i++)
        diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
    return (int)diff;
}

// Expirar sesiones inactivas
static void expire_old(void) {
    /* Hora actual. */
    time_t now = time(NULL);
    /* Marca como inactiva toda sesion sin actividad por mas de
       SESSION_TTL_SECS. */
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (g_sessions[i].active &&
            (now - g_sessions[i].last_seen) > SESSION_TTL_SECS) {
            printf("[auth] sesión expirada para '%s'\n", g_sessions[i].username);
            g_sessions[i].active = 0;
        }
}

// Encontrar sesion por token
static Session *find_by_token(const char *token) {
    /* Un token ausente o de otro largo no puede ser valido. */
    if (!token || strlen(token) != SESSION_TOKEN_LEN) return NULL;
    /* Busca entre las sesiones activas, comparando en tiempo constante. */
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (g_sessions[i].active &&
            const_time_cmp(g_sessions[i].token, token, SESSION_TOKEN_LEN) == 0)
            return &g_sessions[i];
    return NULL;
}

// Encontrar sesion por user
static Session *find_by_user(const char *username) {
    /* Busca una sesion activa de ese usuario. */
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (g_sessions[i].active &&
            strcmp(g_sessions[i].username, username) == 0)
            return &g_sessions[i];
    return NULL;
}

// Cuenta de sesiones activas
static int count_active(void) {
    /* Cuenta las casillas en uso. */
    int n = 0;
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (g_sessions[i].active) n++;
    return n;
}

// Verificar slots de sesiones vacios
static Session *find_free_slot(void) {
    /* Devuelve la primera casilla libre, o NULL si estan todas ocupadas. */
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (!g_sessions[i].active) return &g_sessions[i];
    return NULL;
}

/* ═══════════════════════════════════════════════════════════
   IMPLEMENTACION DE LA API
═══════════════════════════════════════════════════════════ */

/* Limpia las dos tablas y crea el mutex. Devuelve 0 o -1. */
int auth_init(void) {
    memset(g_sessions, 0, sizeof(g_sessions));
    memset(g_brute,    0, sizeof(g_brute));
    if (pthread_mutex_init(&g_lock, NULL) != 0) return -1;
    printf("[auth] inicializado — %d usuario(s), maximo %d sesiones simultaneas\n",
           NUM_USERS, MAX_SESSIONS);
    return 0;
}

/* Libera el mutex. */
void auth_destroy(void) {
    pthread_mutex_destroy(&g_lock);
}

// Valida usuario + contraseña (compara contra hash SHA-256)
int auth_validate(const char *username, const char *password) {
    /* Sin usuario o sin contrasena no hay nada que validar. */
    if (!username || !password) return 0;
    /* Resumen de la contrasena recibida: 64 caracteres hex y el terminador. */
    char hash[65];
    sha256_hex(password, strlen(password), hash);
    /* Si coinciden el nombre y el resumen, las credenciales son validas. */
    for (int i = 0; i < NUM_USERS; i++)
        if (strcmp(USERS[i].user, username) == 0 &&
            strcmp(USERS[i].pass_hash, hash) == 0)
            return 1;
    return 0;
}

// Crea sesión para usuario ya validado.
//   - Si el usuario ya tiene sesión, se invalida (una sesión por usuario)
//   - Si ya hay MAX_SESSIONS usuarios distintos → AUTH_ERR_FULL
AuthResult auth_create_session(const char *username, char *out_token) {
    /* Toda la funcion corre bajo el candado. Primero se descartan las
       sesiones
       vencidas. */
    pthread_mutex_lock(&g_lock);
    expire_old();

    // Regla 1: una sesión por usuario — cerrar la sesión anterior
    Session *existing = find_by_user(username);
    if (existing) {
        printf("[auth] '%s' ya tenia sesion activa — cerrando sesion anterior\n",
               username);
        existing->active = 0;
    }

    // Regla 2: límite de usuarios simultáneos
    if (count_active() >= MAX_SESSIONS) {
        pthread_mutex_unlock(&g_lock);
        printf("[auth] limite de sesiones (%d) alcanzado — '%s' rechazado\n",
               MAX_SESSIONS, username);
        return AUTH_ERR_FULL;
    }

    /* Casilla libre para la sesion nueva. */
    Session *slot = find_free_slot();
    if (!slot) {
        pthread_mutex_unlock(&g_lock);
        return AUTH_ERR_FULL;
    }

    /* Llena la casilla: token nuevo, usuario, hora y marca de uso. */
    gen_token(slot->token);
    strncpy(slot->username, username, sizeof(slot->username) - 1);
    slot->username[sizeof(slot->username) - 1] = '\0';
    slot->last_seen = time(NULL);
    slot->active    = 1;
    /* Devuelve el token al llamador, que lo manda en la cookie. */
    strncpy(out_token, slot->token, SESSION_TOKEN_LEN + 1);

    printf("[auth] sesion creada para '%s' (activas: %d/%d)\n",
           username, count_active(), MAX_SESSIONS);

    pthread_mutex_unlock(&g_lock);
    return AUTH_OK;
}

// Verifica cookie de sesión en una request
int auth_check_request(struct MHD_Connection *conn) {
    /* Busca la cookie "session" en la peticion. */
    const char *cookie = MHD_lookup_connection_value(
        conn, MHD_COOKIE_KIND, "session");
    /* Sin cookie no hay sesion. */
    if (!cookie) return 0;
    /* Bajo el candado: descarta vencidas, busca el token y renueva su hora. */
    pthread_mutex_lock(&g_lock);
    expire_old();
    Session *s = find_by_token(cookie);
    if (s) s->last_seen = time(NULL);
    pthread_mutex_unlock(&g_lock);
    /* 1 si el token corresponde a una sesion activa. */
    return s != NULL;
}

// Invalida sesión
void auth_invalidate(const char *token) {
    /* Busca la sesion de ese token y la marca como inactiva. */
    pthread_mutex_lock(&g_lock);
    Session *s = find_by_token(token);
    if (s) {
        printf("[auth] logout de '%s'\n", s->username);
        s->active = 0;
    }
    pthread_mutex_unlock(&g_lock);
}

/* Cuenta las sesiones activas, descartando antes las vencidas. */
int auth_active_sessions(void) {
    pthread_mutex_lock(&g_lock);
    expire_old();
    int n = count_active();
    pthread_mutex_unlock(&g_lock);
    return n;
}

/* ═══════════════════════════════════════════════════════════
    SEGURIDAD / PROTECCION DE IPS
═══════════════════════════════════════════════════════════ */

/* Devuelve la entrada de una IP. Si la IP no esta en la tabla, reutiliza
   la entrada mas antigua. */
static BruteEntry *brute_get(const char *ip) {
    /* Hora actual, para estrenar la ventana de una IP nueva. */
    time_t now = time(NULL);
    /* Candidata a reutilizar: la de ventana mas vieja. */
    BruteEntry *oldest = &g_brute[0];
    /* Recorre la tabla: si encuentra la IP, la devuelve; de paso recuerda la
       entrada mas antigua. */
    for (int i = 0; i < BRUTE_TRACK_IPS; i++) {
        if (strcmp(g_brute[i].ip, ip) == 0) return &g_brute[i];
        if (g_brute[i].window_start < oldest->window_start)
            oldest = &g_brute[i];
    }
    //IP nueva, reutiliza el slot más antiguo
    /* Limpia la entrada y la asigna a la IP nueva. */
    memset(oldest, 0, sizeof(*oldest));
    strncpy(oldest->ip, ip, sizeof(oldest->ip) - 1);
    oldest->window_start = now;
    return oldest;
}

// Autorizacion de IP para inicio de seion
int auth_ip_allowed(const char *ip) {
    /* Sin IP no se puede vigilar: se permite. */
    if (!ip) return 1;
    time_t now = time(NULL);
    pthread_mutex_lock(&g_lock);
    /* Entrada de esta IP. */
    BruteEntry *e = brute_get(ip);

    // En caso de que la IP ya se encuentre restringida
    if (e->blocked_until > 0 && now < e->blocked_until) {
        /* Segundos que faltan para que se levante el bloqueo. */
        int left = (int)(e->blocked_until - now);
        pthread_mutex_unlock(&g_lock);
        printf("[auth] IP %s bloqueada — %d segundos restantes\n", ip, left);
        return 0;
    }
    // Resetear la ventana expirada
    if ((now - e->window_start) > BRUTE_WINDOW_SECS) {
        e->attempts      = 0;
        e->window_start  = now;
        e->blocked_until = 0;
    }
    pthread_mutex_unlock(&g_lock);
    return 1;
}

// Restringir usuario si intenta iniciar sesion muchas veces sin exito
void auth_ip_failed(const char *ip) {
    /* Sin IP no hay a quien anotarle el fallo. */
    if (!ip) return;
    time_t now = time(NULL);
    pthread_mutex_lock(&g_lock);
    /* Entrada de esta IP. */
    BruteEntry *e = brute_get(ip);

    /* Si la ventana anterior ya vencio, se empieza a contar de nuevo. */
    if ((now - e->window_start) > BRUTE_WINDOW_SECS) {
        e->attempts      = 0;
        e->window_start  = now;
        e->blocked_until = 0;
    }
    /* Un fallo mas. */
    e->attempts++;
    printf("[auth] intento fallido desde %s (%d/%d)\n",
           ip, e->attempts, BRUTE_MAX_ATTEMPTS);

    /* Al llegar al maximo, la IP queda bloqueada por BRUTE_BLOCK_SECS. */
    if (e->attempts >= BRUTE_MAX_ATTEMPTS) {
        e->blocked_until = now + BRUTE_BLOCK_SECS;
        printf("[auth] IP %s BLOQUEADA por %d segundos\n", ip, BRUTE_BLOCK_SECS);
    }
    pthread_mutex_unlock(&g_lock);
}

// Validar ip con inicio de sesion exitoso
void auth_ip_success(const char *ip) {
    /* Sin IP no hay nada que limpiar. */
    if (!ip) return;
    pthread_mutex_lock(&g_lock);
    /* Un login correcto borra los fallos y el bloqueo de esa IP. */
    BruteEntry *e = brute_get(ip);
    e->attempts      = 0;
    e->blocked_until = 0;
    pthread_mutex_unlock(&g_lock);
}