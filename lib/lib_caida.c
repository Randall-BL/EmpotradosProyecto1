/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 */

/* Pines CAIDA_GPIO_*, bits CAIDA_IZQ / CAIDA_DER y prototipos. */
#include "lib_caida.h"

/* Cliente de pigpiod: set_mode, set_pull_up_down y gpio_read. */
#include <pigpiod_if2.h>
/* printf y fprintf, para los mensajes del journal. */
#include <stdio.h>
/* usleep(), para separar las dos lecturas de cada sensor. */
#include <unistd.h>

/* Sesion con pigpiod; en -1 caida_leer() informa que hay piso. */
static int g_pi = -1;

/* Configura los dos GPIO como entradas con pull-down. Descripcion completa
   en lib_caida.h. */
int caida_init(int pi) {
    /* Los dos pines, para configurarlos con el mismo ciclo. */
    const unsigned pines[] = { CAIDA_GPIO_IZQ, CAIDA_GPIO_DER };
    for (int i = 0; i < 2; i++) {
        /* Entrada con pull-down interno: sin modulo conectado el pin se lee en
           bajo, que es "hay piso". Si pigpiod rechaza cualquiera de los dos pasos,
           error. */
        if (set_mode(pi, pines[i], PI_INPUT) < 0 ||
            set_pull_up_down(pi, pines[i], PI_PUD_DOWN) < 0) {
            fprintf(stderr, "[caida] no se pudo configurar el GPIO %u\n", pines[i]);
            return -1;
        }
    }
    /* La sesion se guarda solo cuando los dos pines quedaron bien: un fallo deja
       el modulo sin inicializar. */
    g_pi = pi;
    printf("[caida] sensores de desnivel en GPIO %d (izq) y %d (der)\n",
           CAIDA_GPIO_IZQ, CAIDA_GPIO_DER);
    return 0;
}

/* Lee un sensor con doble confirmacion. Devuelve 1 solo si las dos lecturas,
   separadas por 200 us, dicen que no hay piso. */
static int sin_piso(unsigned gpio) {
    /* Primera lectura: si ve piso, se termina aqui. */
    if (gpio_read(g_pi, gpio) != CAIDA_NIVEL_SIN_PISO) return 0;
    /* Espera breve, para que un pico de ruido de los motores ya haya pasado. */
    usleep(200);
    /* Segunda lectura: tiene que confirmar el desnivel. */
    return gpio_read(g_pi, gpio) == CAIDA_NIVEL_SIN_PISO;
}

/* Lee los dos sensores y devuelve los bits CAIDA_IZQ / CAIDA_DER. */
int caida_leer(void) {
    /* Sin inicializar se informa "hay piso", para no frenar el robot en falso. */
    if (g_pi < 0) return 0;
    /* Mascara de resultado, vacia mientras no se detecte nada. */
    int r = 0;
    /* Enciende el bit de cada lado que no ve piso. */
    if (sin_piso(CAIDA_GPIO_IZQ)) r |= CAIDA_IZQ;
    if (sin_piso(CAIDA_GPIO_DER)) r |= CAIDA_DER;
    return r;
}
