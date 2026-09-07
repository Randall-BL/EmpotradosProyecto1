/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#include "robot_state.h"
#include <stdio.h>
#include <string.h>

// Declaracion de todos los estados del robot

static RobotState g_robot;
static int        g_initialized = 0;

int robot_state_init(void) {
    if (g_initialized) return 0;

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

    // Estado de la posicion del robot
    memset(g_robot.map.grid, CELL_UNKNOWN, sizeof(g_robot.map.grid));
    g_robot.map.robot_x       = MAP_COLS / 2;
    g_robot.map.robot_y       = MAP_ROWS / 2;
    g_robot.map.robot_heading = 0;

    if (pthread_mutex_init(&g_robot.lock, NULL) != 0) {
        fprintf(stderr, "[state] mutex init failed\n");
        return -1;
    }

    g_initialized = 1;
    printf("[state] initialized OK\n");
    return 0;
}

RobotState *robot_state_get(void) {
    return g_initialized ? &g_robot : NULL;
}

void robot_state_destroy(void) {
    if (!g_initialized) return;
    pthread_mutex_destroy(&g_robot.lock);
    g_initialized = 0;
    printf("[state] destroyed\n");
}