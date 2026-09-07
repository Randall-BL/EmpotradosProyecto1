/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#include "robot_hardware.h"
#include <stdio.h>
#include <pigpiod_if2.h> 

// Instancias globales de los sensores
SensorUltrasonico sensorFrontal;
SensorUltrasonico sensorLateralIzq;
SensorUltrasonico sensorLateralDer;

static int inicializado = 0;
static int g_pi = -1;  

int robot_hw_init() {
    g_pi = pigpio_start(NULL, NULL);  // ← conectar al daemon
    if (g_pi < 0) {
        fprintf(stderr, "Error: No se pudo conectar a pigpiod (codigo: %d)\n", g_pi);
        return -1;
    }

    inicializado = 1;

    motores_init(g_pi);          // ← pasar pi
    sensor_init(g_pi, &sensorFrontal,     17, 27);  // ← pasar pi
    sensor_init(g_pi, &sensorLateralIzq,  22, 10);
    sensor_init(g_pi, &sensorLateralDer,   9, 11);
    return 0;
}

// Exponer el handle para que otras libs lo usen
int robot_hw_get_pi() {
    return g_pi;
}

void robot_hw_cleanup() {
    if (inicializado) {
        motores_detener();
        gpio_write(g_pi, sensorFrontal.pinTrigger,    0);  // ← gpio_write
        gpio_write(g_pi, sensorLateralIzq.pinTrigger, 0);
        gpio_write(g_pi, sensorLateralDer.pinTrigger, 0);
        pigpio_stop(g_pi);  // ← desconectar del daemon
        inicializado = 0;
    }
}

double robot_get_distancia_frontal() { return sensor_leer_distancia(&sensorFrontal); }
double robot_get_distancia_izq()     { return sensor_leer_distancia(&sensorLateralIzq); }
double robot_get_distancia_der()     { return sensor_leer_distancia(&sensorLateralDer); }
