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
#include "pigpiod_if2.h"

#include "lib_robot.h"
#include "lib_motors.h"
#include "lib_leds.h"
#include "lib_odom.h"
#include "lib_audio.h"
#include "lib_imu.h"
#include "lib_radar.h"
#include "lib_servo.h"
#include "lib_caida.h"

#include <math.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static int g_pruebas = 0;
static int g_fallos  = 0;

static void verificar(const char *que, int ok) {
    printf("  [%s] %s\n", ok ? "OK " : "MAL", que);
    g_pruebas++;
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

/* Espera, andando, a que el radar tome una lectura frontal nueva. */
static int esperar_lectura_frontal(int timeout_ms) {
    RadarLectura l[RADAR_N_ANGULOS];
    radar_lecturas(l, RADAR_N_ANGULOS);
    uint32_t antes = l[RADAR_FRENTE / RADAR_PASO_GRADOS].seq;
    for (int t = 0; t < timeout_ms; t += 50) {
        andar(50);
        radar_lecturas(l, RADAR_N_ANGULOS);
        if (l[RADAR_FRENTE / RADAR_PASO_GRADOS].seq > antes) return 0;
    }
    return -1;
}

int main(void) {
    printf("Prueba de librobot contra el robot simulado\n");

    titulo("Arranque");
    verificar("robot_init() abre la sesion con el hardware", robot_init() == 0);
    verificar("robot_activo() lo confirma", robot_activo() == 1);
    verificar("el MPU-6050 contesta en el bus I2C", imu_disponible() == 1);

    titulo("Sensores de desnivel");
    /* El robot arranca en el centro de la sala, lejos de la grada. */
    verificar("sobre el piso ningun sensor marca desnivel", caida_leer() == 0);
    verificar("en el centro de la sala hay piso", mundo_sin_piso(0.0, 0.0) == 0);

    titulo("MPU-6050");
    /* El sensor simulado trae un sesgo de fabrica (0.02 g y 1.5 grados/s);
       robot_init() lo calibro con el robot quieto. */
    ImuLectura m;
    int leyo = imu_leer(&m) == 0;
    printf("  en reposo: avance %+.2f cm/s2 | giro %+.3f grados/s | vertical %.3f g\n",
           m.avance_cm_s2, m.giro_dps, m.vertical_g);
    verificar("imu_leer() entrega una lectura", leyo);
    verificar("la calibracion quita el sesgo del acelerometro", leyo && fabs(m.avance_cm_s2) < 10.0);
    verificar("la calibracion quita el sesgo del giroscopo", leyo && fabs(m.giro_dps) < 0.5);
    verificar("el eje vertical mide la gravedad", leyo && fabs(m.vertical_g - 1.0) < 0.05);
    andar(100);
    verificar("la odometria usa el MPU", odom_usa_imu() == 1);

    titulo("Radar: HC-SR04 sobre el servo");
    verificar("el radar completa un barrido de 0 a 180 grados", radar_esperar_barrido(5000) == 0);

    RadarLectura l[RADAR_N_ANGULOS];
    int n = radar_lecturas(l, RADAR_N_ANGULOS);
    verificar("hay una lectura por cada angulo", n == RADAR_N_ANGULOS);

    /* El robot esta quieto en el centro de la sala mirando al norte: cada
       angulo del servo tiene que medir lo que hay en esa direccion. */
    int coinciden = 1;
    for (int i = 0; i < n; i++) {
        double real = mundo_distancia_desde(SIM_SENSOR_ADELANTE_CM, 90.0 - l[i].angulo);
        printf("  servo %3d grados -> %6.1f cm   (mundo %6.1f cm)\n",
               l[i].angulo, l[i].distancia_cm, real);
        if (real < 0.0 ? l[i].distancia_cm >= 0.0
                       : fabs(l[i].distancia_cm - real) > 2.0)
            coinciden = 0;
    }
    verificar("cada angulo mide lo que hay en el mundo en esa direccion", coinciden);

    verificar("robot_distancia_frontal() es el servo a 90",
              fabs(robot_distancia_frontal()   - mundo_distancia_desde(SIM_SENSOR_ADELANTE_CM,   0.0)) < 2.0);
    verificar("robot_distancia_izquierda() es el servo a 180",
              fabs(robot_distancia_izquierda() - mundo_distancia_desde(SIM_SENSOR_ADELANTE_CM, -90.0)) < 2.0);
    verificar("robot_distancia_derecha() es el servo a 0",
              fabs(robot_distancia_derecha()   - mundo_distancia_desde(SIM_SENSOR_ADELANTE_CM,  90.0)) < 2.0);

    /* La pared norte esta en y = 200: el eco frontal proyectado tiene que
       caer ahi, en la vertical del robot. */
    const RadarLectura *f = &l[RADAR_FRENTE / RADAR_PASO_GRADOS];
    double ox, oy, rr;
    radar_rayo(f, &ox, &oy, &rr);
    double px = ox + f->distancia_cm * sin(rr * M_PI / 180.0);
    double py = oy + f->distancia_cm * cos(rr * M_PI / 180.0);
    printf("  eco frontal en el mundo: (%.1f, %.1f)\n", px, py);
    verificar("radar_rayo() proyecta el eco frontal sobre la pared norte",
              fabs(px) < 3.0 && fabs(py - 200.0) < 3.0);

    titulo("Servo");
    radar_pausar(1);
    usleep(500000);            /* que el hilo termine el paso en curso */
    servo_mover(45);
    verificar("servo_mover(45) manda un pulso de 1000 us",
              get_servo_pulsewidth(1, SERVO_GPIO) == 1000);
    verificar("servo_angulo() recuerda el angulo ordenado", servo_angulo() == 45);
    servo_mover(-20);
    int p_min = get_servo_pulsewidth(1, SERVO_GPIO);
    servo_mover(200);
    int p_max = get_servo_pulsewidth(1, SERVO_GPIO);
    verificar("satura en 0 y 180 grados (500 y 2500 us)", p_min == 500 && p_max == 2500);
    /* De 180 a 0 a un tercio de la velocidad: 180 * 1.7 ms * 3 = 918 ms. */
    struct timespec ini, fin;
    clock_gettime(CLOCK_MONOTONIC, &ini);
    servo_mover(0);
    clock_gettime(CLOCK_MONOTONIC, &fin);
    double ms = (fin.tv_sec - ini.tv_sec) * 1e3 + (fin.tv_nsec - ini.tv_nsec) / 1e6;
    printf("  media vuelta en %.0f ms\n", ms);
    verificar("la media vuelta va a 1/3 de la velocidad del servo (~0.9 s)",
              ms > 850.0 && ms < 1100.0);
    radar_pausar(0);

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
#if MOTOR_VELOCIDAD_VARIABLE
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
#else
    /* Velocidad fija: cualquier orden distinta de cero va al maximo, asi que
       dos velocidades distintas van recto, y las curvas se hacen girando
       sobre el eje: una llanta adelante y la otra atras. */
    verificar("con velocidad fija, cualquier velocidad va al maximo",
              vi == MOTOR_PWM_MAX && vd == MOTOR_PWM_MAX);

    motores_curva(200, 50);
    motores_get(&vi, &vd);
    verificar("motores_curva gira con una llanta adelante y la otra atras",
              vi == MOTOR_PWM_MAX && vd == -MOTOR_PWM_MAX);

    mundo_pose(&x0, &y0, &r0);
    andar(400);
    motores_detener();
    mundo_pose(&x1, &y1, &r1);
    double giro = fmod(r1 - r0 + 360.0, 360.0);
    double avance = hypot(x1 - x0, y1 - y0);
    printf("  curva: se desplazo %.1f cm y giro %.0f grados\n", avance, giro);
    verificar("la curva a la derecha gira sobre el eje hacia la derecha",
              avance < 5.0 && giro > 20.0 && giro < 180.0);
#endif

    motores_set(300, -400);
    motores_get(&vi, &vd);
    verificar("motores_set satura en +-255", vi == 255 && vd == -255);
    motores_detener();

#if MOTOR_VELOCIDAD_VARIABLE
    motores_curva(200, 100);
    motores_get(&vi, &vd);
    verificar("motores_curva(v, 100) detiene la llanta interior", vi == 200 && vd == 0);
#endif
    motores_detener();
    andar(500);

    titulo("Velocidad con el MPU-6050");
    /* De vuelta al centro, quieto y mirando al norte. */
    mundo_init();
    odom_reset();
    andar(200);

    double v_real, v_izq, v_der;
    motores_avanzar(200);
    andar(100);
    mundo_cinematica(&v_real, NULL);
    odom_get_velocidades(&v_izq, &v_der);
    double v_est    = odom_velocidad_cm_s();
    double v_modelo = (v_izq + v_der) / 2.0;
    printf("  arrancando: real %.1f | MPU %.1f | modelo de motores %.1f cm/s\n",
           v_real, v_est, v_modelo);
    verificar("al arrancar, el MPU ve la inercia que el modelo de motores ignora",
              fabs(v_est - v_real) < fabs(v_modelo - v_real));

    andar(1400);
    mundo_cinematica(&v_real, NULL);
    v_est = odom_velocidad_cm_s();
    printf("  en crucero: real %.1f | MPU %.1f cm/s\n", v_real, v_est);
    verificar("en crucero la velocidad estimada sigue a la real (+-15%)",
              v_real > 5.0 && fabs(v_est - v_real) < 0.15 * v_real);

    motores_detener();
    andar(800);
    printf("  detenido: MPU %.2f cm/s\n", odom_velocidad_cm_s());
    verificar("detenido, la velocidad vuelve a cero (ZUPT)", fabs(odom_velocidad_cm_s()) < 0.5);

    titulo("Tiempo antes de chocar");
    verificar("con el robot quieto no hay riesgo de choque (-1)", radar_tiempo_choque() < 0.0);

    motores_avanzar(200);
    andar(1000);
    verificar("el sensor vuelve a mirar al frente", esperar_lectura_frontal(3000) == 0);

    RadarChoque c;
    radar_estado_choque(&c);
    double d_real = mundo_distancia_desde(SIM_SENSOR_ADELANTE_CM, 0.0);
    mundo_cinematica(&v_real, NULL);
    double ttc_real = d_real / v_real;
    printf("  pared a %.1f cm, medida a %.1f cm/s: choque en %.2f s (real %.2f s)\n",
           c.distancia_cm, c.velocidad_cm_s, c.ttc_s, ttc_real);
    verificar("la lectura frontal detecta la pared", c.hay_obstaculo);
    verificar("el tiempo de choque coincide con el real (+-15%)",
              c.ttc_s > 0.0 && fabs(c.ttc_s - ttc_real) < 0.15 * ttc_real);

    double t1 = c.ttc_s;
    andar(500);
    double t2 = radar_tiempo_choque();
    printf("  medio segundo despues: %.2f s\n", t2);
    verificar("entre lecturas frontales la cuenta regresiva sigue bajando",
              t2 > 0.0 && t2 < t1 - 0.3);

    motores_detener();
    andar(800);
    verificar("al detenerse, el riesgo desaparece (-1)", radar_tiempo_choque() < 0.0);

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
    motores_detener();     andar(500);

    double ox2, oy2, orumbo;
    odom_get(&ox2, &oy2, &orumbo);
    mundo_pose(&x1, &y1, &r1);

    /* La odometria estima desde el origen del reset; el mundo, desde donde
       estaba el robot. Se compara el desplazamiento, no la posicion absoluta. */
    double real_dx = x1 - x0, real_dy = y1 - y0;
    double error = hypot(ox2 - real_dx, oy2 - real_dy);
    double recorrido = odom_distancia_recorrida();
    double error_rumbo = fabs(fmod(orumbo - (r1 - r0) + 540.0, 360.0) - 180.0);

    printf("  odometria : dx %6.1f  dy %6.1f  rumbo %5.0f\n", ox2, oy2, orumbo);
    printf("  mundo real: dx %6.1f  dy %6.1f  rumbo %5.0f\n", real_dx, real_dy,
           fmod(r1 - r0 + 360.0, 360.0));
    printf("  error de posicion: %.1f cm sobre %.1f cm recorridos (%.0f%%); "
           "error de rumbo: %.1f grados\n",
           error, recorrido, recorrido > 0 ? error / recorrido * 100.0 : 0.0, error_rumbo);

    verificar("la odometria acumulo camino recorrido", recorrido > 10.0);
    verificar("el error de la navegacion a la estima se mantiene bajo el 20%",
              recorrido > 0 && error / recorrido < 0.20);
    verificar("con el giroscopo, el rumbo estimado queda a menos de 5 grados",
              error_rumbo < 5.0);

    titulo("Audio (API, sin reproduccion real)");
    /* La reproduccion necesita tarjeta de sonido; aqui se prueba solo la API:
       init, escaneo, listado, volumen y estado. Si no hay dispositivo de audio
       el init puede fallar — se reporta como aviso, no como prueba fallida. */
    if (lib_audio_init("../audio") == 0) {
        int vol_ok = (lib_audio_set_volume(70), lib_audio_get_volume() == 70);
        verificar("el volumen se fija y se lee (roundtrip)", vol_ok);
        verificar("el estado inicial es DETENIDO", lib_audio_get_status() == LIB_AUDIO_STOPPED);
        LibAudioTrack tr[8];
        int pistas = lib_audio_get_tracks(tr, 8);
        printf("  pistas encontradas en ../audio: %d\n", pistas);
        verificar("el listado de pistas no es negativo", pistas >= 0);
        lib_audio_destroy();
    } else {
        printf("  [aviso] audio no disponible en este host (sin tarjeta de sonido); API no ejercitada\n");
    }

    titulo("Cierre");
    robot_shutdown();
    verificar("robot_shutdown cierra la sesion", robot_activo() == 0);
    motores_get(&vi, &vd);
    verificar("los motores quedan detenidos", vi == 0 && vd == 0);
    verificar("el servo queda sin pulsos", get_servo_pulsewidth(1, SERVO_GPIO) == 0);
    verificar("el bus I2C del MPU queda liberado", imu_disponible() == 0);

    printf("\nRecorrido del robot en la sala simulada:\n\n");
    mundo_dibujar();

    printf("\n%d/%d comprobaciones\n", g_pruebas - g_fallos, g_pruebas);
    printf("%s\n", g_fallos == 0 ? "TODAS LAS PRUEBAS PASARON"
                                 : "HAY PRUEBAS FALLIDAS");
    return g_fallos == 0 ? 0 : 1;
}
