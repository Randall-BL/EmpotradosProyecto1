/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 */

/* Identificadores LedId, pines LED_GPIO_* y prototipos del modulo. */
#include "lib_leds.h"
/* Estado global del robot: lo usa lib_leds_sync_from_state(). */
#include "robot_state.h" 
/* printf y fprintf, para los mensajes que van al journal. */
#include <stdio.h>
/* memset. */
#include <string.h>
/* Mutex que protege la tabla de estados. */
#include <pthread.h>
/* Cliente de pigpiod: set_mode y gpio_write. */
#include <pigpiod_if2.h>

/* Sesion con pigpiod que entrega lib_leds_init(). */
static int g_pi = -1;

/* ══════════════════════════════════════════════════════════
   Estado interno de la biblioteca
══════════════════════════════════════════════════════════ */

/* Nombre de cada LED para los mensajes, en el orden del enum LedId. */
static const char *LED_NAMES[LED_COUNT] = {
    "POWER", "AUTONOMOUS", "MANUAL", "OBSTACLE"
};

/* GPIO de cada LED, en el mismo orden: el LedId sirve de indice. */
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

/* Ultimo estado ordenado a cada LED. */
static LedEntry g_leds[LED_COUNT];
/* Protege g_leds: lo leen y lo escriben varios hilos. */
static pthread_mutex_t g_lock;
/* 1 cuando lib_leds_init() ya corrio. Evita inicializar dos veces y usar el
   modulo sin inicializar. */
static int g_initialized = 0;

/* ══════════════════════════════════════════════════════════
   API publica para los LEDS (AHORA USANDO PIGPIO)
══════════════════════════════════════════════════════════ */

/* Configura los cuatro pines como salidas apagadas y enciende el LED de
   sistema. pi: sesion con pigpiod. Devuelve 0 si quedo listo y -1 si no pudo
   crear el mutex. */
int lib_leds_init(int pi)  
{
    /* Llamarla dos veces no es un error: la segunda no hace nada. */
    if (g_initialized) return 0;
    g_pi = pi;  //  guardar handle

    /* En la tabla todos los LEDs parten apagados. */
    memset(g_leds, 0, sizeof(g_leds));
    /* Crea el mutex; si falla, la biblioteca no puede usarse. */
    if (pthread_mutex_init(&g_lock, NULL) != 0) {
        fprintf(stderr, "[leds] ERROR: mutex init failed\n");
        return -1;
    }

    printf("[leds] Inicializando pines GPIOs (via pigpiod)\n");

    /* Recorre los cuatro LEDs. */
    for (int i = 0; i < LED_COUNT; i++) {
        set_mode(g_pi, LED_PINS[i], PI_OUTPUT);  // set_mode en lugar de gpioSetMode
        gpio_write(g_pi, LED_PINS[i], 0);         //  gpio_write en lugar de gpioWrite
        printf("[leds] GPIO %d configurado para LED_%s\n", LED_PINS[i], LED_NAMES[i]);
    }

    /* Desde aqui lib_leds_set() ya acepta ordenes. */
    g_initialized = 1;
    /* El LED de encendido indica que el sistema esta arriba. */
    lib_leds_set(LED_POWER, 1);
    printf("[leds] Inicializado GPIOs OK\n");
    return 0;
}

// Encender o Apagar el LED
void lib_leds_set(LedId led, int state)
{
    /* Ignora la orden si el modulo no esta inicializado o si el identificador
       no existe. */
    if (!g_initialized || led < 0 || led >= LED_COUNT) return;
    /* Normaliza: cualquier valor distinto de cero cuenta como encendido. */
    state = state ? 1 : 0;

    /* Seccion critica: lee el estado anterior y guarda el nuevo sin que otro
       hilo se cruce. */
    pthread_mutex_lock(&g_lock);
    int prev = g_leds[led].state;
    g_leds[led].state = state;
    pthread_mutex_unlock(&g_lock);

    /* Aplica el nivel al pin. Va fuera del mutex para no retenerlo mientras se
       habla con pigpiod. */
    gpio_write(g_pi, LED_PINS[led], state);  // <-- gpio_write

    /* Solo se anuncia en el journal cuando el LED cambia de estado. */
    if (prev != state) {
        printf("[leds] LED_%-10s GPIO %2d → %s\n",
               LED_NAMES[led], LED_PINS[led], state ? "ON" : "OFF");
    }
}

// Obtener el estado actual del LED
int lib_leds_get(LedId led)
{
    /* Fuera de rango o sin inicializar se informa como apagado. */
    if (!g_initialized || led < 0 || led >= LED_COUNT) return 0;

    /* Copia el estado bajo el mutex y lo devuelve. */
    pthread_mutex_lock(&g_lock);
    int state = g_leds[led].state;
    pthread_mutex_unlock(&g_lock);

    return state;
}

// Apagar LEDS y liberar los GPIO
void lib_leds_destroy(void)
{
    /* Si nunca se inicializo no hay nada que apagar. */
    if (!g_initialized) return;
    printf("[leds] Apagando LEDs\n");
    /* Cada LED se apaga y su pin vuelve a ser entrada, el reposo de un GPIO. */
    for (int i = 0; i < LED_COUNT; i++) {
        gpio_write(g_pi, LED_PINS[i], 0);           // <-- gpio_write
        set_mode(g_pi, LED_PINS[i], PI_INPUT);       // <-- set_mode
        printf("[leds] LED_%-10s GPIO %2d → OFF\n", LED_NAMES[i], LED_PINS[i]);
    }
    /* Libera el mutex y marca el modulo como no inicializado. */
    pthread_mutex_destroy(&g_lock);
    g_initialized = 0;
}

// Sincronizar los LEDS
void lib_leds_sync_from_state(void)
{
    /* Estado global que mantiene el servidor. */
    RobotState *rs = robot_state_get();
    /* Sin estado global no hay de donde copiar. */
    if (!rs) return;

    /* Copia los cuatro valores bajo el mutex del estado, para no retenerlo
       mientras se escriben los pines. */
    pthread_mutex_lock(&rs->lock);
    int power     = rs->leds.power;
    int autonomous= rs->leds.autonomous;
    int manual    = rs->leds.manual;
    int obstacle  = rs->leds.obstacle;
    pthread_mutex_unlock(&rs->lock);

    printf("[leds] LEDS sincronizados\n");
    /* Lleva cada LED fisico al valor que dice el estado. */
    lib_leds_set(LED_POWER,      power);
    lib_leds_set(LED_AUTONOMOUS, autonomous);
    lib_leds_set(LED_MANUAL,     manual);
    lib_leds_set(LED_OBSTACLE,   obstacle);
}
