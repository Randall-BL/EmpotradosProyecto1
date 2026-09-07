/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#ifndef API_H
#define API_H

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
 */

enum MHD_Result api_login        (struct MHD_Connection *conn, const char *body, size_t len);
enum MHD_Result api_logout       (struct MHD_Connection *conn);
enum MHD_Result api_status       (struct MHD_Connection *conn);
enum MHD_Result api_set_mode     (struct MHD_Connection *conn, const char *body, size_t len);
enum MHD_Result api_move         (struct MHD_Connection *conn, const char *body, size_t len);
enum MHD_Result api_audio_list   (struct MHD_Connection *conn);
enum MHD_Result api_audio_control(struct MHD_Connection *conn, const char *body, size_t len);
enum MHD_Result api_audio_volume (struct MHD_Connection *conn, const char *body, size_t len);

#endif /* API_H */