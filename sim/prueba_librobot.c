/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

/*
 * Programa de prueba de la biblioteca dinamica (issue #19).
 *
 * Ejercita cada modulo de librobot contra el robot simulado y compara el
 * resultado con la verdad del mundo virtual. No necesita la Raspberry: corre
 * en la laptop con ./construir.sh && ./prueba_librobot.
 *
 * Devuelve 0 si todas las comprobaciones pasan.
 */

#include "mundo.h"

#include "lib_robot.h"
#include "lib_motors.h"
#include "lib_leds.h"
#include "lib_odom.h"

#include <math.h>
#include <stdio.h>
#include <unistd.h>

static int g_fallos = 0;

static void verificar(const char *que, int ok) {
    printf("  [%s] %s\n", ok ? "OK " : "MAL", que);
    if (!ok) g_fallos++;
}

/* Deja correr la simulacion mientras la odometria integra, como hace el
   servidor durante una maniobra. */
static void andar(int ms) {
    for (int t = 0; t < ms; t += 50) {
        usleep(50000);
        odom_update();
    }
}

static void titulo(const char *t) {
    printf("\n== %s ==\n", t);
}

int main(void) {
    printf("Prueba de librobot contra el robot simulado\n");

    titulo("Arranque");
    verificar("robot_init() abre la sesion con el hardware", robot_init() == 0);
    verificar("robot_activo() lo confirma", robot_activo() == 1);

    titulo("Sensores de proximidad");
    double d_frente = robot_distancia_frontal();
    double d_izq    = robot_distancia_izquierda();
    double d_der    = robot_distancia_derecha();
    printf("  frente %.1f cm | izquierda %.1f cm | derecha %.1f cm\n", d_frente, d_izq, d_der);

    /* El robot arranca en el centro de la sala mirando al norte: 200 cm a cada
       pared, menos donde hay un mueble. Se compara contra la verdad del mundo. */
    verificar("la lectura frontal coincide con el mundo",
              fabs(d_frente - mundo_distancia(0.0)) < 2.0);
    verificar("la lectura izquierda coincide con el mundo",
              fabs(d_izq - mundo_distancia(-90.0)) < 2.0);
    verificar("la lectura derecha coincide con el mundo",
              fabs(d_der - mundo_distancia(90.0)) < 2.0);

    titulo("LEDs indicadores");
    lib_leds_set(LED_POWER, 1);
    verificar("el LED de encendido queda prendido", lib_leds_get(LED_POWER) == 1);
    lib_leds_set(LED_OBSTACLE, 1);
    lib_leds_set(LED_OBSTACLE, 0);
    verificar("el LED de obstaculo se apaga", lib_leds_get(LED_OBSTACLE) == 0);

    titulo("Motores: movimientos basicos");
    double x0, y0, r0, x1, y1, r1;

    odom_reset();
    mundo_pose(&x0, &y0, &r0);
    motores_avanzar(200);
    andar(1000);
    motores_detener();
    mundo_pose(&x1, &y1, &r1);
    printf("  avanzo %.1f cm hacia el norte\n", y1 - y0);
    verificar("motores_avanzar mueve al robot hacia adelante", (y1 - y0) > 5.0);

    mundo_pose(&x0, &y0, &r0);
    motores_retroceder(200);
    andar(600);
    motores_detener();
    mundo_pose(&x1, &y1, &r1);
    verificar("motores_retroceder lo devuelve", (y1 - y0) < -2.0);

    mundo_pose(&x0, &y0, &r0);
    motores_girar_derecha(200);
    andar(600);
    motores_detener();
    mundo_pose(&x1, &y1, &r1);
    printf("  giro de %.0f a %.0f grados\n", r0, r1);
    verificar("motores_girar_derecha aumenta el rumbo", fmod(r1 - r0 + 360.0, 360.0) > 5.0);

    titulo("Motores: control diferencial");
    int vi, vd;
    motores_set(200, 120);
    motores_get(&vi, &vd);
    verificar("motores_set guarda la velocidad de cada motor", vi == 200 && vd == 120);

    mundo_pose(&x0, &y0, &r0);
    andar(800);
    motores_detener();
    mundo_pose(&x1, &y1, &r1);
    double giro = fmod(r1 - r0 + 360.0, 360.0);
    double avance = hypot(x1 - x0, y1 - y0);
    printf("  curva: avanzo %.1f cm y giro %.0f grados\n", avance, giro);
    verificar("con el motor izquierdo mas rapido describe una curva a la derecha",
              avance > 5.0 && giro > 2.0 && giro < 180.0);

    motores_set(300, -400);
    motores_get(&vi, &vd);
    verificar("motores_set satura en +-255", vi == 255 && vd == -255);
    motores_detener();

    motores_curva(200, 100);
    motores_get(&vi, &vd);
    verificar("motores_curva(v, 100) detiene la llanta interior", vi == 200 && vd == 0);
    motores_detener();

    titulo("Odometria");

    /* Los dos marcos de referencia tienen que arrancar juntos: la odometria
       cuenta desde (0,0) mirando al norte, asi que el mundo tambien vuelve
       ahi. Sin esto se compararian desplazamientos girados uno respecto del
       otro por el rumbo que traia el robot de las pruebas anteriores. */
    mundo_init();
    odom_reset();
    mundo_pose(&x0, &y0, &r0);

    motores_avanzar(200);  andar(1500);
    motores_girar_derecha(200); andar(700);
    motores_avanzar(200);  andar(1500);
    motores_detener();     andar(100);

    double ox, oy, orumbo;
    odom_get(&ox, &oy, &orumbo);
    mundo_pose(&x1, &y1, &r1);

    /* La odometria estima desde el origen del reset; el mundo, desde donde
       estaba el robot. Se compara el desplazamiento, no la posicion absoluta. */
    double real_dx = x1 - x0, real_dy = y1 - y0;
    double error = hypot(ox - real_dx, oy - real_dy);
    double recorrido = odom_distancia_recorrida();

    printf("  odometria : dx %6.1f  dy %6.1f  rumbo %5.0f\n", ox, oy, orumbo);
    printf("  mundo real: dx %6.1f  dy %6.1f  rumbo %5.0f\n", real_dx, real_dy,
           fmod(r1 - r0 + 360.0, 360.0));
    printf("  error de posicion: %.1f cm sobre %.1f cm recorridos (%.0f%%)\n",
           error, recorrido, recorrido > 0 ? error / recorrido * 100.0 : 0.0);

    verificar("la odometria acumulo camino recorrido", recorrido > 10.0);
    verificar("el error de la navegacion a la estima se mantiene bajo el 20%",
              recorrido > 0 && error / recorrido < 0.20);

    titulo("Cierre");
    robot_shutdown();
    verificar("robot_shutdown cierra la sesion", robot_activo() == 0);
    motores_get(&vi, &vd);
    verificar("los motores quedan detenidos", vi == 0 && vd == 0);

    printf("\nRecorrido del robot en la sala simulada:\n\n");
    mundo_dibujar();

    printf("\n%s\n", g_fallos == 0 ? "TODAS LAS PRUEBAS PASARON"
                                   : "HAY PRUEBAS FALLIDAS");
    return g_fallos == 0 ? 0 : 1;
}
