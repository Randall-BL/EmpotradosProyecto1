/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#ifndef LIB_AUDIO_H
#define LIB_AUDIO_H
// sudo apt install libmpg123-dev libasound2-dev mpg123
#include <stdint.h>

// Configuracion
#define LIB_AUDIO_DIR_DEFAULT   "./audio"   // directorio para guardar los tracks / audios  

/* Salida PWM analogica de la RPi4 (card 1, "Headphones"). El overlay audremap
   la saca por GPIO 18 hacia el filtro RC y el amplificador PAM8403, en vez de
   por el jack de 3.5 mm. Ver docs/hardware-sensores.md. */
#define LIB_AUDIO_ALSA_DEVICE   "hw:1,0"
#define LIB_AUDIO_MIXER_CARD    "hw:1"            
#define LIB_AUDIO_MIXER_CTRL    "PCM"    // "PCM" Rasp, "Master" PC

/* El robot tiene un solo parlante, colgado de un solo canal del PAM8403.
   Con 1 cada pista estereo se mezcla a mono (L+R)/2 antes de salir, y los dos
   canales llevan la misma senal: no se pierde lo que viene solo por la
   derecha y da igual a cual de los dos GPIO quedo cableado el amplificador. */
#define LIB_AUDIO_MONO          1
#define LIB_AUDIO_TRACKS_MAX     64
#define LIB_AUDIO_NAME_MAX       128

/* Tipos */
typedef enum {
    LIB_AUDIO_STOPPED = 0,
    LIB_AUDIO_PLAYING = 1,
    LIB_AUDIO_PAUSED  = 2,
} LibAudioStatus;

typedef enum {
    NOTIFY_STARTUP    = 0,  
    NOTIFY_AUTONOMOUS = 1,  
    NOTIFY_OBSTACLE   = 2, 
    NOTIFY_MANUAL     = 3, 
} NotificationEvent;

// Parametros de cada Audio
typedef struct {
    int  id;
    char filename[LIB_AUDIO_NAME_MAX];  
    char filepath[640];                 
    int  duration_secs;                 
} LibAudioTrack;

/* ─────────────────────────────────────────────────
   API
───────────────────────────────────────────────── */

int  lib_audio_init   (const char *audio_dir);
void lib_audio_destroy(void);

int lib_audio_scan(void);

int lib_audio_get_tracks(LibAudioTrack *out, int max);

// Reproduccion del audio
int  lib_audio_play   (int track_id);
void lib_audio_pause  (void);
void lib_audio_resume (void);
void lib_audio_stop   (void);

// Volumen: 0-100
void lib_audio_set_volume(int vol);
int  lib_audio_get_volume(void);

// Estado actual
LibAudioStatus lib_audio_get_status    (void);
int            lib_audio_get_current_id(void);
float          lib_audio_get_position  (void);  

// Notificaciones
void lib_audio_notify(NotificationEvent event);

#endif
