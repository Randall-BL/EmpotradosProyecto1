/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 */

/* Constantes ODOM_* y prototipos del modulo. */
#include "lib_odom.h"
/* MPU-6050: aceleracion de avance y velocidad de giro. */
#include "lib_imu.h"
/* Ultima orden dada a los motores y MOTOR_PWM_MAX. */
#include "lib_motors.h"

/* sin, cos, fabs y fmod. */
#include <math.h>
/* Mutex e hilo de la odometria. */
#include <pthread.h>
/* clock_gettime y struct timespec. */
#include <time.h>
/* usleep. */
#include <unistd.h>

/* Conversiones entre radianes y grados. */
#define GRADOS(rad)   ((rad) * 180.0 / M_PI)
#define RADIANES(gr)  ((gr) * M_PI / 180.0)

/* Protege toda la pose y las velocidades. */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static double g_x_cm       = 0.0;   /* este positivo  */
static double g_y_cm       = 0.0;   /* norte positivo */
static double g_rumbo_rad  = 0.0;   /* rumbo de brujula, crece hacia el este */
/* Camino total recorrido, sin signo. */
static double g_camino_cm  = 0.0;
static double g_avance_cm  = 0.0;   /* con signo */
static double g_v_cm_s     = 0.0;   /* velocidad hacia adelante estimada */
static double g_quieto_s   = 0.0;   /* cuanto llevan los motores detenidos */
/* Velocidad del modelo de cada llanta en la ultima integracion. */
static double g_v_izq_cm_s = 0.0;
static double g_v_der_cm_s = 0.0;
/* 1 si la ultima integracion uso el MPU-6050. */
static int    g_usa_imu    = 0;

/* Instante de la integracion anterior: el paso es ahora menos esto. */
static struct timespec g_t_anterior;
/* 1 cuando g_t_anterior ya tiene un valor valido. */
static int g_iniciada = 0;

/* Hilo que llama a odom_update() a 50 Hz. */
static pthread_t    g_hilo;
/* 1 mientras ese hilo debe seguir corriendo. */
static volatile int g_hilo_vivo = 0;

/* Convierte el duty cycle ordenado a velocidad lineal de la llanta. */
static double pwm_a_cm_s(int pwm) {
    /* Magnitud de la orden, sin signo. */
    int magnitud = pwm < 0 ? -pwm : pwm;
    /* Por debajo del umbral de arranque el motor no gira. */
    if (magnitud < ODOM_PWM_ARRANQUE) return 0.0;

    /* Proporcion lineal: la orden maxima da la velocidad maxima. */
    double v = (magnitud / (double)MOTOR_PWM_MAX) * ODOM_VEL_MAX_CM_S;
    /* Recupera el signo. */
    return pwm < 0 ? -v : v;
}

/* Fija el instante inicial la primera vez que se llama. */
void odom_init(void) {
    pthread_mutex_lock(&g_lock);
    if (!g_iniciada) {
        clock_gettime(CLOCK_MONOTONIC, &g_t_anterior);
        g_iniciada = 1;
    }
    pthread_mutex_unlock(&g_lock);
}

/* Vuelve al origen: posicion, rumbo, acumulados y velocidades en cero, y el
   reloj de integracion en este instante. */
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
    /* con_imu vale 1 solo si hay sensor y ademas contesto esta lectura. */
    int con_imu = imu_disponible() && imu_leer(&m) == 0;

    /* Ultima orden aplicada a cada motor. */
    int vel_izq, vel_der;
    motores_get(&vel_izq, &vel_der);

    /* Instante actual, tomado ya con el candado. */
    struct timespec ahora;
    pthread_mutex_lock(&g_lock);
    clock_gettime(CLOCK_MONOTONIC, &ahora);

    /* Primera llamada: solo se fija el instante de partida, sin integrar. */
    if (!g_iniciada) {
        g_t_anterior = ahora;
        g_iniciada   = 1;
        pthread_mutex_unlock(&g_lock);
        return;
    }

    /* Paso de integracion: segundos reales desde la llamada anterior. */
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
    /* Velocidad del centro: el promedio de las dos llantas. */
    double v_modelo    = (v_izq + v_der) / 2.0;
    /* Velocidad angular: la diferencia entre llantas sobre su separacion. */
    double omega_model = (v_izq - v_der) / ODOM_ENTRE_EJES_CM;   /* rad/s */

    /* Velocidad antes de este paso, para integrar por trapecios. */
    double v_antes = g_v_cm_s;
    /* Velocidad angular que se va a integrar, en rad/s. */
    double omega;

    if (con_imu) {
        /* Filtro complementario: la aceleracion integrada aporta la dinamica
           rapida y el modelo corrige la deriva lenta. Ver lib_odom.h. */
        /* Peso del modelo en este paso: dt sobre la constante de tiempo, a
           lo
           sumo 1. */
        double alfa = dt / ODOM_TAU_FUSION_S;
        if (alfa > 1.0) alfa = 1.0;
        /* Integra la aceleracion medida. */
        g_v_cm_s += m.avance_cm_s2 * dt;
        /* Acerca la velocidad a la del modelo en la proporcion alfa. */
        g_v_cm_s += alfa * (v_modelo - g_v_cm_s);

        /* ZUPT: motores detenidos un rato = robot quieto, sin discusion. */
        if (v_izq == 0.0 && v_der == 0.0) {
            /* Acumula el tiempo detenido y, pasado el umbral, fuerza
               velocidad cero. */
            g_quieto_s += dt;
            if (g_quieto_s >= ODOM_ZUPT_S) g_v_cm_s = 0.0;
        } else {
            /* Con algun motor andando se reinicia la cuenta. */
            g_quieto_s = 0.0;
        }

        /* El giro sale del giroscopo, pasado a rad/s. */
        omega = RADIANES(m.giro_dps);
    } else {
        /* Sin MPU todo sale del modelo de los motores. */
        g_v_cm_s   = v_modelo;
        g_quieto_s = 0.0;
        omega      = omega_model;
    }

    /* Se integra con el promedio del tramo (trapecio) y el rumbo del punto
       medio, para no sesgar las curvas. */
    double v         = (v_antes + g_v_cm_s) / 2.0;
    double rumbo_med = g_rumbo_rad + omega * dt / 2.0;

    /* Nuevo rumbo. */
    g_rumbo_rad += omega * dt;
    /* Desplazamiento: x crece con el seno del rumbo (este) e y con el coseno
       (norte). */
    g_x_cm      += v * sin(rumbo_med) * dt;
    g_y_cm      += v * cos(rumbo_med) * dt;
    /* El camino suma siempre; el avance conserva el signo. */
    g_camino_cm += fabs(v) * dt;
    g_avance_cm += v * dt;

    /* Lleva el rumbo al rango de 0 a 2 pi. */
    g_rumbo_rad = fmod(g_rumbo_rad, 2.0 * M_PI);
    if (g_rumbo_rad < 0.0) g_rumbo_rad += 2.0 * M_PI;

    /* Guarda las velocidades del modelo y de donde salio la estimacion. */
    g_v_izq_cm_s = v_izq;
    g_v_der_cm_s = v_der;
    g_usa_imu    = con_imu;

    pthread_mutex_unlock(&g_lock);
}

/* Cuerpo del hilo: integra y duerme un periodo, hasta que odom_parar() baja
   la bandera. */
static void *hilo_odom(void *arg) {
    /* El argumento del hilo no se usa. */
    (void)arg;
    while (g_hilo_vivo) {
        odom_update();
        usleep(ODOM_PERIODO_MS * 1000);
    }
    return NULL;
}

/* Lanza el hilo de la odometria. Devuelve 0 si quedo corriendo. */
int odom_arrancar(void) {
    /* Si ya corre no se lanza otro. */
    if (g_hilo_vivo) return 0;
    /* Asegura que el reloj de integracion tenga un punto de partida. */
    odom_init();
    /* La bandera se sube antes de crear el hilo, que la consulta al entrar. */
    g_hilo_vivo = 1;
    if (pthread_create(&g_hilo, NULL, hilo_odom, NULL) != 0) {
        g_hilo_vivo = 0;
        return -1;
    }
    return 0;
}

/* Detiene el hilo y espera a que termine. */
void odom_parar(void) {
    if (!g_hilo_vivo) return;
    g_hilo_vivo = 0;
    pthread_join(g_hilo, NULL);
}

/* Devuelve la pose. Cada puntero puede ser NULL. */
void odom_get(double *x_cm, double *y_cm, double *rumbo_grados) {
    pthread_mutex_lock(&g_lock);
    if (x_cm)        *x_cm        = g_x_cm;
    if (y_cm)        *y_cm        = g_y_cm;
    /* El rumbo se guarda en radianes y se entrega en grados. */
    if (rumbo_grados) *rumbo_grados = GRADOS(g_rumbo_rad);
    pthread_mutex_unlock(&g_lock);
}

/* Velocidad hacia adelante, leida bajo el candado. */
double odom_velocidad_cm_s(void) {
    pthread_mutex_lock(&g_lock);
    double v = g_v_cm_s;
    pthread_mutex_unlock(&g_lock);
    return v;
}

/* Avance neto acumulado, con signo. */
double odom_avance_cm(void) {
    pthread_mutex_lock(&g_lock);
    double a = g_avance_cm;
    pthread_mutex_unlock(&g_lock);
    return a;
}

/* Camino total recorrido, sin signo. */
double odom_distancia_recorrida(void) {
    pthread_mutex_lock(&g_lock);
    double d = g_camino_cm;
    pthread_mutex_unlock(&g_lock);
    return d;
}

/* Velocidad del modelo de cada llanta. Cada puntero puede ser NULL. */
void odom_get_velocidades(double *izq_cm_s, double *der_cm_s) {
    pthread_mutex_lock(&g_lock);
    if (izq_cm_s) *izq_cm_s = g_v_izq_cm_s;
    if (der_cm_s) *der_cm_s = g_v_der_cm_s;
    pthread_mutex_unlock(&g_lock);
}

/* 1 si la ultima integracion uso el MPU-6050. */
int odom_usa_imu(void) {
    pthread_mutex_lock(&g_lock);
    int u = g_usa_imu;
    pthread_mutex_unlock(&g_lock);
    return u;
}
