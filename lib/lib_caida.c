/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#include "lib_caida.h"

#include <pigpiod_if2.h>
#include <stdio.h>
#include <unistd.h>

static int g_pi = -1;

int caida_init(int pi) {
    const unsigned pines[] = { CAIDA_GPIO_IZQ, CAIDA_GPIO_DER };
    for (int i = 0; i < 2; i++) {
        if (set_mode(pi, pines[i], PI_INPUT) < 0 ||
            set_pull_up_down(pi, pines[i], PI_PUD_DOWN) < 0) {
            fprintf(stderr, "[caida] no se pudo configurar el GPIO %u\n", pines[i]);
            return -1;
        }
    }
    g_pi = pi;
    printf("[caida] sensores de desnivel en GPIO %d (izq) y %d (der)\n",
           CAIDA_GPIO_IZQ, CAIDA_GPIO_DER);
    return 0;
}

static int sin_piso(unsigned gpio) {
    if (gpio_read(g_pi, gpio) != CAIDA_NIVEL_SIN_PISO) return 0;
    usleep(200);
    return gpio_read(g_pi, gpio) == CAIDA_NIVEL_SIN_PISO;
}

int caida_leer(void) {
    if (g_pi < 0) return 0;
    int r = 0;
    if (sin_piso(CAIDA_GPIO_IZQ)) r |= CAIDA_IZQ;
    if (sin_piso(CAIDA_GPIO_DER)) r |= CAIDA_DER;
    return r;
}
