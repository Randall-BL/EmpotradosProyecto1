/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#ifndef ULTRASONIC_SENSOR_H
#define ULTRASONIC_SENSOR_H

#include <stdint.h>

/**
 * @struct SensorUltrasonico
 * @brief Estructura para almacenar los pines de un sensor HC-SR04.
 */
typedef struct {
    int pinTrigger;
    int pinEcho;
} SensorUltrasonico;

/**
 * @brief Inicializa los pines de un sensor específico.
 */

void sensor_init(int pi, SensorUltrasonico* sensor, int trigger, int echo);

/**
 * @brief Calcula la distancia al obstáculo más cercano.
 * @return Distancia en centímetros o -1.0 si hay error/timeout.
 */
double sensor_leer_distancia(SensorUltrasonico* sensor);

#endif // ULTRASONIC_SENSOR_H
