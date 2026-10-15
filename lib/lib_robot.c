/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 */

/*
 * La fachada: el unico punto de entrada que necesita el servidor para encender
 * y apagar todo el hardware. Incluye los headers de los demas modulos y el de
 * pigpio.
 *
 * Guarda dos variables privadas:
 *
 * - g_pi: el numero de sesion con pigpiod. Todos los modulos lo reciben en su
 *   init.
 * - g_listo: 1 cuando ya se inicializo. Evita inicializar dos veces.
 *
 * Para entender: static int g_pi = -1; fuera de una funcion significa:
 * variable global, pero solo visible dentro de este archivo. Es la forma de C
 * de tener datos privados. El prefijo g_ es una convencion del proyecto para
 * "global".
 */

/* Prototipos de la fachada. */
#include "lib_robot.h"
/* Los modulos que la fachada inicializa y detiene, uno por dispositivo. */
#include "lib_caida.h"
#include "lib_imu.h"
#include "lib_leds.h"
#include "lib_motors.h"
#include "lib_odom.h"
#include "lib_radar.h"

/* pigpio_start y pigpio_stop: abrir y cerrar la sesion con el demonio. */
#include <pigpiod_if2.h>
/* fprintf, para los avisos. */
#include <stdio.h>

/* Sesion con pigpiod, compartida con todos los modulos. */
static int g_pi    = -1;
/* 1 cuando el hardware ya esta inicializado. */
static int g_listo = 0;

/* Inicializa todo el hardware en el orden correcto. Ver lib_robot.h. */
int robot_init(void) {
    /*
     * Enciende todo el hardware en el orden correcto. Devuelve 0 si quedo
     * listo o -1 si no pudo hablar con pigpiod.
     *
     * 1. if (g_listo) return 0; - si ya esta encendido, no hace nada.
     * 2. g_pi = pigpio_start(NULL, NULL); - se conecta al demonio pigpiod en
     *    esta misma maquina (NULL = valores por defecto). Si devuelve un
     *    numero negativo, imprime el error y sale con -1. Es el unico fallo
     *    que detiene todo.
     * 3. motores_init(g_pi) - primero los motores, para que las llantas queden
     *    frenadas antes de cualquier otra cosa.
     * 4. caida_init y lib_leds_init - si fallan, solo imprimen un aviso en
     *    stderr; el robot sigue.
     * 5. imu_init y, si encontro el sensor, imu_calibrar(100) - mide el "cero"
     *    del MPU mientras el robot esta quieto.
     * 6. odom_reset() y odom_arrancar() - pone la posicion en (0,0) y lanza el
     *    hilo de odometria.
     * 7. radar_iniciar(g_pi) - configura sensor y servo y lanza el hilo del
     *    barrido. Va despues de la odometria porque cada lectura del radar
     *    guarda la posicion del robot.
     * 8. g_listo = 1; return 0;
     *
     * Para entender: fprintf(stderr, "...") escribe en la salida de errores.
     * En el robot, systemd manda todo eso al journal (journalctl -u
     * robot-server).
     */

    /* Llamarla dos veces no reinicializa nada. */
    if (g_listo) return 0;

    /* Conecta con pigpiod en esta misma maquina y en el puerto por defecto. */
    g_pi = pigpio_start(NULL, NULL);
    if (g_pi < 0) {
        fprintf(stderr, "[librobot] no se pudo conectar a pigpiod (codigo %d). "
                        "Verificar que el servicio pigpiod este corriendo.\n", g_pi);
        return -1;
    }

    /* Los motores primero: quedan frenados antes de tocar nada mas. */
    motores_init(g_pi);

    /* Los sensores de desnivel son opcionales: sin ellos el robot navega
       igual, solo que no se detiene ante una grada. */
    if (caida_init(g_pi) < 0)
        fprintf(stderr, "[librobot] sin deteccion de desnivel\n");

    /* Los LEDs son indicadores: si fallan, el robot igual navega. */
    if (lib_leds_init(g_pi) < 0)
        fprintf(stderr, "[librobot] los LEDs indicadores no se pudieron inicializar\n");

    /* El MPU se calibra antes de que nada se mueva: con los motores recien
       detenidos y el servo todavia sin pulsos, el robot esta quieto. Sin MPU
       la odometria cae al modelo de los motores, que ya funcionaba solo. */
    if (imu_init(g_pi) == 0) {
        if (imu_calibrar(IMU_MUESTRAS_CALIBRACION) < 0)
            fprintf(stderr, "[librobot] el MPU-6050 no se pudo calibrar\n");
    } else {
        fprintf(stderr, "[librobot] MPU-6050 no disponible: la velocidad y el "
                        "rumbo salen del modelo de los motores\n");
    }

    /* Primero la odometria y despues el radar: cada lectura del radar se
       guarda con la pose del robot en el instante de medir. */
    /* Posicion en el origen e hilo de integracion corriendo. */
    odom_reset();
    if (odom_arrancar() < 0)
        fprintf(stderr, "[librobot] no se pudo lanzar el hilo de la odometria\n");

    /* Sensor, servo e hilo de barrido. */
    if (radar_iniciar(g_pi) < 0)
        fprintf(stderr, "[librobot] el radar no pudo arrancar: sin sensores de proximidad\n");

    /* Solo ahora la fachada se declara lista. */
    g_listo = 1;
    return 0;
}

/* Detiene todo en orden inverso y cierra la sesion. Ver lib_robot.h. */
void robot_shutdown(void) {
    /*
     * Apaga todo en orden inverso. Si nunca se encendio (!g_listo), sale sin
     * hacer nada.
     *
     * Orden: radar (deja de mover el servo), odometria, motores frenados,
     * cierra el I2C del MPU, apaga los LEDs y cierra la sesion con
     * pigpio_stop. Al final vuelve g_pi a -1 y g_listo a 0, para que se pueda
     * volver a encender.
     */

    /* Sin inicializar no hay nada que cerrar. */
    if (!g_listo) return;

    /* El radar primero: deja de mover el servo y de disparar el sensor. */
    radar_detener();
    /* Despues la odometria, los motores y el bus del MPU. */
    odom_parar();
    motores_detener();
    imu_cerrar();

    /* Apaga los LEDs y cierra la sesion con pigpiod. */
    lib_leds_destroy();
    pigpio_stop(g_pi);

    /* Vuelve al estado de "sin inicializar". */
    g_pi    = -1;
    g_listo = 0;
}

/* 1 si robot_init() termino bien y no se ha llamado a robot_shutdown(). */
int robot_activo(void) {
    /* Devuelve g_listo: 1 si el hardware esta encendido, 0 si no. */

    return g_listo;
}

/* Atajos a tres angulos del radar: 90 al frente, 180 a la izquierda y 0 a
   la derecha. */
/*
 * Atajo: la ultima distancia del radar a 90 grados (al frente). Llama a
 * radar_distancia(RADAR_FRENTE).
 */
double robot_distancia_frontal(void)   { return radar_distancia(RADAR_FRENTE); }
/* Atajo: la ultima distancia del radar a 180 grados (izquierda). */
double robot_distancia_izquierda(void) { return radar_distancia(180);          }
/* Atajo: la ultima distancia del radar a 0 grados (derecha). */
double robot_distancia_derecha(void)   { return radar_distancia(0);            }
