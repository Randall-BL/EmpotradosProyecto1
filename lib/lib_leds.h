/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#ifndef LIB_LEDS_H
#define LIB_LEDS_H

// Pines GPIO BCM 
#define LED_GPIO_POWER       16   /* GPIO 16 — Pin fisico  */
#define LED_GPIO_AUTONOMOUS  20   /* GPIO 20 — Pin fisico  */
#define LED_GPIO_MANUAL      21   /* GPIO 21 — Pin fisico  */
#define LED_GPIO_OBSTACLE    26   /* GPIO 26 — Pin fisico  */

// ID del LED
typedef enum {
    LED_POWER      = 0,
    LED_AUTONOMOUS = 1,
    LED_MANUAL     = 2,
    LED_OBSTACLE   = 3,
    LED_COUNT      = 4
} LedId;

// Inicializar la biblioteca y pines GPIO
int lib_leds_init(int pi);

// Enciende (state=1) o apaga (state=0) un LED por su ID
void lib_leds_set(LedId led, int state);

// Obtener estado actual 1 | 0
int  lib_leds_get(LedId led);

// Apaga los LED y libera los GPIO 
void lib_leds_destroy(void);

// Sincronizar los LEDS
void lib_leds_sync_from_state(void);

#endif
