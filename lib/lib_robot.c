/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
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
    return g_listo;
}

/* Atajos a tres angulos del radar: 90 al frente, 180 a la izquierda y 0 a
   la derecha. */
double robot_distancia_frontal(void)   { return radar_distancia(RADAR_FRENTE); }
double robot_distancia_izquierda(void) { return radar_distancia(180);          }
double robot_distancia_derecha(void)   { return radar_distancia(0);            }
