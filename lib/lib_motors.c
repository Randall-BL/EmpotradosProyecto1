/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#include "lib_motors.h"
#include <pigpiod_if2.h>
#include <unistd.h>

/* Motor Izquierdo (A) */
#define ENA 12
#define IN1  5
#define IN2  6

/* Motor Derecho (B) */
#define ENB 13
#define IN3 23
#define IN4 24

#define PWM_FREQ 1000

static int g_pi = -1;

void motores_init(int pi) {
    g_pi = pi;

    set_mode(g_pi, ENA, PI_OUTPUT);
    set_mode(g_pi, IN1, PI_OUTPUT);
    set_mode(g_pi, IN2, PI_OUTPUT);
    set_mode(g_pi, ENB, PI_OUTPUT);
    set_mode(g_pi, IN3, PI_OUTPUT);
    set_mode(g_pi, IN4, PI_OUTPUT);

    set_PWM_frequency(g_pi, ENA, PWM_FREQ);
    set_PWM_frequency(g_pi, ENB, PWM_FREQ);
    set_PWM_range(g_pi, ENA, 255);
    set_PWM_range(g_pi, ENB, 255);

    motores_detener();

}

void motores_detener(void) {
    if (g_pi < 0) return;
    
    set_PWM_dutycycle(g_pi, ENA, 0);
    set_PWM_dutycycle(g_pi, ENB, 0);
    gpio_write(g_pi, IN1, 0);
    gpio_write(g_pi, IN2, 0);
    gpio_write(g_pi, IN3, 0);
    gpio_write(g_pi, IN4, 0);
}

void motores_avanzar(int velocidad) {
    if (g_pi < 0) return;
    gpio_write(g_pi, IN1, 1); gpio_write(g_pi, IN2, 0);
    gpio_write(g_pi, IN3, 1); gpio_write(g_pi, IN4, 0);
    set_PWM_dutycycle(g_pi, ENA, velocidad);
    set_PWM_dutycycle(g_pi, ENB, velocidad);
}

void motores_retroceder(int velocidad) {
    if (g_pi < 0) return;
    gpio_write(g_pi, IN1, 0); gpio_write(g_pi, IN2, 1);
    gpio_write(g_pi, IN3, 0); gpio_write(g_pi, IN4, 1);
    set_PWM_dutycycle(g_pi, ENA, velocidad);
    set_PWM_dutycycle(g_pi, ENB, velocidad);
}

void motores_girar_izquierda(int velocidad) {
    if (g_pi < 0) return;
    gpio_write(g_pi, IN1, 0); gpio_write(g_pi, IN2, 1);
    gpio_write(g_pi, IN3, 1); gpio_write(g_pi, IN4, 0);
    set_PWM_dutycycle(g_pi, ENA, velocidad);
    set_PWM_dutycycle(g_pi, ENB, velocidad);
}

void motores_girar_derecha(int velocidad) {
    if (g_pi < 0) return;
    gpio_write(g_pi, IN1, 1); gpio_write(g_pi, IN2, 0);
    gpio_write(g_pi, IN3, 0); gpio_write(g_pi, IN4, 1);
    set_PWM_dutycycle(g_pi, ENA, velocidad);
    set_PWM_dutycycle(g_pi, ENB, velocidad);
}