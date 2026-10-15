/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 */

/*
 * Lee el sensor ultrasonico HC-SR04. El header define SensorUltrasonico, una
 * estructura con los dos pines del sensor (pinTrigger y pinEcho). Asi el mismo
 * codigo serviria para varios sensores; en el robot hay uno solo, el del
 * radar.
 *
 * Guarda g_pi, la sesion con pigpiod.
 */

/* Tipo SensorUltrasonico y prototipos del modulo. */
#include "lib_sensors.h"
/* Cliente de pigpiod: GPIO y reloj de microsegundos (get_current_tick). */
#include <pigpiod_if2.h>
/* usleep(), para el pulso de disparo. */
#include <unistd.h>

/* Sesion con pigpiod, la misma para todo sensor que se inicialice. */
static int g_pi = -1;

/* Prepara un HC-SR04: guarda sus dos pines en la estructura y los configura.
   pi: sesion con pigpiod. sensor: estructura a llenar.
   trigger, echo: GPIO (BCM) de cada senal. */
void sensor_init(int pi, SensorUltrasonico* sensor, int trigger, int echo) {
    /*
     * Prepara un sensor. Recibe la sesion, un puntero a la estructura a llenar
     * y los dos pines.
     *
     * 1. Guarda la sesion en g_pi.
     * 2. Escribe los pines en la estructura: sensor->pinTrigger = trigger;.
     * 3. TRIG como salida (la Raspberry le da la orden de medir) y ECHO como
     *    entrada (el sensor contesta).
     * 4. Deja TRIG en 0, en reposo.
     *
     * Para entender: sensor->pinTrigger es lo mismo que (*sensor).pinTrigger:
     * el campo de la estructura a la que apunta el puntero.
     */

    /* Guarda la sesion para las lecturas posteriores. */
    g_pi = pi;
    /* Recuerda que pin dispara y cual escucha. */
    sensor->pinTrigger = trigger;
    sensor->pinEcho = echo;
    /* TRIG es salida: la Raspberry Pi le ordena al sensor emitir. */
    set_mode(g_pi, sensor->pinTrigger, PI_OUTPUT);
    /* ECHO es entrada: por ahi el sensor avisa cuanto tardo el eco. Llega por
       un divisor de tension, porque el sensor entrega 5 V. */
    set_mode(g_pi, sensor->pinEcho, PI_INPUT);
    /* TRIG en reposo, en bajo: el sensor no emite hasta que se le pida. */
    gpio_write(g_pi, sensor->pinTrigger, 0);
}

/* Hace una medicion y devuelve la distancia en centimetros, o -1.0 si no
   hubo eco. Bloquea mientras mide: hasta 50 ms en cada espera. */
double sensor_leer_distancia(SensorUltrasonico* sensor) {
    /*
     * Mide una distancia en centimetros, o devuelve -1 si no hubo eco. Bloquea
     * mientras mide.
     *
     * 1. Disparo: TRIG a 1, espera 10 us (usleep(10)), TRIG a 0. El sensor
     *    emite un chillido ultrasonico.
     * 2. Espera que ECHO suba: un ciclo while lee ECHO hasta que vale 1. En
     *    cada vuelta guarda la hora en inicio. Si pasan mas de 50 ms (50 000
     *    us), devuelve -1: el sensor no respondio.
     * 3. Mide cuanto dura ECHO en 1: otro while hasta que ECHO vuelve a 0,
     *    guardando la hora en fin. Si pasa de 50 ms, devuelve -1: el obstaculo
     *    esta demasiado lejos.
     * 4. Calcula: duracion = fin - inicio (en us), y
     *
     *     distancia = duracion x 34300 / 2 000 000
     *
     * 34 300 cm/s es la velocidad del sonido; se divide entre 2 porque el
     * sonido va y vuelve, y entre 1 000 000 para pasar de us a s.
     *
     * Ejemplo: ECHO estuvo en alto 1166 us -> 1166 x 34300 / 2 000 000 ~ 20
     * cm.
     *
     * Para entender: uint32_t es un entero sin signo de 32 bits. El reloj de
     * pigpiod da la vuelta (vuelve a 0) cada ~72 minutos; con numeros sin
     * signo, fin - inicio sigue dando bien aunque haya dado la vuelta en
     * medio.
     */

    /* Pulso de disparo: TRIG en alto durante 10 us. Con eso el HC-SR04 emite
       una rafaga de ultrasonido. */
    gpio_write(g_pi, sensor->pinTrigger, 1);
    usleep(10); 
    gpio_write(g_pi, sensor->pinTrigger, 0);

    /* Reloj de pigpiod, en microsegundos. "timeout" guarda el instante inicial
       para saber cuanto se lleva esperando. */
    uint32_t inicio = get_current_tick(g_pi);  // <-- get_current_tick
    uint32_t timeout = inicio;

    /* Espera a que ECHO suba: es cuando sale la rafaga. "inicio" se actualiza
       en cada vuelta y queda con el instante del flanco de subida. */
    while (gpio_read(g_pi, sensor->pinEcho) == 0) {
        inicio = get_current_tick(g_pi);
        /* Si en 50 ms no subio, el sensor no respondio. */
        if (inicio - timeout > 50000) return -1.0;
    }
    /* Desde aqui se mide cuanto dura ECHO en alto. */
    uint32_t fin = get_current_tick(g_pi);
    /* Espera a que ECHO baje: el sensor lo baja cuando recibe el eco. */
    while (gpio_read(g_pi, sensor->pinEcho) == 1) {
        fin = get_current_tick(g_pi);
        /* Mas de 50 ms en alto es mas distancia de la que el sensor mide. */
        if (fin - inicio > 50000) return -1.0;
    }
    /* Ancho del pulso de ECHO: el tiempo de ida y vuelta del sonido. La resta
       sin signo sigue siendo correcta aunque el reloj de 32 bits de la vuelta. */
    uint32_t duracion = fin - inicio;
    /* Distancia = tiempo x velocidad del sonido / 2. Con el tiempo en
       microsegundos y 34300 cm/s: us x 34300 / 2 000 000 = cm. */
    return (duracion * 34300.0) / 2000000.0;
}
