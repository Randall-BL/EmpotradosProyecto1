/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#include "lib_robot.h"
#include "lib_motors.h"
#include "lib_sensors.h"
#include "lib_leds.h"
#include "lib_odom.h"

#include <pigpiod_if2.h>
#include <stdio.h>

/* Sensores HC-SR04: pines TRIG y ECHO en numeracion BCM.
   Ver docs/hardware-pinout.md — el ECHO va con divisor de tension. */
#define TRIG_FRONTAL 17
#define ECHO_FRONTAL 27
#define TRIG_IZQ     22
#define ECHO_IZQ     10
#define TRIG_DER      9
#define ECHO_DER     11

static SensorUltrasonico g_frontal;
static SensorUltrasonico g_izquierdo;
static SensorUltrasonico g_derecho;

static int g_pi    = -1;
static int g_listo = 0;

int robot_init(void) {
    if (g_listo) return 0;

    g_pi = pigpio_start(NULL, NULL);
    if (g_pi < 0) {
        fprintf(stderr, "[librobot] no se pudo conectar a pigpiod (codigo %d). "
                        "Verificar que el servicio pigpiod este corriendo.\n", g_pi);
        return -1;
    }

    motores_init(g_pi);
    sensor_init(g_pi, &g_frontal,    TRIG_FRONTAL, ECHO_FRONTAL);
    sensor_init(g_pi, &g_izquierdo,  TRIG_IZQ,     ECHO_IZQ);
    sensor_init(g_pi, &g_derecho,    TRIG_DER,     ECHO_DER);

    /* Los LEDs son indicadores: si fallan, el robot igual navega. */
    if (lib_leds_init(g_pi) < 0)
        fprintf(stderr, "[librobot] los LEDs indicadores no se pudieron inicializar\n");

    odom_reset();

    g_listo = 1;
    return 0;
}

void robot_shutdown(void) {
    if (!g_listo) return;

    motores_detener();

    /* Dejar los TRIG en bajo para que ningun sensor quede disparando. */
    gpio_write(g_pi, g_frontal.pinTrigger,   0);
    gpio_write(g_pi, g_izquierdo.pinTrigger, 0);
    gpio_write(g_pi, g_derecho.pinTrigger,   0);

    lib_leds_destroy();
    pigpio_stop(g_pi);

    g_pi    = -1;
    g_listo = 0;
}

int robot_activo(void) {
    return g_listo;
}

double robot_distancia_frontal(void)    { return sensor_leer_distancia(&g_frontal);    }
double robot_distancia_izquierda(void)  { return sensor_leer_distancia(&g_izquierdo);  }
double robot_distancia_derecha(void)    { return sensor_leer_distancia(&g_derecho);    }
