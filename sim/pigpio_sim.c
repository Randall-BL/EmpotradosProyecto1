/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#include "pigpiod_if2.h"
#include "mundo.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * Implementacion falsa de pigpio contra el mundo simulado.
 *
 * Traduce lo que la biblioteca le escribe a los pines en movimiento del robot
 * virtual, y contesta las lecturas de los sensores midiendo sobre ese mundo.
 * Los numeros de pin son los mismos de docs/hardware-pinout.md: si alguien
 * cambia el cableado alla y no aqui, el simulador deja de moverse, que es
 * justamente el aviso que uno quiere.
 */

/* Motores (L298N) */
#define ENA 12
#define IN1  5
#define IN2  6
#define ENB 13
#define IN3 23
#define IN4 24

/* LEDs indicadores */
#define LED_POWER_PIN      16
#define LED_AUTONOMOUS_PIN 20
#define LED_MANUAL_PIN     21
#define LED_OBSTACLE_PIN   26

/* Microsegundos de eco por centimetro: ida y vuelta a 343 m/s. */
#define US_POR_CM 58.309

/* ── Estado de los pines ─────────────────────────────────────────────────── */

static int g_in1, g_in2, g_in3, g_in4;
static int g_duty_a, g_duty_b;
static int g_leds[4];              /* power, autonomous, manual, obstacle */
static int g_verboso = 1;          /* imprimir cambios de LED */

/* Reloj virtual: microsegundos reales desde el arranque, mas lo que suman los
   pulsos de eco simulados. */
static struct timespec g_t0;
static uint32_t g_tick_extra = 0;
static int g_reloj_listo = 0;

/* ── Sensores ────────────────────────────────────────────────────────────── */

typedef enum { S_IDLE = 0, S_ARMADO, S_ESPERA_BAJO, S_PULSO_ALTO, S_PULSO_FIN } FaseSensor;

typedef struct {
    int        trig, echo;
    double     rumbo_offset;   /* grados respecto al frente */
    FaseSensor fase;
    uint32_t   pulso_us;       /* 0 = sin eco */
} SensorSim;

static SensorSim g_sensores[] = {
    { 17, 27,   0.0, S_IDLE, 0 },   /* frontal   */
    { 22, 10, -90.0, S_IDLE, 0 },   /* izquierdo */
    {  9, 11,  90.0, S_IDLE, 0 },   /* derecho   */
};
#define N_SENSORES ((int)(sizeof(g_sensores) / sizeof(g_sensores[0])))

static SensorSim *sensor_por_trig(unsigned gpio) {
    for (int i = 0; i < N_SENSORES; i++)
        if (g_sensores[i].trig == (int)gpio) return &g_sensores[i];
    return NULL;
}

static SensorSim *sensor_por_echo(unsigned gpio) {
    for (int i = 0; i < N_SENSORES; i++)
        if (g_sensores[i].echo == (int)gpio) return &g_sensores[i];
    return NULL;
}

/* ── Motores ─────────────────────────────────────────────────────────────── */

static int sentido(int a, int b) {
    if (a && !b) return  1;
    if (!a && b) return -1;
    return 0;                       /* ambos en 0 o en 1: motor libre */
}

static void empujar_motores(void) {
    mundo_set_motores(g_duty_a * sentido(g_in1, g_in2),
                      g_duty_b * sentido(g_in3, g_in4));
}

/* ── API de pigpio ───────────────────────────────────────────────────────── */

int pigpio_start(const char *addrStr, const char *portStr) {
    (void)addrStr; (void)portStr;
    mundo_init();
    memset(g_leds, 0, sizeof(g_leds));
    g_in1 = g_in2 = g_in3 = g_in4 = 0;
    g_duty_a = g_duty_b = 0;
    clock_gettime(CLOCK_MONOTONIC, &g_t0);
    g_tick_extra  = 0;
    g_reloj_listo = 1;
    printf("[sim] pigpiod simulado: robot en el centro de una sala de 4x4 m\n");
    return 1;                        /* handle cualquiera, pero >= 0 */
}

void pigpio_stop(int pi) {
    (void)pi;
    mundo_set_motores(0, 0);
    printf("[sim] pigpiod simulado cerrado\n");
}

int set_mode(int pi, unsigned gpio, unsigned mode) {
    (void)pi; (void)gpio; (void)mode;
    return 0;
}

int set_PWM_frequency(int pi, unsigned user_gpio, unsigned frequency) {
    (void)pi; (void)user_gpio; (void)frequency;
    return 0;
}

int set_PWM_range(int pi, unsigned user_gpio, unsigned range) {
    (void)pi; (void)user_gpio; (void)range;
    return 0;
}

int set_PWM_dutycycle(int pi, unsigned user_gpio, unsigned dutycycle) {
    (void)pi;
    if      (user_gpio == ENA) g_duty_a = (int)dutycycle;
    else if (user_gpio == ENB) g_duty_b = (int)dutycycle;
    else return -1;
    empujar_motores();
    return 0;
}

int gpio_write(int pi, unsigned gpio, unsigned level) {
    (void)pi;
    int nivel = level ? 1 : 0;

    switch (gpio) {
        case IN1: g_in1 = nivel; empujar_motores(); return 0;
        case IN2: g_in2 = nivel; empujar_motores(); return 0;
        case IN3: g_in3 = nivel; empujar_motores(); return 0;
        case IN4: g_in4 = nivel; empujar_motores(); return 0;

        case LED_POWER_PIN:      case LED_AUTONOMOUS_PIN:
        case LED_MANUAL_PIN:     case LED_OBSTACLE_PIN: {
            static const char *nombre[] = { "encendido", "autonomo", "manual", "obstaculo" };
            int idx = gpio == LED_POWER_PIN      ? 0 :
                      gpio == LED_AUTONOMOUS_PIN ? 1 :
                      gpio == LED_MANUAL_PIN     ? 2 : 3;
            if (g_leds[idx] != nivel) {
                g_leds[idx] = nivel;
                if (g_verboso)
                    printf("[sim] LED %-10s %s\n", nombre[idx], nivel ? "ENCENDIDO" : "apagado");
            }
            return 0;
        }
        default: break;
    }

    SensorSim *s = sensor_por_trig(gpio);
    if (s) {
        if (nivel) {
            s->fase = S_ARMADO;
        } else if (s->fase == S_ARMADO) {
            /* Flanco de bajada del TRIG: el HC-SR04 dispara y prepara el eco. */
            double d = mundo_distancia(s->rumbo_offset);
            s->pulso_us = (d < 0.0) ? 0 : (uint32_t)(d * US_POR_CM);
            s->fase     = S_ESPERA_BAJO;
        }
        return 0;
    }
    return 0;
}

int gpio_read(int pi, unsigned gpio) {
    (void)pi;
    SensorSim *s = sensor_por_echo(gpio);
    if (!s) return 0;

    switch (s->fase) {
        case S_ESPERA_BAJO:
            if (s->pulso_us == 0) {
                /* Sin eco: el reloj corre hasta que el lazo se rinde por timeout. */
                g_tick_extra += 10000;
                return 0;
            }
            s->fase = S_PULSO_ALTO;
            return 0;                       /* ultimo instante en bajo */

        case S_PULSO_ALTO:
            g_tick_extra += s->pulso_us;    /* el eco duro esto */
            s->fase = S_PULSO_FIN;
            return 1;

        case S_PULSO_FIN:
            s->fase = S_IDLE;
            return 0;                       /* flanco de bajada: fin de la medicion */

        default:
            return 0;
    }
}

uint32_t get_current_tick(int pi) {
    (void)pi;
    if (!g_reloj_listo) return g_tick_extra;

    struct timespec ahora;
    clock_gettime(CLOCK_MONOTONIC, &ahora);
    uint64_t us = (uint64_t)(ahora.tv_sec - g_t0.tv_sec) * 1000000ULL +
                  (ahora.tv_nsec - g_t0.tv_nsec) / 1000ULL;
    return (uint32_t)(us + g_tick_extra);
}
