/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 */

/* Guarda de inclusion: el contenido se procesa una sola vez por archivo. */
#ifndef ROBOT_STATE_H
#define ROBOT_STATE_H

/* uint8_t y uint64_t. */
#include <stdint.h>
/* pthread_mutex_t, el candado del estado. */
#include <pthread.h>

// Recuadro para el mapa: 31x31 celdas de MAP_CELDA_CM de lado. Con celdas de
// 30 cm, del tamano del robot, cubre un cuadrado de 9.3 m.
#define MAP_COLS 31
#define MAP_ROWS 31
#define MAP_CELDA_CM 30

/* Lo que puede haber en cada celda del mapa: nada conocido, el robot paso
   por ahi, o el radar vio un obstaculo. */
typedef enum { CELL_UNKNOWN = 0, CELL_VISITED = 1, CELL_OBSTACLE = 2 } CellState;

/* Mapa de recorrido y posicion del robot dentro de el. */
typedef struct {
    /* Columna y fila de la celda donde esta el robot. */
    int robot_x;
    int robot_y;
    int robot_heading;  // 0=Norte 90=Este 180=Sur 270=Oeste
    uint8_t grid[MAP_ROWS][MAP_COLS];  // grilla
} MapState;

// Modo de operacion
typedef enum { MODE_AUTONOMOUS = 0, MODE_MANUAL = 1 } OperationMode;

// Sensores
typedef struct {
    /* Distancia en cm en cada direccion, tal como se publica en /api/status. */
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
    /* Angulo al que apunta el servo en este momento. */
    int        angulo_servo;
    /* Cuantas entradas de lecturas[] son validas. */
    int        n;
    /* Una lectura por angulo del barrido. */
    RadarPunto lecturas[RADAR_LECTURAS_MAX];
} RadarState;

// Movimiento: velocidad estimada con el MPU-6050 y tiempo antes de chocar
typedef struct {
    /* Velocidad hacia adelante, en cm/s. */
    float velocidad_cm_s;
    float ttc_s;         /* -1 sin riesgo de choque */
    int   imu;           /* 1 si la velocidad sale del MPU, 0 del modelo de motores */
    /* Distancia al frente por debajo de la cual hay obstaculo, en cm. Se
       ajusta desde el panel, entre UMBRAL_OBSTACULO_MIN_CM y _MAX_CM. */
    float umbral_obstaculo_cm;
} MovimientoState;

/* Umbral de obstaculo al arrancar y limites que acepta el panel, en cm. */
#define UMBRAL_OBSTACULO_DEFECTO_CM 10.0f
#define UMBRAL_OBSTACULO_MIN_CM      5.0f
#define UMBRAL_OBSTACULO_MAX_CM    100.0f

// Led de estados
typedef struct {
    /* 1 = encendido, 0 = apagado; uno por cada LED indicador. */
    int power;      
    int autonomous;
    int manual;
    int obstacle;
} LedStates;

// Parametros de la funcionalidad de audio
/* Largo maximo del nombre de archivo de una pista, con el terminador. */
#define AUDIO_NAME_MAX    128
/* Cantidad maxima de pistas en la lista. */
#define AUDIO_TRACKS_MAX   64

/* Estado del reproductor. */
typedef enum { AUDIO_STOPPED = 0, AUDIO_PLAYING = 1, AUDIO_PAUSED = 2 } AudioStatus;

/* Una pista de la lista. */
typedef struct {
    /* Identificador con el que la API se refiere a la pista. */
    int  id;
    char filename[AUDIO_NAME_MAX];
    /* Duracion en segundos. */
    int  duration_secs;
} AudioTrack;

/* Estado completo del audio que se publica en /api/status. */
typedef struct {
    AudioStatus status;
    int         current_track_id;  /* -1 = none */
    /* Segundos transcurridos de la pista en curso. */
    float       position_secs;
    int         volume;            /* 0-100 */
    /* Lista de pistas y cuantas hay. */
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
/* Estado completo del robot. Hay una sola instancia, compartida por el
   servidor, la navegacion y la biblioteca. */
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
    /* Segundos desde que arranco el servidor. */
    uint64_t         uptime_secs;
    /* Candado que hay que tomar para leer o escribir cualquier campo. */
    pthread_mutex_t  lock;
} RobotState;

// Singleton
/* Crea la instancia unica. Devuelve 0 si quedo lista y -1 si no. */
int         robot_state_init(void);
/* Devuelve la instancia, o NULL si todavia no se creo. */
RobotState *robot_state_get(void);
/* Libera la instancia. */
void        robot_state_destroy(void);

/* Ciclo de limpieza. Las tres se llaman con rs->lock tomado. */
/* Cuenta las celdas del mapa marcadas como visitadas. */
int    robot_mapa_visitadas(const RobotState *rs);
/* Empieza un ciclo de limpieza nuevo: pone sus contadores en cero. */
void   robot_ciclo_reiniciar(RobotState *rs);
/* Avance del ciclo actual entre 0 y 1; 0 si el ciclo no tiene limite. */
double robot_ciclo_progreso(const RobotState *rs);

#endif
