/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#include "lib_leds.h"
#include "robot_state.h" 
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <pigpiod_if2.h>

static int g_pi = -1;

/* ══════════════════════════════════════════════════════════
   Estado interno de la biblioteca
══════════════════════════════════════════════════════════ */

static const char *LED_NAMES[LED_COUNT] = {
    "POWER", "AUTONOMOUS", "MANUAL", "OBSTACLE"
};

static const int LED_PINS[LED_COUNT] = {
    LED_GPIO_POWER,
    LED_GPIO_AUTONOMOUS,
    LED_GPIO_MANUAL,
    LED_GPIO_OBSTACLE
};

// Estado 1 | 0
typedef struct {
    int state;     
} LedEntry;

static LedEntry g_leds[LED_COUNT];
static pthread_mutex_t g_lock;
static int g_initialized = 0;

/* ══════════════════════════════════════════════════════════
   API publica para los LEDS (AHORA USANDO PIGPIO)
══════════════════════════════════════════════════════════ */

int lib_leds_init(int pi)  
{
    if (g_initialized) return 0;
    g_pi = pi;  //  guardar handle

    memset(g_leds, 0, sizeof(g_leds));
    if (pthread_mutex_init(&g_lock, NULL) != 0) {
        fprintf(stderr, "[leds] ERROR: mutex init failed\n");
        return -1;
    }

    printf("[leds] Inicializando pines GPIOs (via pigpiod)\n");

    for (int i = 0; i < LED_COUNT; i++) {
        set_mode(g_pi, LED_PINS[i], PI_OUTPUT);  // set_mode en lugar de gpioSetMode
        gpio_write(g_pi, LED_PINS[i], 0);         //  gpio_write en lugar de gpioWrite
        printf("[leds] GPIO %d configurado para LED_%s\n", LED_PINS[i], LED_NAMES[i]);
    }

    g_initialized = 1;
    lib_leds_set(LED_POWER, 1);
    printf("[leds] Inicializado GPIOs OK\n");
    return 0;
}

// Encender o Apagar el LED
void lib_leds_set(LedId led, int state)
{
    if (!g_initialized || led < 0 || led >= LED_COUNT) return;
    state = state ? 1 : 0;

    pthread_mutex_lock(&g_lock);
    int prev = g_leds[led].state;
    g_leds[led].state = state;
    pthread_mutex_unlock(&g_lock);

    gpio_write(g_pi, LED_PINS[led], state);  // <-- gpio_write

    if (prev != state) {
        printf("[leds] LED_%-10s GPIO %2d → %s\n",
               LED_NAMES[led], LED_PINS[led], state ? "ON" : "OFF");
    }
}

// Obtener el estado actual del LED
int lib_leds_get(LedId led)
{
    if (!g_initialized || led < 0 || led >= LED_COUNT) return 0;

    pthread_mutex_lock(&g_lock);
    int state = g_leds[led].state;
    pthread_mutex_unlock(&g_lock);

    return state;
}

// Apagar LEDS y liberar los GPIO
void lib_leds_destroy(void)
{
    if (!g_initialized) return;
    printf("[leds] Apagando LEDs\n");
    for (int i = 0; i < LED_COUNT; i++) {
        gpio_write(g_pi, LED_PINS[i], 0);           // <-- gpio_write
        set_mode(g_pi, LED_PINS[i], PI_INPUT);       // <-- set_mode
        printf("[leds] LED_%-10s GPIO %2d → OFF\n", LED_NAMES[i], LED_PINS[i]);
    }
    pthread_mutex_destroy(&g_lock);
    g_initialized = 0;
}

// Sincronizar los LEDS
void lib_leds_sync_from_state(void)
{
    RobotState *rs = robot_state_get();
    if (!rs) return;

    pthread_mutex_lock(&rs->lock);
    int power     = rs->leds.power;
    int autonomous= rs->leds.autonomous;
    int manual    = rs->leds.manual;
    int obstacle  = rs->leds.obstacle;
    pthread_mutex_unlock(&rs->lock);

    printf("[leds] LEDS sincronizados\n");
    lib_leds_set(LED_POWER,      power);
    lib_leds_set(LED_AUTONOMOUS, autonomous);
    lib_leds_set(LED_MANUAL,     manual);
    lib_leds_set(LED_OBSTACLE,   obstacle);
}
