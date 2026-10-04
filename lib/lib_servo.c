/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#include "lib_servo.h"
#include <pigpiod_if2.h>
#include <unistd.h>

static int g_pi     = -1;
static int g_angulo = -1;   /* -1: posicion desconocida */

void servo_init(int pi) {
    g_pi     = pi;
    g_angulo = -1;
    set_mode(g_pi, SERVO_GPIO, PI_OUTPUT);
    gpio_write(g_pi, SERVO_GPIO, 0);
}

static unsigned pulso_de(double grados) {
    return SERVO_PULSO_0_US +
        (unsigned)((SERVO_PULSO_180_US - SERVO_PULSO_0_US) * grados / 180.0 + 0.5);
}

int servo_mover(int grados) {
    if (g_pi < 0) return -1;

    if (grados < 0)   grados = 0;
    if (grados > 180) grados = 180;

    /* Desde una posicion desconocida no hay rampa posible: un salto, y se
       asume el peor caso, media vuelta a toda velocidad. */
    if (g_angulo < 0) {
        if (set_servo_pulsewidth(g_pi, SERVO_GPIO, pulso_de(grados)) != 0) return -1;
        g_angulo = grados;
        return (int)(180 * SERVO_MS_POR_GRADO) + SERVO_ASENTAMIENTO_MS;
    }

    /* Rampa: un pulso intermedio por periodo de 50 Hz. Cada escalon es lo que
       el servo recorre en ese periodo a la velocidad reducida, asi que llega
       al siguiente justo cuando le llega el pulso nuevo. */
    int    desde     = g_angulo;
    int    recorrido = grados > desde ? grados - desde : desde - grados;
    double ms_total  = recorrido * SERVO_MS_POR_GRADO * SERVO_DIVISOR_VELOCIDAD;
    int    pasos     = (int)(ms_total / SERVO_PERIODO_MS + 0.999);
    if (pasos < 1) pasos = 1;

    for (int i = 1; i <= pasos; i++) {
        double a = desde + (double)(grados - desde) * i / pasos;
        if (set_servo_pulsewidth(g_pi, SERVO_GPIO, pulso_de(a)) != 0) return -1;
        g_angulo = (int)(a + 0.5);   /* por si falla el siguiente pulso */
        if (i < pasos) usleep((useconds_t)(ms_total / pasos * 1000.0));
    }
    g_angulo = grados;

    /* Tras el ultimo pulso falta el ultimo escalon, que el servo recorre a su
       velocidad propia, y el asentamiento. */
    return (int)((double)recorrido / pasos * SERVO_MS_POR_GRADO) + SERVO_ASENTAMIENTO_MS;
}

int servo_angulo(void) {
    return g_angulo;
}

void servo_liberar(void) {
    if (g_pi < 0) return;
    set_servo_pulsewidth(g_pi, SERVO_GPIO, 0);
    g_angulo = -1;
}
