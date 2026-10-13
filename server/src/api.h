/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 
 */

/* Guarda de inclusion: el contenido se procesa una sola vez por archivo. */
#ifndef API_H
#define API_H

/* enum MHD_Result y struct MHD_Connection. */
#include <microhttpd.h>

/*
 * Definicion de los Endpoints
 *
 * Publico:
 *   POST /api/login
 *
 * Protegido:
 *   POST /api/logout
 *   GET  /api/status
 *   POST /api/mode
 *   POST /api/move
 *   GET  /api/audio/list
 *   POST /api/audio/control
 *   POST /api/audio/volume
 *   GET  /api/audio/playlist
 *   POST /api/audio/playlist
 *   POST /api/ciclo
 */

/* Cada manejador recibe la conexion y, en los POST, el cuerpo de la
   peticion con su largo. Devuelve MHD_YES si pudo encolar la respuesta. */
/* Valida las credenciales y abre la sesion. */
enum MHD_Result api_login        (struct MHD_Connection *conn, const char *body, size_t len);
/* Cierra la sesion y detiene la musica. */
enum MHD_Result api_logout       (struct MHD_Connection *conn);
/* Estado completo del robot, en JSON. */
enum MHD_Result api_status       (struct MHD_Connection *conn);
/* Cambia entre modo manual y autonomo. */
enum MHD_Result api_set_mode     (struct MHD_Connection *conn, const char *body, size_t len);
/* Mueve el robot; solo en modo manual. */
enum MHD_Result api_move         (struct MHD_Connection *conn, const char *body, size_t len);
/* Lista de pistas. */
enum MHD_Result api_audio_list   (struct MHD_Connection *conn);
/* play, play_playlist, seek, pause, resume o stop. */
enum MHD_Result api_audio_control(struct MHD_Connection *conn, const char *body, size_t len);
/* Volumen de 0 a 100. */
enum MHD_Result api_audio_volume (struct MHD_Connection *conn, const char *body, size_t len);
/* Ids de la playlist y posicion que suena. */
enum MHD_Result api_audio_playlist_get(struct MHD_Connection *conn);
/* Reemplaza la playlist y la guarda. */
enum MHD_Result api_audio_playlist_set(struct MHD_Connection *conn, const char *body, size_t len);
/* Configura la meta del ciclo de limpieza o empieza uno nuevo. */
enum MHD_Result api_ciclo        (struct MHD_Connection *conn, const char *body, size_t len);

#endif /* API_H */