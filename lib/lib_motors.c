/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#include "lib_motors.h"
#include <pigpiod_if2.h>

/* Entradas del L298N. Los jumpers ENA/ENB del modulo van puestos: el puente
   esta siempre habilitado y todo se controla con las entradas de sentido.
   Para avanzar, IN1 activa con IN2 en bajo; para retroceder, al reves; con
   las dos en bajo el L298N frena el motor en vez de soltarlo.

   Con MOTOR_VELOCIDAD_VARIABLE (lib_motors.h) la entrada activa lleva PWM:
   en la parte baja de cada ciclo las dos quedan iguales y el motor frena, asi
   que la velocidad sigue al ciclo de trabajo. Sin ella, la entrada activa
   queda fija en alto y el motor va al maximo. */

/* Motor Izquierdo (A) */
#define IN1  5
#define IN2  6

/* Motor Derecho (B) */
#define IN3 23
#define IN4 24

#define PWM_FREQ 1000

/* Las cuatro senales cruzan al L298N por un PC817 en emisor comun, que las
   invierte: GPIO en alto = entrada del L298N en bajo. Se compensa aqui, el
   unico punto donde la biblioteca toca los motores. Sin compensar, cada motor
   giraria al reves. Ver docs/hardware-aislamiento.md.
   En 0 solo para probar en banco con el L298N conectado sin optoacopladores. */
#define OPTO_INVERTIDO 1

static const unsigned PINES[] = { IN1, IN2, IN3, IN4 };
#define N_PINES ((int)(sizeof(PINES) / sizeof(PINES[0])))

static int g_pi = -1;

/* Ultima orden dada a cada motor, con signo. La lee la odometria. */
static int g_vel_izq = 0;
static int g_vel_der = 0;

/* Deja en la ENTRADA DEL L298N un ciclo de trabajo de 0 (siempre en bajo) a
   MOTOR_PWM_MAX (siempre en alto), compensando el optoacoplador. Con
   velocidad fija, cualquier ciclo distinto de cero es "siempre en alto". */
static void entrada_l298n(unsigned gpio, int duty) {
#if MOTOR_VELOCIDAD_VARIABLE
  #if OPTO_INVERTIDO
    duty = MOTOR_PWM_MAX - duty;
  #endif
    set_PWM_dutycycle(g_pi, gpio, (unsigned)duty);
#else
    int nivel = duty > 0;
  #if OPTO_INVERTIDO
    nivel = !nivel;
  #endif
    /* gpio_write tambien apaga la PWM si una version anterior del servidor
       la dejo corriendo en pigpiod. */
    gpio_write(g_pi, gpio, (unsigned)nivel);
#endif
}

void motores_init(int pi) {
    g_pi = pi;

    for (int i = 0; i < N_PINES; i++) {
        set_mode(g_pi, PINES[i], PI_OUTPUT);
#if MOTOR_VELOCIDAD_VARIABLE
        set_PWM_frequency(g_pi, PINES[i], PWM_FREQ);
        set_PWM_range(g_pi, PINES[i], MOTOR_PWM_MAX);
#endif
    }

    motores_detener();
}

/* Lo que de verdad recibe el motor: la orden saturada, o el maximo con su
   signo si la velocidad es fija. */
static int efectiva(int v) {
#if MOTOR_VELOCIDAD_VARIABLE
    if (v >  MOTOR_PWM_MAX) return  MOTOR_PWM_MAX;
    if (v < -MOTOR_PWM_MAX) return -MOTOR_PWM_MAX;
    return v;
#else
    return v > 0 ? MOTOR_PWM_MAX : v < 0 ? -MOTOR_PWM_MAX : 0;
#endif
}

/* Un motor: el signo elige cual de sus dos entradas lleva la PWM y la otra
   queda en bajo. Con 0 las dos quedan en bajo, que con el puente habilitado
   es freno. */
static void motor(unsigned in_avance, unsigned in_retroceso, int vel) {
    int magnitud = vel < 0 ? -vel : vel;
    entrada_l298n(in_avance,    vel > 0 ? magnitud : 0);
    entrada_l298n(in_retroceso, vel < 0 ? magnitud : 0);
}

void motores_set(int vel_izq, int vel_der) {
    if (g_pi < 0) return;

    vel_izq = efectiva(vel_izq);
    vel_der = efectiva(vel_der);

    motor(IN1, IN2, vel_izq);
    motor(IN3, IN4, vel_der);

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

#if MOTOR_VELOCIDAD_VARIABLE
    /* El motor del lado hacia el que se gira baja de velocidad; el otro
       mantiene la del parametro. Con giro = 100 el interior queda detenido. */
    int interior = velocidad - (velocidad * (giro < 0 ? -giro : giro)) / 100;

    if (giro >= 0) motores_set(velocidad, interior);
    else           motores_set(interior,  velocidad);
#else
    /* Sin velocidad variable no hay curvas de radio variable: bajar la llanta
       interior la dejaria igual al maximo. Se gira sobre el eje, con una
       llanta hacia adelante y la otra hacia atras. */
    if (giro > 0)      motores_girar_derecha(velocidad);
    else if (giro < 0) motores_girar_izquierda(velocidad);
    else               motores_avanzar(velocidad);
#endif
}
