/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#include "auth.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>

/* ═══════════════════════════════════════════════════════════
   TABLA DE USUARIOS
═══════════════════════════════════════════════════════════ */
typedef struct { const char *user; const char *pass_hash; } UserEntry;

static const UserEntry USERS[] = {
    { "user1", "0b14d501a594442a01c6859541bcb3e8164d183d32937b851835442f69d5c94e" },
};
static const int NUM_USERS = (int)(sizeof(USERS) / sizeof(USERS[0]));

/* ═══════════════════════════════════════════════════════════
   SESIONES
═══════════════════════════════════════════════════════════ */
typedef struct {
    char   token[SESSION_TOKEN_LEN + 1];
    char   username[64];
    time_t last_seen;
    int    active;
} Session;

static Session         g_sessions[MAX_SESSIONS];
static pthread_mutex_t g_lock;

/* ═══════════════════════════════════════════════════════════
   PROTECCION DE IPS
═══════════════════════════════════════════════════════════ */
typedef struct {
    char   ip[46];
    int    attempts;
    time_t window_start;
    time_t blocked_until;
} BruteEntry;

static BruteEntry g_brute[BRUTE_TRACK_IPS];

/* ═══════════════════════════════════════════════════════════
   Helpers internos
═══════════════════════════════════════════════════════════ */

// Generar Token
static void gen_token(char *buf) {
    unsigned char raw[SESSION_TOKEN_LEN / 2];
    int ok = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ok = (read(fd, raw, sizeof(raw)) == (ssize_t)sizeof(raw));
        close(fd);
    }
    if (ok) {
        for (int i = 0; i < (int)sizeof(raw); i++)
            snprintf(buf + i*2, 3, "%02x", raw[i]);
    } else {
        const char hex[] = "0123456789abcdef";
        for (int i = 0; i < SESSION_TOKEN_LEN; i++)
            buf[i] = hex[rand() % 16];
    }
    buf[SESSION_TOKEN_LEN] = '\0';
}

// Comparación en tiempo constante de tokens
static int const_time_cmp(const char *a, const char *b, size_t len) {
    unsigned char diff = 0;
    for (size_t i = 0; i < len; i++)
        diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
    return (int)diff;
}

// Expirar sesiones inactivas
static void expire_old(void) {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (g_sessions[i].active &&
            (now - g_sessions[i].last_seen) > SESSION_TTL_SECS) {
            printf("[auth] sesión expirada para '%s'\n", g_sessions[i].username);
            g_sessions[i].active = 0;
        }
}

// Encontrar sesion por token
static Session *find_by_token(const char *token) {
    if (!token || strlen(token) != SESSION_TOKEN_LEN) return NULL;
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (g_sessions[i].active &&
            const_time_cmp(g_sessions[i].token, token, SESSION_TOKEN_LEN) == 0)
            return &g_sessions[i];
    return NULL;
}

// Encontrar sesion por user
static Session *find_by_user(const char *username) {
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (g_sessions[i].active &&
            strcmp(g_sessions[i].username, username) == 0)
            return &g_sessions[i];
    return NULL;
}

// Cuenta de sesiones activas
static int count_active(void) {
    int n = 0;
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (g_sessions[i].active) n++;
    return n;
}

// Verificar slots de sesiones vacios
static Session *find_free_slot(void) {
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (!g_sessions[i].active) return &g_sessions[i];
    return NULL;
}

/* ═══════════════════════════════════════════════════════════
   IMPLEMENTACION DE LA API
═══════════════════════════════════════════════════════════ */

int auth_init(void) {
    memset(g_sessions, 0, sizeof(g_sessions));
    memset(g_brute,    0, sizeof(g_brute));
    if (pthread_mutex_init(&g_lock, NULL) != 0) return -1;
    printf("[auth] inicializado — %d usuario(s), maximo %d sesiones simultaneas\n",
           NUM_USERS, MAX_SESSIONS);
    return 0;
}

void auth_destroy(void) {
    pthread_mutex_destroy(&g_lock);
}

// Valida usuario + contraseña (compara contra hash SHA-256)
int auth_validate(const char *username, const char *password) {
    if (!username || !password) return 0;
    char hash[65];
    sha256_hex(password, strlen(password), hash);
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

    Session *slot = find_free_slot();
    if (!slot) {
        pthread_mutex_unlock(&g_lock);
        return AUTH_ERR_FULL;
    }

    gen_token(slot->token);
    strncpy(slot->username, username, sizeof(slot->username) - 1);
    slot->username[sizeof(slot->username) - 1] = '\0';
    slot->last_seen = time(NULL);
    slot->active    = 1;
    strncpy(out_token, slot->token, SESSION_TOKEN_LEN + 1);

    printf("[auth] sesion creada para '%s' (activas: %d/%d)\n",
           username, count_active(), MAX_SESSIONS);

    pthread_mutex_unlock(&g_lock);
    return AUTH_OK;
}

// Verifica cookie de sesión en una request
int auth_check_request(struct MHD_Connection *conn) {
    const char *cookie = MHD_lookup_connection_value(
        conn, MHD_COOKIE_KIND, "session");
    if (!cookie) return 0;
    pthread_mutex_lock(&g_lock);
    expire_old();
    Session *s = find_by_token(cookie);
    if (s) s->last_seen = time(NULL);
    pthread_mutex_unlock(&g_lock);
    return s != NULL;
}

// Invalida sesión
void auth_invalidate(const char *token) {
    pthread_mutex_lock(&g_lock);
    Session *s = find_by_token(token);
    if (s) {
        printf("[auth] logout de '%s'\n", s->username);
        s->active = 0;
    }
    pthread_mutex_unlock(&g_lock);
}

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

static BruteEntry *brute_get(const char *ip) {
    time_t now = time(NULL);
    BruteEntry *oldest = &g_brute[0];
    for (int i = 0; i < BRUTE_TRACK_IPS; i++) {
        if (strcmp(g_brute[i].ip, ip) == 0) return &g_brute[i];
        if (g_brute[i].window_start < oldest->window_start)
            oldest = &g_brute[i];
    }
    //IP nueva, reutiliza el slot más antiguo
    memset(oldest, 0, sizeof(*oldest));
    strncpy(oldest->ip, ip, sizeof(oldest->ip) - 1);
    oldest->window_start = now;
    return oldest;
}

// Autorizacion de IP para inicio de seion
int auth_ip_allowed(const char *ip) {
    if (!ip) return 1;
    time_t now = time(NULL);
    pthread_mutex_lock(&g_lock);
    BruteEntry *e = brute_get(ip);

    // En caso de que la IP ya se encuentre restringida
    if (e->blocked_until > 0 && now < e->blocked_until) {
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
    if (!ip) return;
    time_t now = time(NULL);
    pthread_mutex_lock(&g_lock);
    BruteEntry *e = brute_get(ip);

    if ((now - e->window_start) > BRUTE_WINDOW_SECS) {
        e->attempts      = 0;
        e->window_start  = now;
        e->blocked_until = 0;
    }
    e->attempts++;
    printf("[auth] intento fallido desde %s (%d/%d)\n",
           ip, e->attempts, BRUTE_MAX_ATTEMPTS);

    if (e->attempts >= BRUTE_MAX_ATTEMPTS) {
        e->blocked_until = now + BRUTE_BLOCK_SECS;
        printf("[auth] IP %s BLOQUEADA por %d segundos\n", ip, BRUTE_BLOCK_SECS);
    }
    pthread_mutex_unlock(&g_lock);
}

// Validar ip con inicio de sesion exitoso
void auth_ip_success(const char *ip) {
    if (!ip) return;
    pthread_mutex_lock(&g_lock);
    BruteEntry *e = brute_get(ip);
    e->attempts      = 0;
    e->blocked_until = 0;
    pthread_mutex_unlock(&g_lock);
}