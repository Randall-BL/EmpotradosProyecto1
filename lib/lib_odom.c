/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#include "lib_odom.h"
#include "lib_imu.h"
#include "lib_motors.h"

#include <math.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define GRADOS(rad)   ((rad) * 180.0 / M_PI)
#define RADIANES(gr)  ((gr) * M_PI / 180.0)

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static double g_x_cm       = 0.0;   /* este positivo  */
static double g_y_cm       = 0.0;   /* norte positivo */
static double g_rumbo_rad  = 0.0;   /* rumbo de brujula, crece hacia el este */
static double g_camino_cm  = 0.0;
static double g_avance_cm  = 0.0;   /* con signo */
static double g_v_cm_s     = 0.0;   /* velocidad hacia adelante estimada */
static double g_quieto_s   = 0.0;   /* cuanto llevan los motores detenidos */
static double g_v_izq_cm_s = 0.0;
static double g_v_der_cm_s = 0.0;
static int    g_usa_imu    = 0;

static struct timespec g_t_anterior;
static int g_iniciada = 0;

static pthread_t    g_hilo;
static volatile int g_hilo_vivo = 0;

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
    g_x_cm = g_y_cm = g_rumbo_rad = g_camino_cm = g_avance_cm = 0.0;
    g_v_cm_s = g_quieto_s = 0.0;
    g_v_izq_cm_s = g_v_der_cm_s = 0.0;
    clock_gettime(CLOCK_MONOTONIC, &g_t_anterior);
    g_iniciada = 1;
    pthread_mutex_unlock(&g_lock);
}

void odom_update(void) {
    /* El sensor se lee fuera del candado: es una ida y vuelta a pigpiod. */
    ImuLectura m;
    int con_imu = imu_disponible() && imu_leer(&m) == 0;

    int vel_izq, vel_der;
    motores_get(&vel_izq, &vel_der);

    struct timespec ahora;
    pthread_mutex_lock(&g_lock);
    clock_gettime(CLOCK_MONOTONIC, &ahora);

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

    /* Modelo de traccion diferencial: la velocidad del centro del eje es el
       promedio de las dos llantas, y la rotacion viene de su diferencia.
       El rumbo es de brujula (crece en sentido horario), asi que la llanta
       izquierda mas rapida que la derecha hace crecer el rumbo. */
    double v_izq       = pwm_a_cm_s(vel_izq);
    double v_der       = pwm_a_cm_s(vel_der);
    double v_modelo    = (v_izq + v_der) / 2.0;
    double omega_model = (v_izq - v_der) / ODOM_ENTRE_EJES_CM;   /* rad/s */

    double v_antes = g_v_cm_s;
    double omega;

    if (con_imu) {
        /* Filtro complementario: la aceleracion integrada aporta la dinamica
           rapida y el modelo corrige la deriva lenta. Ver lib_odom.h. */
        double alfa = dt / ODOM_TAU_FUSION_S;
        if (alfa > 1.0) alfa = 1.0;
        g_v_cm_s += m.avance_cm_s2 * dt;
        g_v_cm_s += alfa * (v_modelo - g_v_cm_s);

        /* ZUPT: motores detenidos un rato = robot quieto, sin discusion. */
        if (v_izq == 0.0 && v_der == 0.0) {
            g_quieto_s += dt;
            if (g_quieto_s >= ODOM_ZUPT_S) g_v_cm_s = 0.0;
        } else {
            g_quieto_s = 0.0;
        }

        omega = RADIANES(m.giro_dps);
    } else {
        g_v_cm_s   = v_modelo;
        g_quieto_s = 0.0;
        omega      = omega_model;
    }

    /* Se integra con el promedio del tramo (trapecio) y el rumbo del punto
       medio, para no sesgar las curvas. */
    double v         = (v_antes + g_v_cm_s) / 2.0;
    double rumbo_med = g_rumbo_rad + omega * dt / 2.0;

    g_rumbo_rad += omega * dt;
    g_x_cm      += v * sin(rumbo_med) * dt;
    g_y_cm      += v * cos(rumbo_med) * dt;
    g_camino_cm += fabs(v) * dt;
    g_avance_cm += v * dt;

    g_rumbo_rad = fmod(g_rumbo_rad, 2.0 * M_PI);
    if (g_rumbo_rad < 0.0) g_rumbo_rad += 2.0 * M_PI;

    g_v_izq_cm_s = v_izq;
    g_v_der_cm_s = v_der;
    g_usa_imu    = con_imu;

    pthread_mutex_unlock(&g_lock);
}

static void *hilo_odom(void *arg) {
    (void)arg;
    while (g_hilo_vivo) {
        odom_update();
        usleep(ODOM_PERIODO_MS * 1000);
    }
    return NULL;
}

int odom_arrancar(void) {
    if (g_hilo_vivo) return 0;
    odom_init();
    g_hilo_vivo = 1;
    if (pthread_create(&g_hilo, NULL, hilo_odom, NULL) != 0) {
        g_hilo_vivo = 0;
        return -1;
    }
    return 0;
}

void odom_parar(void) {
    if (!g_hilo_vivo) return;
    g_hilo_vivo = 0;
    pthread_join(g_hilo, NULL);
}

void odom_get(double *x_cm, double *y_cm, double *rumbo_grados) {
    pthread_mutex_lock(&g_lock);
    if (x_cm)        *x_cm        = g_x_cm;
    if (y_cm)        *y_cm        = g_y_cm;
    if (rumbo_grados) *rumbo_grados = GRADOS(g_rumbo_rad);
    pthread_mutex_unlock(&g_lock);
}

double odom_velocidad_cm_s(void) {
    pthread_mutex_lock(&g_lock);
    double v = g_v_cm_s;
    pthread_mutex_unlock(&g_lock);
    return v;
}

double odom_avance_cm(void) {
    pthread_mutex_lock(&g_lock);
    double a = g_avance_cm;
    pthread_mutex_unlock(&g_lock);
    return a;
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

int odom_usa_imu(void) {
    pthread_mutex_lock(&g_lock);
    int u = g_usa_imu;
    pthread_mutex_unlock(&g_lock);
    return u;
}
