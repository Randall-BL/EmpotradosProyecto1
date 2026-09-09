/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#include "mundo.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* La habitacion: 4 x 4 metros con el origen en el centro. */
#define SALA_MIN_X (-200.0)
#define SALA_MAX_X ( 200.0)
#define SALA_MIN_Y (-200.0)
#define SALA_MAX_Y ( 200.0)

/* Alcance del HC-SR04. Mas alla no hay eco de vuelta. */
#define ALCANCE_CM 400.0

/* Radio del robot, para que choque con el borde y no con su centro. */
#define RADIO_ROBOT_CM 12.0

/* Parametros fisicos "verdaderos" del robot simulado. No son los mismos que
   las constantes de lib_odom: la gracia es justamente que difieran un poco,
   para que se vea el error que acumula la navegacion a la estima. */
#define REAL_VEL_MAX_CM_S   28.0
#define REAL_ENTRE_EJES_CM  15.5
#define REAL_PWM_ARRANQUE   60

typedef struct { double x0, y0, x1, y1; } Caja;

/* Un par de muebles, para que la habitacion no sea una caja vacia. */
static const Caja g_muebles[] = {
    {  40.0,  40.0, 100.0, 120.0 },   /* sofa      */
    { -150.0, -60.0, -90.0, -20.0 },  /* mesa      */
    { -30.0, -180.0, 60.0, -140.0 },  /* alfombra? un mueble mas */
};
#define N_MUEBLES ((int)(sizeof(g_muebles) / sizeof(g_muebles[0])))

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static double g_x = 0.0, g_y = 0.0, g_rumbo = 0.0;   /* pose verdadera  */
static int    g_vel_izq = 0, g_vel_der = 0;          /* ordenes de PWM  */
static int    g_choco = 0;
static struct timespec g_t;
static int    g_listo = 0;
static int    g_hilo_vivo = 0;

/* Rastro recorrido, para dibujarlo. */
#define RASTRO_MAX 4000
static double g_rastro_x[RASTRO_MAX], g_rastro_y[RASTRO_MAX];
static int    g_rastro_n = 0;

static double pwm_a_cm_s(int pwm) {
    int m = pwm < 0 ? -pwm : pwm;
    if (m < REAL_PWM_ARRANQUE) return 0.0;
    double v = (m / 255.0) * REAL_VEL_MAX_CM_S;
    return pwm < 0 ? -v : v;
}

/* Avanza la simulacion hasta ahora. Se llama con g_lock tomado. */
static void integrar(void) {
    struct timespec ahora;
    clock_gettime(CLOCK_MONOTONIC, &ahora);

    if (!g_listo) { g_t = ahora; g_listo = 1; return; }

    double dt = (ahora.tv_sec - g_t.tv_sec) + (ahora.tv_nsec - g_t.tv_nsec) / 1e9;
    g_t = ahora;
    if (dt <= 0.0 || dt > 1.0) return;

    double v_izq = pwm_a_cm_s(g_vel_izq);
    double v_der = pwm_a_cm_s(g_vel_der);
    double v     = (v_izq + v_der) / 2.0;
    double omega = (v_izq - v_der) / REAL_ENTRE_EJES_CM;

    double rumbo_med = g_rumbo + omega * dt / 2.0;
    double nx = g_x + v * sin(rumbo_med) * dt;
    double ny = g_y + v * cos(rumbo_med) * dt;

    g_rumbo = fmod(g_rumbo + omega * dt, 2.0 * M_PI);
    if (g_rumbo < 0.0) g_rumbo += 2.0 * M_PI;

    /* Las paredes y los muebles frenan: el robot no los atraviesa. */
    g_choco = 0;
    if (nx < SALA_MIN_X + RADIO_ROBOT_CM) { nx = SALA_MIN_X + RADIO_ROBOT_CM; g_choco = 1; }
    if (nx > SALA_MAX_X - RADIO_ROBOT_CM) { nx = SALA_MAX_X - RADIO_ROBOT_CM; g_choco = 1; }
    if (ny < SALA_MIN_Y + RADIO_ROBOT_CM) { ny = SALA_MIN_Y + RADIO_ROBOT_CM; g_choco = 1; }
    if (ny > SALA_MAX_Y - RADIO_ROBOT_CM) { ny = SALA_MAX_Y - RADIO_ROBOT_CM; g_choco = 1; }

    for (int i = 0; i < N_MUEBLES; i++) {
        const Caja *c = &g_muebles[i];
        if (nx > c->x0 - RADIO_ROBOT_CM && nx < c->x1 + RADIO_ROBOT_CM &&
            ny > c->y0 - RADIO_ROBOT_CM && ny < c->y1 + RADIO_ROBOT_CM) {
            nx = g_x; ny = g_y;   /* se queda donde estaba */
            g_choco = 1;
            break;
        }
    }

    g_x = nx;
    g_y = ny;

    if (g_rastro_n < RASTRO_MAX &&
        (g_rastro_n == 0 ||
         fabs(g_x - g_rastro_x[g_rastro_n - 1]) > 2.0 ||
         fabs(g_y - g_rastro_y[g_rastro_n - 1]) > 2.0)) {
        g_rastro_x[g_rastro_n] = g_x;
        g_rastro_y[g_rastro_n] = g_y;
        g_rastro_n++;
    }
}

/* El mundo avanza solo, como la realidad.
 *
 * Sin este hilo la simulacion solo progresaba cuando alguien preguntaba algo,
 * y un tramo largo de movimiento —un avance de 1.5 s sin leer sensores— se
 * integraba de un solo paso, o se descartaba por pasarse del limite de dt.
 * Con el hilo, el robot se mueve al ritmo del reloj, lea quien lea.
 */
static void *hilo_mundo(void *arg) {
    (void)arg;
    while (g_hilo_vivo) {
        usleep(20000);
        pthread_mutex_lock(&g_lock);
        integrar();
        pthread_mutex_unlock(&g_lock);
    }
    return NULL;
}

void mundo_init(void) {
    pthread_mutex_lock(&g_lock);
    g_x = g_y = g_rumbo = 0.0;
    g_vel_izq = g_vel_der = 0;
    g_choco = 0;
    g_rastro_n = 0;
    clock_gettime(CLOCK_MONOTONIC, &g_t);
    g_listo = 1;
    pthread_mutex_unlock(&g_lock);

    if (!g_hilo_vivo) {
        pthread_t t;
        g_hilo_vivo = 1;
        if (pthread_create(&t, NULL, hilo_mundo, NULL) == 0) pthread_detach(t);
        else g_hilo_vivo = 0;
    }
}

void mundo_set_motores(int vel_izq, int vel_der) {
    pthread_mutex_lock(&g_lock);
    integrar();                  /* cerrar el tramo con la velocidad anterior */
    g_vel_izq = vel_izq;
    g_vel_der = vel_der;
    pthread_mutex_unlock(&g_lock);
}

/* Distancia del rayo (x,y)+t*(dx,dy) al borde de una caja, o -1 si no la toca.
   Metodo de las rebanadas: se intersecta el rayo con cada par de planos. */
static double rayo_caja(double x, double y, double dx, double dy,
                        double x0, double y0, double x1, double y1, int desde_adentro)
{
    double t_min = -INFINITY, t_max = INFINITY;

    if (fabs(dx) < 1e-9) {
        if (x < x0 || x > x1) return -1.0;
    } else {
        double ta = (x0 - x) / dx, tb = (x1 - x) / dx;
        if (ta > tb) { double s = ta; ta = tb; tb = s; }
        if (ta > t_min) t_min = ta;
        if (tb < t_max) t_max = tb;
    }

    if (fabs(dy) < 1e-9) {
        if (y < y0 || y > y1) return -1.0;
    } else {
        double ta = (y0 - y) / dy, tb = (y1 - y) / dy;
        if (ta > tb) { double s = ta; ta = tb; tb = s; }
        if (ta > t_min) t_min = ta;
        if (tb < t_max) t_max = tb;
    }

    if (t_max < t_min || t_max < 0.0) return -1.0;

    /* Desde adentro (las paredes de la sala) interesa la salida; desde afuera
       (un mueble) interesa la entrada. */
    double t = desde_adentro ? t_max : t_min;
    return t >= 0.0 ? t : -1.0;
}

double mundo_distancia(double rumbo_offset) {
    pthread_mutex_lock(&g_lock);
    integrar();

    double rumbo = g_rumbo + rumbo_offset * M_PI / 180.0;
    double dx = sin(rumbo), dy = cos(rumbo);
    double x = g_x, y = g_y;

    pthread_mutex_unlock(&g_lock);

    double d = rayo_caja(x, y, dx, dy, SALA_MIN_X, SALA_MIN_Y, SALA_MAX_X, SALA_MAX_Y, 1);

    for (int i = 0; i < N_MUEBLES; i++) {
        double dm = rayo_caja(x, y, dx, dy, g_muebles[i].x0, g_muebles[i].y0,
                              g_muebles[i].x1, g_muebles[i].y1, 0);
        if (dm >= 0.0 && (d < 0.0 || dm < d)) d = dm;
    }

    if (d < 0.0 || d > ALCANCE_CM) return -1.0;
    return d;
}

void mundo_pose(double *x_cm, double *y_cm, double *rumbo_grados) {
    pthread_mutex_lock(&g_lock);
    integrar();
    if (x_cm)        *x_cm = g_x;
    if (y_cm)        *y_cm = g_y;
    if (rumbo_grados) *rumbo_grados = g_rumbo * 180.0 / M_PI;
    pthread_mutex_unlock(&g_lock);
}

int mundo_choco(void) {
    pthread_mutex_lock(&g_lock);
    integrar();
    int c = g_choco;
    pthread_mutex_unlock(&g_lock);
    return c;
}

/* ── Dibujo en la terminal ──────────────────────────────────────────────── */

#define DIBUJO_COLS 61
#define DIBUJO_FILS 25

void mundo_dibujar(void) {
    char lienzo[DIBUJO_FILS][DIBUJO_COLS + 1];
    memset(lienzo, ' ', sizeof(lienzo));
    for (int f = 0; f < DIBUJO_FILS; f++) lienzo[f][DIBUJO_COLS] = '\0';

    double ancho = SALA_MAX_X - SALA_MIN_X;
    double alto  = SALA_MAX_Y - SALA_MIN_Y;

    #define A_COL(x) ((int)(((x) - SALA_MIN_X) / ancho * (DIBUJO_COLS - 1)))
    #define A_FIL(y) ((int)((SALA_MAX_Y - (y)) / alto * (DIBUJO_FILS - 1)))

    for (int i = 0; i < N_MUEBLES; i++)
        for (int f = A_FIL(g_muebles[i].y1); f <= A_FIL(g_muebles[i].y0); f++)
            for (int c = A_COL(g_muebles[i].x0); c <= A_COL(g_muebles[i].x1); c++)
                if (f >= 0 && f < DIBUJO_FILS && c >= 0 && c < DIBUJO_COLS)
                    lienzo[f][c] = '#';

    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < g_rastro_n; i++) {
        int f = A_FIL(g_rastro_y[i]), c = A_COL(g_rastro_x[i]);
        if (f >= 0 && f < DIBUJO_FILS && c >= 0 && c < DIBUJO_COLS && lienzo[f][c] == ' ')
            lienzo[f][c] = '.';
    }
    int rf = A_FIL(g_y), rc = A_COL(g_x);
    double grados = g_rumbo * 180.0 / M_PI;
    pthread_mutex_unlock(&g_lock);

    if (rf >= 0 && rf < DIBUJO_FILS && rc >= 0 && rc < DIBUJO_COLS) {
        int cuadrante = ((int)((grados + 45.0) / 90.0)) % 4;
        lienzo[rf][rc] = "^>v<"[cuadrante];
    }

    printf("+");
    for (int c = 0; c < DIBUJO_COLS; c++) printf("-");
    printf("+\n");
    for (int f = 0; f < DIBUJO_FILS; f++) printf("|%s|\n", lienzo[f]);
    printf("+");
    for (int c = 0; c < DIBUJO_COLS; c++) printf("-");
    printf("+\n");
    printf("  # mueble   . recorrido   ^>v< robot (%.0f, %.0f) rumbo %.0f\n",
           g_x, g_y, grados);
}
