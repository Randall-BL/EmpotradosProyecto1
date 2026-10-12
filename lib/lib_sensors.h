/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 */

/* Guarda de inclusion: el contenido se procesa una sola vez por archivo. */
#ifndef ULTRASONIC_SENSOR_H
#define ULTRASONIC_SENSOR_H

/* Enteros de ancho fijo (uint32_t). */
#include <stdint.h>

/**
 * @struct SensorUltrasonico
 * @brief Estructura para almacenar los pines de un sensor HC-SR04.
 */
typedef struct {
    /* GPIO (BCM) que dispara la medicion: salida de la Raspberry Pi. */
    int pinTrigger;
    /* GPIO (BCM) por donde vuelve el pulso de eco: entrada. */
    int pinEcho;
} SensorUltrasonico;

/**
 * @brief Inicializa los pines de un sensor específico.
 */

/* pi: sesion con pigpiod. sensor: estructura que se llena.
   trigger, echo: GPIO (BCM) de cada senal. */
void sensor_init(int pi, SensorUltrasonico* sensor, int trigger, int echo);

/**
 * @brief Calcula la distancia al obstáculo más cercano.
 * @return Distancia en centímetros o -1.0 si hay error/timeout.
 */
double sensor_leer_distancia(SensorUltrasonico* sensor);

#endif // ULTRASONIC_SENSOR_H
