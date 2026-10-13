/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 */

/* Guarda de inclusion: el contenido se procesa una sola vez por archivo. */
#ifndef LIB_LEDS_H
#define LIB_LEDS_H

// Pines GPIO BCM 
/* Pines fisicos del conector de 40 pines: 36 (encendido), 38 (autonomo),
   40 (manual) y 37 (obstaculo). Ver docs/hardware-pinout.md. */
#define LED_GPIO_POWER       16   /* GPIO 16 — Pin fisico  */
#define LED_GPIO_AUTONOMOUS  20   /* GPIO 20 — Pin fisico  */
#define LED_GPIO_MANUAL      21   /* GPIO 21 — Pin fisico  */
#define LED_GPIO_OBSTACLE    26   /* GPIO 26 — Pin fisico  */

// ID del LED
typedef enum {
    /* Los valores sirven de indice en las tablas de lib_leds.c. */
    LED_POWER      = 0,
    LED_AUTONOMOUS = 1,
    LED_MANUAL     = 2,
    LED_OBSTACLE   = 3,
    /* No es un LED: es la cantidad de LEDs, el tamano de esas tablas. */
    LED_COUNT      = 4
} LedId;

// Inicializar la biblioteca y pines GPIO
/* pi: sesion con pigpiod. Devuelve 0 si quedo listo y -1 si fallo. */
int lib_leds_init(int pi);

// Enciende (state=1) o apaga (state=0) un LED por su ID
void lib_leds_set(LedId led, int state);

// Obtener estado actual 1 | 0
int  lib_leds_get(LedId led);

// Apaga los LED y libera los GPIO 
void lib_leds_destroy(void);

// Sincronizar los LEDS
/* Copia a los LEDs fisicos el estado que guarda robot_state. */
void lib_leds_sync_from_state(void);

#endif
