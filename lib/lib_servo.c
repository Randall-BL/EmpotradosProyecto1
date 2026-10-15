/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 */

/*
 * Mueve el servo de 180 grados que orienta el sensor ultrasonico. Las
 * constantes estan en lib_servo.h:
 *
 * - SERVO_GPIO = 25: Pin de la senal
 * - SERVO_PULSO_0_US / _180_US = 500 / 2500: Pulso para 0 grados y para 180
 *   grados
 * - SERVO_MS_POR_GRADO = 1.7: Lo que tarda el servo por grado a toda velocidad
 * - SERVO_DIVISOR_VELOCIDAD = 3: Se mueve a un tercio de su velocidad
 * - SERVO_PERIODO_MS = 20: Cada cuanto recibe un pulso (50 Hz)
 * - SERVO_ASENTAMIENTO_MS = 30: Espera extra para que deje de vibrar
 *
 * Variables: g_pi y g_angulo (ultimo angulo ordenado; -1 = no se sabe donde
 * esta).
 */

/* Constantes SERVO_* y prototipos del modulo. */
#include "lib_servo.h"
/* Cliente de pigpiod: set_servo_pulsewidth genera los pulsos del servo. */
#include <pigpiod_if2.h>
/* usleep(), para espaciar los escalones de la rampa. */
#include <unistd.h>

/* Sesion con pigpiod; en -1 el modulo no esta inicializado. */
static int g_pi     = -1;
static int g_angulo = -1;   /* -1: posicion desconocida */

/* Guarda la sesion y deja el pin del servo como salida en bajo. No manda
   ningun pulso: el servo no se mueve hasta el primer servo_mover(). */
void servo_init(int pi) {
    /*
     * Guarda la sesion, marca la posicion como desconocida (g_angulo = -1) y
     * deja el pin como salida en 0. No mueve el servo.
     */

    g_pi     = pi;
    /* Todavia no se sabe hacia donde apunta el servo. */
    g_angulo = -1;
    /* El pin de senal es una salida; en bajo el servo no recibe pulsos. */
    set_mode(g_pi, SERVO_GPIO, PI_OUTPUT);
    gpio_write(g_pi, SERVO_GPIO, 0);
}

/* Convierte un angulo en el ancho de pulso que lo produce, en
   microsegundos: interpolacion lineal entre el pulso de 0 grados y el de
   180, redondeada al entero mas cercano. */
static unsigned pulso_de(double grados) {
    /*
     * Convierte grados en el ancho de pulso en microsegundos, con una regla de
     * tres:
     *
     *     pulso = 500 + (2500 - 500) x grados / 180
     *
     * Grados   Pulso
     * ------   -------
     * 0        500 us
     * 45       1000 us
     * 90       1500 us
     * 180      2500 us
     *
     * El + 0.5 antes de convertir a entero sirve para redondear en vez de
     * truncar: (unsigned)(7.6 + 0.5) = 8.
     */

    return SERVO_PULSO_0_US +
        (unsigned)((SERVO_PULSO_180_US - SERVO_PULSO_0_US) * grados / 180.0 + 0.5);
}

/* Mueve el servo en rampa al angulo pedido y devuelve los milisegundos que
   faltan para que quede quieto. Descripcion completa en lib_servo.h. */
int servo_mover(int grados) {
    /*
     * Lleva el servo a un angulo despacio (en rampa) y devuelve cuantos
     * milisegundos faltan para que quede quieto.
     *
     * 1. Si no hay sesion, devuelve -1. Si el angulo esta fuera de 0-180, lo
     *    recorta.
     * 2. Si no se sabe donde esta (g_angulo < 0, primer movimiento): manda
     *    directo el pulso final y devuelve 180 x 1.7 + 30 = 336 ms (el peor
     *    caso: media vuelta).
     * 3. Si se sabe: calcula la rampa.
     *    - recorrido = grados a mover (sin signo).
     *    - ms_total = recorrido x 1.7 x 3.
     *    - pasos = ms_total / 20, redondeado hacia arriba (el + 0.999), al
     *      menos 1.
     * 4. En cada paso i calcula un angulo intermedio desde + (destino - desde)
     *    x i / pasos, manda su pulso y duerme ms_total / pasos. Tras el ultimo
     *    no duerme.
     * 5. Guarda el angulo final y devuelve lo que tarda el ultimo tramo + 30
     *    ms.
     *
     * Ejemplo, de 0 grados a 30 grados:
     *
     *     ms_total = 30 x 1.7 x 3 = 153 ms
     *     pasos    = 153 / 20 -> 8
     *     angulos  = 3.75, 7.5, 11.25, ... 30
     *     cada paso duerme ~ 19 ms
     *     devuelve 30/8 x 1.7 + 30 ~ 36 ms
     */

    /* Sin sesion con pigpiod no se puede generar el pulso. */
    if (g_pi < 0) return -1;

    /* Satura el angulo al recorrido fisico del servo. */
    if (grados < 0)   grados = 0;
    if (grados > 180) grados = 180;

    /* Desde una posicion desconocida no hay rampa posible: un salto, y se
       asume el peor caso, media vuelta a toda velocidad. */
    if (g_angulo < 0) {
        /* Manda de una vez el pulso del angulo final; si pigpiod lo rechaza, error. */
        if (set_servo_pulsewidth(g_pi, SERVO_GPIO, pulso_de(grados)) != 0) return -1;
        /* Ahora si se conoce la posicion. */
        g_angulo = grados;
        /* Espera estimada: 180 grados a la velocidad propia del servo, mas el
           asentamiento. */
        return (int)(180 * SERVO_MS_POR_GRADO) + SERVO_ASENTAMIENTO_MS;
    }

    /* Rampa: un pulso intermedio por periodo de 50 Hz. Cada escalon es lo que
       el servo recorre en ese periodo a la velocidad reducida, asi que llega
       al siguiente justo cuando le llega el pulso nuevo. */
    /* Angulo de partida. */
    int    desde     = g_angulo;
    /* Grados por recorrer, sin signo. */
    int    recorrido = grados > desde ? grados - desde : desde - grados;
    /* Duracion total de la rampa: los grados por los ms que tarda cada uno,
       alargados por el divisor de velocidad. */
    double ms_total  = recorrido * SERVO_MS_POR_GRADO * SERVO_DIVISOR_VELOCIDAD;
    /* Cantidad de escalones: uno por periodo de 20 ms, redondeando hacia arriba. */
    int    pasos     = (int)(ms_total / SERVO_PERIODO_MS + 0.999);
    /* Al menos un escalon, aunque el recorrido sea de un grado. */
    if (pasos < 1) pasos = 1;

    /* Un pulso por escalon, del primero al ultimo. */
    for (int i = 1; i <= pasos; i++) {
        /* Angulo intermedio de este escalon: interpolacion lineal entre el origen
           y el destino. */
        double a = desde + (double)(grados - desde) * i / pasos;
        /* Pulso de ese angulo intermedio. */
        if (set_servo_pulsewidth(g_pi, SERVO_GPIO, pulso_de(a)) != 0) return -1;
        g_angulo = (int)(a + 0.5);   /* por si falla el siguiente pulso */
        /* Espera un escalon antes del siguiente pulso. Tras el ultimo no se espera
           aqui: lo que falta se le devuelve al llamador. */
        if (i < pasos) usleep((useconds_t)(ms_total / pasos * 1000.0));
    }
    /* La rampa termino: el angulo ordenado es el pedido. */
    g_angulo = grados;

    /* Tras el ultimo pulso falta el ultimo escalon, que el servo recorre a su
       velocidad propia, y el asentamiento. */
    return (int)((double)recorrido / pasos * SERVO_MS_POR_GRADO) + SERVO_ASENTAMIENTO_MS;
}

/* Devuelve el ultimo angulo ordenado, o -1 si la posicion es desconocida. */
int servo_angulo(void) {
    /* Devuelve g_angulo: el ultimo angulo ordenado, o -1. */

    return g_angulo;
}

/* Deja de mandar pulsos: el servo queda sin fuerza. */
void servo_liberar(void) {
    /*
     * Manda pulso 0, que para pigpiod significa "deja de mandar pulsos". El
     * servo queda suelto, sin fuerza, y la posicion vuelve a ser desconocida.
     */

    /* Sin sesion no hay nada que liberar. */
    if (g_pi < 0) return;
    /* Un ancho de pulso de 0 le dice a pigpiod que apague los pulsos del pin. */
    set_servo_pulsewidth(g_pi, SERVO_GPIO, 0);
    /* Sin pulsos el servo puede moverse solo: la posicion vuelve a ser
       desconocida. */
    g_angulo = -1;
}
