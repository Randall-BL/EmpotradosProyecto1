/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#ifndef ROBOT_STATE_H
#define ROBOT_STATE_H

#include <stdint.h>
#include <pthread.h>

// Recuadro para el mapa: 31x31 celdas de MAP_CELDA_CM de lado. Con celdas de
// 30 cm, del tamano del robot, cubre un cuadrado de 9.3 m.
#define MAP_COLS 31
#define MAP_ROWS 31
#define MAP_CELDA_CM 30

typedef enum { CELL_UNKNOWN = 0, CELL_VISITED = 1, CELL_OBSTACLE = 2 } CellState;

typedef struct {
    int robot_x;
    int robot_y;
    int robot_heading;  // 0=Norte 90=Este 180=Sur 270=Oeste
    uint8_t grid[MAP_ROWS][MAP_COLS];  // grilla
} MapState;

// Modo de operacion
typedef enum { MODE_AUTONOMOUS = 0, MODE_MANUAL = 1 } OperationMode;

// Sensores
typedef struct {
    float front_cm;
    float back_cm;
    float left_cm;
    float right_cm;
} ProximitySensors;

// Radar: ultima lectura de cada angulo del barrido del servo (0, 30, ..., 180)
#define RADAR_LECTURAS_MAX 7

typedef struct {
    int   angulo;        /* del servo: 0 derecha, 90 frente, 180 izquierda */
    float distancia_cm;  /* -1 sin eco */
    float edad_s;        /* -1 nunca medido */
} RadarPunto;

typedef struct {
    int        angulo_servo;
    int        n;
    RadarPunto lecturas[RADAR_LECTURAS_MAX];
} RadarState;

// Movimiento: velocidad estimada con el MPU-6050 y tiempo antes de chocar
typedef struct {
    float velocidad_cm_s;
    float ttc_s;         /* -1 sin riesgo de choque */
    int   imu;           /* 1 si la velocidad sale del MPU, 0 del modelo de motores */
} MovimientoState;

// Led de estados
typedef struct {
    int power;      
    int autonomous;
    int manual;
    int obstacle;
} LedStates;

// Parametros de la funcionalidad de audio
#define AUDIO_NAME_MAX    128
#define AUDIO_TRACKS_MAX   64

typedef enum { AUDIO_STOPPED = 0, AUDIO_PLAYING = 1, AUDIO_PAUSED = 2 } AudioStatus;

typedef struct {
    int  id;
    char filename[AUDIO_NAME_MAX];
    int  duration_secs;
} AudioTrack;

typedef struct {
    AudioStatus status;
    int         current_track_id;  /* -1 = none */
    float       position_secs;
    int         volume;            /* 0-100 */
    AudioTrack  tracks[AUDIO_TRACKS_MAX];
    int         track_count;
} AudioState;

// Ciclo de limpieza (opcional del enunciado): termina por tiempo en modo
// autonomo o por area recorrida, segun lo configure el usuario desde el panel.
typedef enum { CICLO_OFF = 0, CICLO_TIEMPO = 1, CICLO_AREA = 2 } CicloTipo;

typedef struct {
    CicloTipo tipo;
    double    meta;              /* segundos (TIEMPO) o m2 (AREA) */
    double    segundos;          /* en modo autonomo, en el ciclo actual */
    int       visitadas_inicio;  /* celdas visitadas al empezar el ciclo */
    double    area_m2;           /* recorrida en el ciclo actual */
    int       completo;          /* 1: termino y el robot espera quieto */
    unsigned  completados;       /* ciclos terminados desde el arranque */
} CicloState;

// Sensores infrarrojos de desnivel (opcional del enunciado)
typedef struct {
    int izq;                     /* 1: no hay piso bajo la esquina izquierda */
    int der;
    unsigned eventos;            /* desniveles detectados desde el arranque */
} CaidaState;

// Estados Globales
typedef struct {
    OperationMode    mode;
    ProximitySensors sensors;
    LedStates        leds;
    AudioState       audio;
    MapState         map;
    RadarState       radar;
    MovimientoState  movimiento;
    CicloState       ciclo;
    CaidaState       caida;
    uint64_t         uptime_secs;
    pthread_mutex_t  lock;
} RobotState;

// Singleton
int         robot_state_init(void);
RobotState *robot_state_get(void);
void        robot_state_destroy(void);

/* Ciclo de limpieza. Las tres se llaman con rs->lock tomado. */
int    robot_mapa_visitadas(const RobotState *rs);
void   robot_ciclo_reiniciar(RobotState *rs);
/* Avance del ciclo actual entre 0 y 1; 0 si el ciclo no tiene limite. */
double robot_ciclo_progreso(const RobotState *rs);

#endif
