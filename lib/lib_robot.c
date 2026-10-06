/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#include "lib_robot.h"
#include "lib_caida.h"
#include "lib_imu.h"
#include "lib_leds.h"
#include "lib_motors.h"
#include "lib_odom.h"
#include "lib_radar.h"

#include <pigpiod_if2.h>
#include <stdio.h>

static int g_pi    = -1;
static int g_listo = 0;

int robot_init(void) {
    if (g_listo) return 0;

    g_pi = pigpio_start(NULL, NULL);
    if (g_pi < 0) {
        fprintf(stderr, "[librobot] no se pudo conectar a pigpiod (codigo %d). "
                        "Verificar que el servicio pigpiod este corriendo.\n", g_pi);
        return -1;
    }

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
    odom_reset();
    if (odom_arrancar() < 0)
        fprintf(stderr, "[librobot] no se pudo lanzar el hilo de la odometria\n");

    if (radar_iniciar(g_pi) < 0)
        fprintf(stderr, "[librobot] el radar no pudo arrancar: sin sensores de proximidad\n");

    g_listo = 1;
    return 0;
}

void robot_shutdown(void) {
    if (!g_listo) return;

    /* El radar primero: deja de mover el servo y de disparar el sensor. */
    radar_detener();
    odom_parar();
    motores_detener();
    imu_cerrar();

    lib_leds_destroy();
    pigpio_stop(g_pi);

    g_pi    = -1;
    g_listo = 0;
}

int robot_activo(void) {
    return g_listo;
}

double robot_distancia_frontal(void)   { return radar_distancia(RADAR_FRENTE); }
double robot_distancia_izquierda(void) { return radar_distancia(180);          }
double robot_distancia_derecha(void)   { return radar_distancia(0);            }
