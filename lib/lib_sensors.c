/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#include "lib_sensors.h"
#include <pigpiod_if2.h>
#include <unistd.h>

static int g_pi = -1;

void sensor_init(int pi, SensorUltrasonico* sensor, int trigger, int echo) {
    g_pi = pi;
    sensor->pinTrigger = trigger;
    sensor->pinEcho = echo;
    set_mode(g_pi, sensor->pinTrigger, PI_OUTPUT);
    set_mode(g_pi, sensor->pinEcho, PI_INPUT);
    gpio_write(g_pi, sensor->pinTrigger, 0);
}

double sensor_leer_distancia(SensorUltrasonico* sensor) {
    gpio_write(g_pi, sensor->pinTrigger, 1);
    usleep(10); 
    gpio_write(g_pi, sensor->pinTrigger, 0);

    uint32_t inicio = get_current_tick(g_pi);  // <-- get_current_tick
    uint32_t timeout = inicio;

    while (gpio_read(g_pi, sensor->pinEcho) == 0) {
        inicio = get_current_tick(g_pi);
        if (inicio - timeout > 50000) return -1.0;
    }
    uint32_t fin = get_current_tick(g_pi);
    while (gpio_read(g_pi, sensor->pinEcho) == 1) {
        fin = get_current_tick(g_pi);
        if (fin - inicio > 50000) return -1.0;
    }
    uint32_t duracion = fin - inicio;
    return (duracion * 34300.0) / 2000000.0;
}
