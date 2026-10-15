/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *

 */

/* Tipos del estado y prototipos. */
#include "robot_state.h"
/* printf y fprintf. */
#include <stdio.h>
/* memset. */
#include <string.h>

// Declaracion de todos los estados del robot

/* La instancia unica del estado. */
static RobotState g_robot;
/* 1 cuando robot_state_init() ya corrio. */
static int        g_initialized = 0;

/* Crea el estado con sus valores iniciales y su mutex. Devuelve 0 o -1. */
int robot_state_init(void) {
    /* Llamarla dos veces no reinicializa. */
    if (g_initialized) return 0;

    /* Todo en cero; despues se fijan los valores que no son cero. */
    memset(&g_robot, 0, sizeof(g_robot));
    // Estado del modo de operacion
    g_robot.mode = MODE_AUTONOMOUS;

    // Estado de los Sensores
    g_robot.sensors.front_cm = 0.0f;
    g_robot.sensors.back_cm  = 0.0f;
    g_robot.sensors.left_cm  = 0.0f;
    g_robot.sensors.right_cm = 0.0f;

    // Estado de los leds
    g_robot.leds.power      = 1; 
    g_robot.leds.autonomous = 1;  
    g_robot.leds.manual     = 0;
    g_robot.leds.obstacle   = 0;

    // Estado de la funcionalidad de audio
    g_robot.audio.status           = AUDIO_STOPPED;
    g_robot.audio.current_track_id = -1;
    g_robot.audio.position_secs    = 0.0f;
    g_robot.audio.volume           = 70;
    g_robot.audio.track_count      = 0;

    /// Funcionalida de tracklist de la biblioteca

    // Estado del radar: nada medido todavia, servo al frente
    g_robot.radar.angulo_servo = 90;
    g_robot.radar.n            = 0;

    // Movimiento: quieto y sin riesgo de choque
    g_robot.movimiento.velocidad_cm_s = 0.0f;
    g_robot.movimiento.ttc_s          = -1.0f;
    g_robot.movimiento.imu            = 0;
    g_robot.movimiento.umbral_obstaculo_cm = UMBRAL_OBSTACULO_DEFECTO_CM;

    // Ciclo de limpieza: sin limite hasta que el usuario lo configure
    g_robot.ciclo.tipo = CICLO_OFF;
    g_robot.ciclo.meta = 0.0;

    // Estado de la posicion del robot
    memset(g_robot.map.grid, CELL_UNKNOWN, sizeof(g_robot.map.grid));
    /* El robot empieza en la celda central, mirando al norte. */
    g_robot.map.robot_x       = MAP_COLS / 2;
    g_robot.map.robot_y       = MAP_ROWS / 2;
    g_robot.map.robot_heading = 0;

    /* Crea el candado del estado. */
    if (pthread_mutex_init(&g_robot.lock, NULL) != 0) {
        fprintf(stderr, "[state] mutex init failed\n");
        return -1;
    }

    /* Desde aqui robot_state_get() entrega el puntero. */
    g_initialized = 1;
    printf("[state] initialized OK\n");
    return 0;
}

/* Devuelve el estado, o NULL si todavia no se creo. */
RobotState *robot_state_get(void) {
    return g_initialized ? &g_robot : NULL;
}

/* Destruye el mutex y marca el estado como no creado. */
void robot_state_destroy(void) {
    if (!g_initialized) return;
    pthread_mutex_destroy(&g_robot.lock);
    g_initialized = 0;
    printf("[state] destroyed\n");
}
/* ══════════════════════════════════════════════════════════
   Ciclo de limpieza
══════════════════════════════════════════════════════════ */

/* Cuenta las celdas del mapa marcadas como visitadas. */
int robot_mapa_visitadas(const RobotState *rs) {
    /* Contador. */
    int n = 0;
    /* Recorre todas las filas y columnas. */
    for (int f = 0; f < MAP_ROWS; f++)
        for (int c = 0; c < MAP_COLS; c++)
            if (rs->map.grid[f][c] == CELL_VISITED) n++;
    return n;
}

/* El area de un ciclo se mide con las celdas que se visitan durante el: las
   que ya estaban visitadas de ciclos anteriores no cuentan. */
void robot_ciclo_reiniciar(RobotState *rs) {
    /* Tiempo y area del ciclo en cero. */
    rs->ciclo.segundos         = 0.0;
    rs->ciclo.area_m2          = 0.0;
    /* Las celdas ya visitadas no cuentan para el ciclo nuevo. */
    rs->ciclo.visitadas_inicio = robot_mapa_visitadas(rs);
    /* El ciclo vuelve a estar en curso. */
    rs->ciclo.completo         = 0;
}

/* Avance del ciclo actual, entre 0 y 1. */
double robot_ciclo_progreso(const RobotState *rs) {
    /* Sin ciclo configurado, o sin meta, no hay avance que informar. */
    if (rs->ciclo.tipo == CICLO_OFF || rs->ciclo.meta <= 0.0) return 0.0;
    /* Lo hecho se mide en segundos o en metros cuadrados, segun el tipo. */
    double hecho = rs->ciclo.tipo == CICLO_TIEMPO ? rs->ciclo.segundos : rs->ciclo.area_m2;
    /* Proporcion de la meta, limitada a 1. */
    double p = hecho / rs->ciclo.meta;
    return p > 1.0 ? 1.0 : p;
}
