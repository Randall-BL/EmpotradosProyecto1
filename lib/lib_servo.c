/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#include "lib_servo.h"
#include <pigpiod_if2.h>

static int g_pi     = -1;
static int g_angulo = -1;   /* -1: posicion desconocida */

void servo_init(int pi) {
    g_pi     = pi;
    g_angulo = -1;
    set_mode(g_pi, SERVO_GPIO, PI_OUTPUT);
    gpio_write(g_pi, SERVO_GPIO, 0);
}

int servo_mover(int grados) {
    if (g_pi < 0) return -1;

    if (grados < 0)   grados = 0;
    if (grados > 180) grados = 180;

    unsigned pulso = SERVO_PULSO_0_US +
        (unsigned)((SERVO_PULSO_180_US - SERVO_PULSO_0_US) * grados / 180);
    if (set_servo_pulsewidth(g_pi, SERVO_GPIO, pulso) != 0) return -1;

    /* Desde una posicion desconocida se asume el peor caso: media vuelta. */
    int recorrido = g_angulo < 0 ? 180
                  : (grados > g_angulo ? grados - g_angulo : g_angulo - grados);
    g_angulo = grados;

    return (int)(recorrido * SERVO_MS_POR_GRADO) + SERVO_ASENTAMIENTO_MS;
}

int servo_angulo(void) {
    return g_angulo;
}

void servo_liberar(void) {
    if (g_pi < 0) return;
    set_servo_pulsewidth(g_pi, SERVO_GPIO, 0);
    g_angulo = -1;
}
