/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#ifndef AUTH_H
#define AUTH_H

#include <microhttpd.h>

// Parametros de seguridad de las sesiones

//Maximo de sesiones simultáneas
#define MAX_SESSIONS         4

//Duración de la sesión sin actividad en segundos
#define SESSION_TTL_SECS     600     

//Longitud del token de sesión 
#define SESSION_TOKEN_LEN    64       

// Protección de IP
#define BRUTE_MAX_ATTEMPTS   5        
#define BRUTE_WINDOW_SECS    300      
#define BRUTE_BLOCK_SECS     600      
#define BRUTE_TRACK_IPS      32     

// Resultados de autenticacion
typedef enum {
    AUTH_OK          = 0, 
    AUTH_ERR_FULL    = 1,  
    AUTH_ERR_ALREADY = 2,  
} AuthResult;

/* ═══════════════════════════════════════════════════════════
   PARAMETROS / VOIDS DE LA API
═══════════════════════════════════════════════════════════ */

int        auth_init(void);
void       auth_destroy(void);
int        auth_validate(const char *username, const char *password);
AuthResult auth_create_session(const char *username, char *out_token);
int        auth_check_request(struct MHD_Connection *connection);
void       auth_invalidate(const char *token);
int        auth_ip_allowed(const char *ip);
void       auth_ip_failed (const char *ip);
void       auth_ip_success(const char *ip);
int        auth_active_sessions(void);

#endif 