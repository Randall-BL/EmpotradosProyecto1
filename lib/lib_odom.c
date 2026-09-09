/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#include "lib_odom.h"
#include "lib_motors.h"

#include <math.h>
#include <pthread.h>
#include <time.h>

#define GRADOS(rad) ((rad) * 180.0 / M_PI)

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static double g_x_cm       = 0.0;   /* este positivo  */
static double g_y_cm       = 0.0;   /* norte positivo */
static double g_rumbo_rad  = 0.0;   /* rumbo de brujula, crece hacia el este */
static double g_camino_cm  = 0.0;
static double g_v_izq_cm_s = 0.0;
static double g_v_der_cm_s = 0.0;

static struct timespec g_t_anterior;
static int g_iniciada = 0;

/* Convierte el duty cycle ordenado a velocidad lineal de la llanta. */
static double pwm_a_cm_s(int pwm) {
    int magnitud = pwm < 0 ? -pwm : pwm;
    if (magnitud < ODOM_PWM_ARRANQUE) return 0.0;

    double v = (magnitud / (double)MOTOR_PWM_MAX) * ODOM_VEL_MAX_CM_S;
    return pwm < 0 ? -v : v;
}

void odom_init(void) {
    pthread_mutex_lock(&g_lock);
    if (!g_iniciada) {
        clock_gettime(CLOCK_MONOTONIC, &g_t_anterior);
        g_iniciada = 1;
    }
    pthread_mutex_unlock(&g_lock);
}

void odom_reset(void) {
    pthread_mutex_lock(&g_lock);
    g_x_cm = g_y_cm = g_rumbo_rad = g_camino_cm = 0.0;
    g_v_izq_cm_s = g_v_der_cm_s = 0.0;
    clock_gettime(CLOCK_MONOTONIC, &g_t_anterior);
    g_iniciada = 1;
    pthread_mutex_unlock(&g_lock);
}

void odom_update(void) {
    struct timespec ahora;
    clock_gettime(CLOCK_MONOTONIC, &ahora);

    int vel_izq, vel_der;
    motores_get(&vel_izq, &vel_der);

    pthread_mutex_lock(&g_lock);

    if (!g_iniciada) {
        g_t_anterior = ahora;
        g_iniciada   = 1;
        pthread_mutex_unlock(&g_lock);
        return;
    }

    double dt = (ahora.tv_sec  - g_t_anterior.tv_sec) +
                (ahora.tv_nsec - g_t_anterior.tv_nsec) / 1e9;
    g_t_anterior = ahora;

    /* Un salto de reloj o una pausa larga no deben inventar metros de avance. */
    if (dt <= 0.0 || dt > 1.0) {
        pthread_mutex_unlock(&g_lock);
        return;
    }

    double v_izq = pwm_a_cm_s(vel_izq);
    double v_der = pwm_a_cm_s(vel_der);

    /* Modelo de traccion diferencial: la velocidad del centro del eje es el
       promedio de las dos llantas, y la rotacion viene de su diferencia.
       El rumbo es de brujula (crece en sentido horario), asi que la llanta
       izquierda mas rapida que la derecha hace crecer el rumbo. */
    double v          = (v_izq + v_der) / 2.0;
    double omega      = (v_izq - v_der) / ODOM_ENTRE_EJES_CM;   /* rad/s */
    double rumbo_med  = g_rumbo_rad + omega * dt / 2.0;         /* punto medio */

    g_rumbo_rad += omega * dt;
    g_x_cm      += v * sin(rumbo_med) * dt;
    g_y_cm      += v * cos(rumbo_med) * dt;
    g_camino_cm += fabs(v) * dt;

    g_rumbo_rad = fmod(g_rumbo_rad, 2.0 * M_PI);
    if (g_rumbo_rad < 0.0) g_rumbo_rad += 2.0 * M_PI;

    g_v_izq_cm_s = v_izq;
    g_v_der_cm_s = v_der;

    pthread_mutex_unlock(&g_lock);
}

void odom_get(double *x_cm, double *y_cm, double *rumbo_grados) {
    pthread_mutex_lock(&g_lock);
    if (x_cm)        *x_cm        = g_x_cm;
    if (y_cm)        *y_cm        = g_y_cm;
    if (rumbo_grados) *rumbo_grados = GRADOS(g_rumbo_rad);
    pthread_mutex_unlock(&g_lock);
}

double odom_distancia_recorrida(void) {
    pthread_mutex_lock(&g_lock);
    double d = g_camino_cm;
    pthread_mutex_unlock(&g_lock);
    return d;
}

void odom_get_velocidades(double *izq_cm_s, double *der_cm_s) {
    pthread_mutex_lock(&g_lock);
    if (izq_cm_s) *izq_cm_s = g_v_izq_cm_s;
    if (der_cm_s) *der_cm_s = g_v_der_cm_s;
    pthread_mutex_unlock(&g_lock);
}
