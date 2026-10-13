/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *

 */

/* Guarda de inclusion: el contenido se procesa una sola vez por archivo. */
#ifndef AUTH_H
#define AUTH_H

/* struct MHD_Connection, el tipo de conexion de libmicrohttpd. */
#include <microhttpd.h>

// Parametros de seguridad de las sesiones

//Maximo de sesiones simultáneas
#define MAX_SESSIONS         4

//Duración de la sesión sin actividad en segundos
#define SESSION_TTL_SECS     600     

//Longitud del token de sesión 
#define SESSION_TOKEN_LEN    64       

// Protección de IP
/* Fallos de login que se toleran dentro de la ventana. */
#define BRUTE_MAX_ATTEMPTS   5        
/* Duracion de esa ventana, en segundos. */
#define BRUTE_WINDOW_SECS    300      
/* Segundos que dura el bloqueo de la IP. */
#define BRUTE_BLOCK_SECS     600      
/* Cuantas direcciones IP se vigilan a la vez. */
#define BRUTE_TRACK_IPS      32     

// Resultados de autenticacion
typedef enum {
    /* Sesion creada. */
    AUTH_OK          = 0, 
    /* Ya hay MAX_SESSIONS sesiones activas. */
    AUTH_ERR_FULL    = 1,  
    /* Reservado: el codigo actual no lo devuelve. */
    AUTH_ERR_ALREADY = 2,  
} AuthResult;

/* ═══════════════════════════════════════════════════════════
   PARAMETROS / VOIDS DE LA API
═══════════════════════════════════════════════════════════ */

/* Limpia las tablas y crea el mutex. Devuelve 0 o -1. */
int        auth_init(void);
/* Libera el mutex. */
void       auth_destroy(void);
/* 1 si el usuario existe y la contrasena coincide; 0 si no. */
int        auth_validate(const char *username, const char *password);
/* Abre una sesion para un usuario ya validado y escribe su token en
   out_token, que debe tener SESSION_TOKEN_LEN + 1 bytes. */
AuthResult auth_create_session(const char *username, char *out_token);
/* 1 si la peticion trae la cookie de una sesion activa; renueva su vigencia. */
int        auth_check_request(struct MHD_Connection *connection);
/* Cierra la sesion de ese token. */
void       auth_invalidate(const char *token);
/* 1 si esa IP puede intentar un login; 0 si esta bloqueada. */
int        auth_ip_allowed(const char *ip);
/* Anota un login fallido de esa IP; al llegar a BRUTE_MAX_ATTEMPTS la
   bloquea. */
void       auth_ip_failed (const char *ip);
/* Borra los fallos de esa IP tras un login correcto. */
void       auth_ip_success(const char *ip);
/* Cantidad de sesiones activas. */
int        auth_active_sessions(void);

#endif 