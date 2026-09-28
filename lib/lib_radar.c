/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#include "lib_radar.h"
#include "lib_odom.h"
#include "lib_sensors.h"
#include "lib_servo.h"

#include <errno.h>
#include <math.h>
#include <pigpiod_if2.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define RADIANES(gr) ((gr) * M_PI / 180.0)

typedef struct {
    RadarLectura    l;
    struct timespec t;
} Entrada;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cond;
static int             g_cond_lista = 0;

static SensorUltrasonico g_sensor;
static int               g_pi = -1;
static pthread_t         g_hilo;
static volatile int      g_vivo = 0;
static int               g_pausado = 0;
static int               g_angulo  = RADAR_FRENTE;

static Entrada  g_tabla[RADAR_N_ANGULOS];
static uint32_t g_seq = 0;

/* La ultima lectura frontal, congelada para el tiempo de choque. */
static struct {
    int             medida;         /* ya hubo al menos una lectura frontal */
    int             hay;
    double          distancia_cm;
    double          velocidad_cm_s;
    double          avance_cm;      /* odom_avance_cm() al medir */
    double          rumbo_grados;
    struct timespec t;
} g_frente;

static double segundos_desde(const struct timespec *t) {
    struct timespec ahora;
    clock_gettime(CLOCK_MONOTONIC, &ahora);
    return (ahora.tv_sec - t->tv_sec) + (ahora.tv_nsec - t->tv_nsec) / 1e9;
}

/* Diferencia entre dos rumbos, en grados, sin el salto de 360 a 0. */
static double diferencia_rumbo(double a, double b) {
    return fabs(fmod(a - b + 540.0, 360.0) - 180.0);
}

static void dormir_ms(int ms) {
    if (ms > 0) usleep((useconds_t)ms * 1000);
}

static void medir(int angulo) {
    double d = sensor_leer_distancia(&g_sensor);

    double x, y, rumbo;
    odom_get(&x, &y, &rumbo);
    double avance = odom_avance_cm();
    double v      = odom_velocidad_cm_s();

    struct timespec ahora;
    clock_gettime(CLOCK_MONOTONIC, &ahora);

    pthread_mutex_lock(&g_lock);
    Entrada *e = &g_tabla[angulo / RADAR_PASO_GRADOS];
    e->l.angulo       = angulo;
    e->l.distancia_cm = d;
    e->l.seq          = ++g_seq;
    e->l.x_cm         = x;
    e->l.y_cm         = y;
    e->l.rumbo_grados = rumbo;
    e->t              = ahora;

    /* Solo la lectura frontal actualiza el tiempo de choque. Sin eco al
       frente no hay obstaculo con el que chocar: se borra el anterior. */
    if (angulo == RADAR_FRENTE) {
        g_frente.medida         = 1;
        g_frente.hay            = d > 0.0;
        g_frente.distancia_cm   = d;
        g_frente.velocidad_cm_s = v;
        g_frente.avance_cm      = avance;
        g_frente.rumbo_grados   = rumbo;
        g_frente.t              = ahora;
    }

    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);
}

static void *hilo_radar(void *arg) {
    (void)arg;
    int angulo = RADAR_FRENTE;
    int paso   = RADAR_PASO_GRADOS;

    while (g_vivo) {
        pthread_mutex_lock(&g_lock);
        while (g_pausado && g_vivo) pthread_cond_wait(&g_cond, &g_lock);
        pthread_mutex_unlock(&g_lock);
        if (!g_vivo) break;

        /* Mover, esperar a que el servo llegue y deje de vibrar, y medir.
           El propio viaje del servo separa los disparos del HC-SR04 mas de
           los 60 ms que pide para no oir el eco del pulso anterior. */
        int espera = servo_mover(angulo);
        pthread_mutex_lock(&g_lock);
        g_angulo = angulo;
        pthread_mutex_unlock(&g_lock);
        dormir_ms(espera >= 0 ? espera : 100);

        medir(angulo);

        /* Barrido de vaiven: al llegar a un extremo se invierte el sentido. */
        if (angulo + paso > 180 || angulo + paso < 0) paso = -paso;
        angulo += paso;
    }
    return NULL;
}

int radar_iniciar(int pi) {
    if (g_vivo) return 0;

    if (!g_cond_lista) {
        /* El reloj monotono evita que un ajuste de hora (NTP) alargue o
           acorte las esperas de radar_esperar_barrido(). */
        pthread_condattr_t atr;
        pthread_condattr_init(&atr);
        pthread_condattr_setclock(&atr, CLOCK_MONOTONIC);
        pthread_cond_init(&g_cond, &atr);
        pthread_condattr_destroy(&atr);
        g_cond_lista = 1;
    }

    g_pi = pi;
    sensor_init(pi, &g_sensor, RADAR_TRIG_GPIO, RADAR_ECHO_GPIO);
    servo_init(pi);

    pthread_mutex_lock(&g_lock);
    memset(g_tabla, 0, sizeof(g_tabla));
    for (int i = 0; i < RADAR_N_ANGULOS; i++) {
        g_tabla[i].l.angulo       = i * RADAR_PASO_GRADOS;
        g_tabla[i].l.distancia_cm = -1.0;
    }
    memset(&g_frente, 0, sizeof(g_frente));
    g_seq     = 0;
    g_pausado = 0;
    g_angulo  = RADAR_FRENTE;
    pthread_mutex_unlock(&g_lock);

    g_vivo = 1;
    if (pthread_create(&g_hilo, NULL, hilo_radar, NULL) != 0) {
        g_vivo = 0;
        fprintf(stderr, "[radar] no se pudo crear el hilo de barrido\n");
        return -1;
    }
    printf("[radar] barriendo de 0 a 180 grados en pasos de %d\n", RADAR_PASO_GRADOS);
    return 0;
}

void radar_detener(void) {
    if (!g_vivo) return;

    pthread_mutex_lock(&g_lock);
    g_vivo = 0;
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);
    pthread_join(g_hilo, NULL);

    servo_liberar();
    gpio_write(g_pi, RADAR_TRIG_GPIO, 0);
}

void radar_pausar(int pausado) {
    pthread_mutex_lock(&g_lock);
    g_pausado = pausado ? 1 : 0;
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);
}

int radar_angulo_actual(void) {
    pthread_mutex_lock(&g_lock);
    int a = g_angulo;
    pthread_mutex_unlock(&g_lock);
    return a;
}

int radar_lecturas(RadarLectura *out, int max) {
    if (!out || max <= 0) return 0;
    int n = max < RADAR_N_ANGULOS ? max : RADAR_N_ANGULOS;

    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < n; i++) {
        out[i] = g_tabla[i].l;
        out[i].edad_s = g_tabla[i].l.seq ? segundos_desde(&g_tabla[i].t) : -1.0;
    }
    pthread_mutex_unlock(&g_lock);
    return n;
}

double radar_distancia(int angulo) {
    if (angulo < 0 || angulo > 180 || angulo % RADAR_PASO_GRADOS != 0) return -1.0;

    pthread_mutex_lock(&g_lock);
    double d = g_tabla[angulo / RADAR_PASO_GRADOS].l.distancia_cm;
    pthread_mutex_unlock(&g_lock);
    return d;
}

uint32_t radar_seq(void) {
    pthread_mutex_lock(&g_lock);
    uint32_t s = g_seq;
    pthread_mutex_unlock(&g_lock);
    return s;
}

/* Con g_lock tomado. */
static int completo(uint32_t desde_seq) {
    for (int i = 0; i < RADAR_N_ANGULOS; i++)
        if (g_tabla[i].l.seq <= desde_seq) return 0;
    return 1;
}

int radar_barrido_completo(uint32_t desde_seq) {
    pthread_mutex_lock(&g_lock);
    int c = completo(desde_seq);
    pthread_mutex_unlock(&g_lock);
    return c;
}

int radar_esperar_barrido(int timeout_ms) {
    if (!g_cond_lista) return -1;

    struct timespec limite;
    clock_gettime(CLOCK_MONOTONIC, &limite);
    limite.tv_sec  += timeout_ms / 1000;
    limite.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (limite.tv_nsec >= 1000000000L) { limite.tv_sec++; limite.tv_nsec -= 1000000000L; }

    pthread_mutex_lock(&g_lock);
    uint32_t desde = g_seq;
    int r = 0;
    while (!completo(desde)) {
        if (pthread_cond_timedwait(&g_cond, &g_lock, &limite) == ETIMEDOUT) {
            r = completo(desde) ? 0 : -1;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return r;
}

void radar_estado_choque(RadarChoque *out) {
    if (!out) return;

    pthread_mutex_lock(&g_lock);
    int    hay     = g_frente.hay;
    double d       = g_frente.distancia_cm;
    double v_medir = g_frente.velocidad_cm_s;
    double avance0 = g_frente.avance_cm;
    double rumbo0  = g_frente.rumbo_grados;
    double edad    = g_frente.medida ? segundos_desde(&g_frente.t) : -1.0;
    pthread_mutex_unlock(&g_lock);

    out->hay_obstaculo  = hay;
    out->distancia_cm   = d;
    out->velocidad_cm_s = v_medir;
    out->edad_s         = edad;
    out->ttc_s          = -1.0;

    if (!hay || edad > RADAR_TTC_VIGENCIA_S) return;

    double rumbo;
    odom_get(NULL, NULL, &rumbo);
    if (diferencia_rumbo(rumbo, rumbo0) > RADAR_TTC_GIRO_MAX_GRADOS) return;

    /* La distancia se proyecta con lo que el robot avanzo desde la lectura;
       el tiempo, con la velocidad de ahora: si frena, el riesgo desaparece
       sin esperar a la proxima vez que el sensor mire al frente. */
    double d_ahora = d - (odom_avance_cm() - avance0);
    double v       = odom_velocidad_cm_s();

    if (d_ahora <= 0.0)             out->ttc_s = 0.0;
    else if (v > RADAR_V_MIN_CM_S)  out->ttc_s = d_ahora / v;
}

double radar_tiempo_choque(void) {
    RadarChoque c;
    radar_estado_choque(&c);
    return c.ttc_s;
}

void radar_rayo(const RadarLectura *l, double *origen_x_cm, double *origen_y_cm,
                double *rumbo_rayo_grados)
{
    if (!l) return;
    double r = RADIANES(l->rumbo_grados);

    /* El sensor gira sobre el eje del servo, adelante del centro del robot.
       Un angulo de servo de 90 mira al rumbo del robot; 0, noventa grados a
       la derecha (rumbo de brujula mayor); 180, noventa a la izquierda. */
    if (origen_x_cm) *origen_x_cm = l->x_cm + RADAR_EJE_ADELANTE_CM * sin(r);
    if (origen_y_cm) *origen_y_cm = l->y_cm + RADAR_EJE_ADELANTE_CM * cos(r);
    if (rumbo_rayo_grados)
        *rumbo_rayo_grados = fmod(l->rumbo_grados + (RADAR_FRENTE - l->angulo) + 360.0, 360.0);
}
