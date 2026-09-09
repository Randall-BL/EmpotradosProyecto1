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

/* Ultima orden dada a cada motor, con signo. La lee la odometria. */
static int g_vel_izq = 0;
static int g_vel_der = 0;

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
    set_PWM_range(g_pi, ENA, MOTOR_PWM_MAX);
    set_PWM_range(g_pi, ENB, MOTOR_PWM_MAX);

    motores_detener();

}

static int saturar(int v) {
    if (v >  MOTOR_PWM_MAX) return  MOTOR_PWM_MAX;
    if (v < -MOTOR_PWM_MAX) return -MOTOR_PWM_MAX;
    return v;
}

void motores_set(int vel_izq, int vel_der) {
    if (g_pi < 0) return;

    vel_izq = saturar(vel_izq);
    vel_der = saturar(vel_der);

    /* Los pines IN definen el sentido; el PWM sobre EN define la magnitud.
       Con ambos IN en 0 el L298N deja el motor libre, que es como se frena. */
    gpio_write(g_pi, IN1, vel_izq > 0);
    gpio_write(g_pi, IN2, vel_izq < 0);
    gpio_write(g_pi, IN3, vel_der > 0);
    gpio_write(g_pi, IN4, vel_der < 0);

    set_PWM_dutycycle(g_pi, ENA, vel_izq < 0 ? -vel_izq : vel_izq);
    set_PWM_dutycycle(g_pi, ENB, vel_der < 0 ? -vel_der : vel_der);

    g_vel_izq = vel_izq;
    g_vel_der = vel_der;
}

void motores_get(int *vel_izq, int *vel_der) {
    if (vel_izq) *vel_izq = g_vel_izq;
    if (vel_der) *vel_der = g_vel_der;
}

void motores_detener(void) {
    motores_set(0, 0);
}

void motores_avanzar(int velocidad)         { motores_set( velocidad,  velocidad); }
void motores_retroceder(int velocidad)      { motores_set(-velocidad, -velocidad); }
void motores_girar_izquierda(int velocidad) { motores_set(-velocidad,  velocidad); }
void motores_girar_derecha(int velocidad)   { motores_set( velocidad, -velocidad); }

void motores_curva(int velocidad, int giro) {
    if (giro >  100) giro =  100;
    if (giro < -100) giro = -100;

    /* El motor del lado hacia el que se gira baja de velocidad; el otro
       mantiene la del parametro. Con giro = 100 el interior queda detenido. */
    int interior = velocidad - (velocidad * (giro < 0 ? -giro : giro)) / 100;

    if (giro >= 0) motores_set(velocidad, interior);
    else           motores_set(interior,  velocidad);
}
