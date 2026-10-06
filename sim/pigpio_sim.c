/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#include "pigpiod_if2.h"
#include "mundo.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * Implementacion falsa de pigpio contra el mundo simulado.
 *
 * Traduce lo que la biblioteca le escribe a los pines en movimiento del robot
 * virtual, y contesta las lecturas de los sensores midiendo sobre ese mundo.
 * Los numeros de pin son los mismos de docs/hardware-pinout.md: si alguien
 * cambia el cableado alla y no aqui, el simulador deja de moverse, que es
 * justamente el aviso que uno quiere.
 */

/* Motores: L298N con los jumpers ENA/ENB puestos, PWM sobre IN1-IN4 */
#define IN1  5
#define IN2  6
#define IN3 23
#define IN4 24

/* Rango de PWM que configura la biblioteca (MOTOR_PWM_MAX). */
#define PWM_RANGO 255

/* LEDs indicadores */
#define LED_POWER_PIN      16
#define LED_AUTONOMOUS_PIN 20
#define LED_MANUAL_PIN     21
#define LED_OBSTACLE_PIN   26

/* Radar: HC-SR04 sobre el servo */
#define RADAR_TRIG 17
#define RADAR_ECHO 27
#define SERVO_PIN  25

/* Sensores infrarrojos de desnivel, en las esquinas delanteras del chasis.
   En alto cuando no ven piso, como el TCRT5000 o el FC-51. */
#define CAIDA_IZQ_PIN  4
#define CAIDA_DER_PIN  8
#define CAIDA_ADELANTE_CM 12.0
#define CAIDA_LATERAL_CM   8.0

/* Un SG90 a 5 V: unos 0.1 s cada 60 grados, y 500-2500 us de pulso para
   0-180 grados. Son las del servo "fisico", no las constantes de lib_servo. */
#define SERVO_GRADOS_POR_S 600.0
#define SERVO_PULSO_MIN    500
#define SERVO_PULSO_MAX   2500

/* MPU-6050 en /dev/i2c-1, direccion 0x68 */
#define MPU_BUS     1
#define MPU_DIR     0x68
#define MPU_HANDLE  3              /* handle cualquiera, pero >= 0 */

/* Sesgo de fabrica del sensor simulado. La calibracion de lib_imu tiene que
   quitarlo: sin ella el rumbo derivaria 1.5 grados por segundo. */
#define MPU_SESGO_AX_G    0.020
#define MPU_SESGO_GZ_DPS  1.5

/* Ruido blanco de las lecturas, de pico. */
#define MPU_RUIDO_G       0.003
#define MPU_RUIDO_DPS     0.10

/* Microsegundos de eco por centimetro: ida y vuelta a 343 m/s. */
#define US_POR_CM 58.309

/* ── Estado de los pines ─────────────────────────────────────────────────── */

/* Ciclo de trabajo en cada GPIO de motor, 0..PWM_RANGO: lo que escribe la
   biblioteca, antes del optoacoplador. En reposo el GPIO esta en bajo. */
static int g_gpio_in[4];            /* IN1, IN2, IN3, IN4 */
static int g_leds[4];              /* power, autonomous, manual, obstacle */
static int g_verboso = 1;          /* imprimir cambios de LED */

/* Reloj virtual: microsegundos reales desde el arranque, mas lo que suman los
   pulsos de eco simulados. */
static struct timespec g_t0;
static uint32_t g_tick_extra = 0;
static int g_reloj_listo = 0;

/* ── Sensores ────────────────────────────────────────────────────────────── */

typedef enum { S_IDLE = 0, S_ARMADO, S_ESPERA_BAJO, S_PULSO_ALTO, S_PULSO_FIN } FaseSensor;

typedef struct {
    FaseSensor fase;
    uint32_t   pulso_us;       /* 0 = sin eco */
} SensorSim;

/* Un solo HC-SR04: la direccion en que mide la decide el servo. */
static SensorSim g_sensor = { S_IDLE, 0 };

/* ── Servo ───────────────────────────────────────────────────────────────── */

static pthread_mutex_t g_lock_sim = PTHREAD_MUTEX_INITIALIZER;

static double          g_servo_desde = 90.0;   /* se mueve de aqui ...      */
static double          g_servo_hacia = 90.0;   /* ... hasta aca             */
static struct timespec g_servo_t;              /* desde este instante       */
static unsigned        g_servo_pulso = 0;

static double segundos_desde(const struct timespec *t) {
    struct timespec ahora;
    clock_gettime(CLOCK_MONOTONIC, &ahora);
    return (ahora.tv_sec - t->tv_sec) + (ahora.tv_nsec - t->tv_nsec) / 1e9;
}

/* Donde esta el servo ahora: viaja a velocidad constante hacia el destino.
   Con g_lock_sim tomado. */
static double servo_angulo_real(void) {
    double recorrido = SERVO_GRADOS_POR_S * segundos_desde(&g_servo_t);
    double falta     = g_servo_hacia - g_servo_desde;
    if (fabs(falta) <= recorrido) return g_servo_hacia;
    return g_servo_desde + (falta > 0 ? recorrido : -recorrido);
}

/* ── MPU-6050 ────────────────────────────────────────────────────────────── */

static int g_mpu_abierto = 0;
static int g_mpu_dormido = 1;      /* el sensor arranca dormido */
static uint32_t g_ruido = 12345;   /* generador congruencial: ruido repetible */

/* Ruido uniforme en [-amplitud, amplitud]. */
static double ruido(double amplitud) {
    g_ruido = g_ruido * 1103515245u + 12345u;
    return amplitud * ((((g_ruido >> 8) & 0xFFFF) / 32767.5) - 1.0);
}

static void poner_be16(char *b, double valor) {
    long v = lround(valor);
    if (v >  32767) v =  32767;
    if (v < -32768) v = -32768;
    b[0] = (char)((v >> 8) & 0xFF);
    b[1] = (char)(v & 0xFF);
}

/* ── Motores ─────────────────────────────────────────────────────────────── */

static int indice_in(unsigned gpio) {
    switch (gpio) {
        case IN1: return 0;
        case IN2: return 1;
        case IN3: return 2;
        case IN4: return 3;
        default:  return -1;
    }
}

/* Lo que llega a la entrada del L298N: el PC817 en emisor comun invierte, asi
   que un GPIO en alto la deja en bajo. Es la placa real, no la biblioteca: si
   la biblioteca no compensa la inversion, aqui el robot anda al reves. */
static int entrada_l298n(int i) {
    return PWM_RANGO - g_gpio_in[i];
}

/* Con el puente siempre habilitado, cada motor es empujado hacia adelante la
   fraccion del ciclo en que solo IN1 esta en alto, hacia atras la fraccion en
   que solo IN2 lo esta, y frenado el resto (las dos iguales). Con una sola
   entrada modulando por vez, el neto es la diferencia de los dos ciclos. */
static void empujar_motores(void) {
    /* Como en el robot armado: el motor izquierdo va a la salida B del L298N
       (IN3/IN4) y el derecho a la A (IN1/IN2). */
    mundo_set_motores(entrada_l298n(2) - entrada_l298n(3),
                      entrada_l298n(0) - entrada_l298n(1));
}

/* ── API de pigpio ───────────────────────────────────────────────────────── */

int pigpio_start(const char *addrStr, const char *portStr) {
    (void)addrStr; (void)portStr;
    mundo_init();
    memset(g_leds, 0, sizeof(g_leds));
    memset(g_gpio_in, 0, sizeof(g_gpio_in));
    g_sensor.fase = S_IDLE;

    pthread_mutex_lock(&g_lock_sim);
    g_servo_desde = g_servo_hacia = 90.0;
    g_servo_pulso = 0;
    clock_gettime(CLOCK_MONOTONIC, &g_servo_t);
    g_mpu_abierto = 0;
    g_mpu_dormido = 1;
    pthread_mutex_unlock(&g_lock_sim);

    clock_gettime(CLOCK_MONOTONIC, &g_t0);
    g_tick_extra  = 0;
    g_reloj_listo = 1;
    printf("[sim] pigpiod simulado: robot con radar y MPU-6050 en el centro de una sala de 4x4 m\n");
    return 1;                        /* handle cualquiera, pero >= 0 */
}

void pigpio_stop(int pi) {
    (void)pi;
    mundo_set_motores(0, 0);
    printf("[sim] pigpiod simulado cerrado\n");
}

int set_mode(int pi, unsigned gpio, unsigned mode) {
    (void)pi; (void)gpio; (void)mode;
    return 0;
}

int set_PWM_frequency(int pi, unsigned user_gpio, unsigned frequency) {
    (void)pi; (void)user_gpio; (void)frequency;
    return 0;
}

int set_PWM_range(int pi, unsigned user_gpio, unsigned range) {
    (void)pi; (void)user_gpio; (void)range;
    return 0;
}

int set_PWM_dutycycle(int pi, unsigned user_gpio, unsigned dutycycle) {
    (void)pi;
    int i = indice_in(user_gpio);
    if (i < 0) return -1;                  /* no hay motor en ese pin */
    g_gpio_in[i] = dutycycle > PWM_RANGO ? PWM_RANGO : (int)dutycycle;
    empujar_motores();
    return 0;
}

int gpio_write(int pi, unsigned gpio, unsigned level) {
    (void)pi;
    int nivel = level ? 1 : 0;

    switch (gpio) {
        case IN1: case IN2: case IN3: case IN4:
            g_gpio_in[indice_in(gpio)] = nivel ? PWM_RANGO : 0;
            empujar_motores();
            return 0;

        case LED_POWER_PIN:      case LED_AUTONOMOUS_PIN:
        case LED_MANUAL_PIN:     case LED_OBSTACLE_PIN: {
            static const char *nombre[] = { "encendido", "autonomo", "manual", "obstaculo" };
            int idx = gpio == LED_POWER_PIN      ? 0 :
                      gpio == LED_AUTONOMOUS_PIN ? 1 :
                      gpio == LED_MANUAL_PIN     ? 2 : 3;
            if (g_leds[idx] != nivel) {
                g_leds[idx] = nivel;
                if (g_verboso)
                    printf("[sim] LED %-10s %s\n", nombre[idx], nivel ? "ENCENDIDO" : "apagado");
            }
            return 0;
        }
        default: break;
    }

    if (gpio == RADAR_TRIG) {
        SensorSim *s = &g_sensor;
        if (nivel) {
            s->fase = S_ARMADO;
        } else if (s->fase == S_ARMADO) {
            /* Flanco de bajada del TRIG: el HC-SR04 dispara y prepara el eco.
               Mide hacia donde apunta el servo EN ESTE INSTANTE: si la
               biblioteca dispara antes de que llegue, mide la direccion
               equivocada, igual que en el robot real. Servo a 90 = frente;
               0 = derecha (+90 en el mundo); 180 = izquierda (-90). */
            pthread_mutex_lock(&g_lock_sim);
            double servo = servo_angulo_real();
            pthread_mutex_unlock(&g_lock_sim);

            double d = mundo_distancia_desde(SIM_SENSOR_ADELANTE_CM, 90.0 - servo);
            s->pulso_us = (d < 0.0) ? 0 : (uint32_t)(d * US_POR_CM);
            s->fase     = S_ESPERA_BAJO;
        }
        return 0;
    }
    return 0;
}

int set_pull_up_down(int pi, unsigned gpio, unsigned pud) {
    (void)pi; (void)gpio; (void)pud;
    return 0;
}

int gpio_read(int pi, unsigned gpio) {
    (void)pi;
    if (gpio == CAIDA_IZQ_PIN) return mundo_sin_piso(CAIDA_ADELANTE_CM,  CAIDA_LATERAL_CM);
    if (gpio == CAIDA_DER_PIN) return mundo_sin_piso(CAIDA_ADELANTE_CM, -CAIDA_LATERAL_CM);
    if (gpio != RADAR_ECHO) return 0;
    SensorSim *s = &g_sensor;

    switch (s->fase) {
        case S_ESPERA_BAJO:
            if (s->pulso_us == 0) {
                /* Sin eco: el reloj corre hasta que el lazo se rinde por timeout. */
                g_tick_extra += 10000;
                return 0;
            }
            s->fase = S_PULSO_ALTO;
            return 0;                       /* ultimo instante en bajo */

        case S_PULSO_ALTO:
            g_tick_extra += s->pulso_us;    /* el eco duro esto */
            s->fase = S_PULSO_FIN;
            return 1;

        case S_PULSO_FIN:
            s->fase = S_IDLE;
            return 0;                       /* flanco de bajada: fin de la medicion */

        default:
            return 0;
    }
}

uint32_t get_current_tick(int pi) {
    (void)pi;
    if (!g_reloj_listo) return g_tick_extra;

    struct timespec ahora;
    clock_gettime(CLOCK_MONOTONIC, &ahora);
    uint64_t us = (uint64_t)(ahora.tv_sec - g_t0.tv_sec) * 1000000ULL +
                  (ahora.tv_nsec - g_t0.tv_nsec) / 1000ULL;
    return (uint32_t)(us + g_tick_extra);
}

/* ── Servo ───────────────────────────────────────────────────────────────── */

int set_servo_pulsewidth(int pi, unsigned user_gpio, unsigned pulsewidth) {
    (void)pi;
    if (user_gpio != SERVO_PIN) return PI_BAD_USER_GPIO;   /* no hay servo ahi */
    if (pulsewidth != 0 &&
        (pulsewidth < SERVO_PULSO_MIN || pulsewidth > SERVO_PULSO_MAX))
        return PI_BAD_PULSEWIDTH;

    pthread_mutex_lock(&g_lock_sim);
    double ahora = servo_angulo_real();
    g_servo_desde = ahora;
    /* Sin pulsos (0) el servo queda suelto donde estaba. */
    g_servo_hacia = pulsewidth == 0 ? ahora
                  : (pulsewidth - SERVO_PULSO_MIN) * 180.0 /
                    (SERVO_PULSO_MAX - SERVO_PULSO_MIN);
    clock_gettime(CLOCK_MONOTONIC, &g_servo_t);
    g_servo_pulso = pulsewidth;
    pthread_mutex_unlock(&g_lock_sim);
    return 0;
}

int get_servo_pulsewidth(int pi, unsigned user_gpio) {
    (void)pi;
    if (user_gpio != SERVO_PIN) return PI_BAD_USER_GPIO;
    pthread_mutex_lock(&g_lock_sim);
    int p = (int)g_servo_pulso;
    pthread_mutex_unlock(&g_lock_sim);
    return p;
}

/* ── I2C: MPU-6050 ───────────────────────────────────────────────────────── */

int i2c_open(int pi, unsigned i2c_bus, unsigned i2c_addr, unsigned i2c_flags) {
    (void)pi; (void)i2c_flags;
    if (i2c_bus != MPU_BUS || i2c_addr != MPU_DIR) return PI_I2C_OPEN_FAILED;
    pthread_mutex_lock(&g_lock_sim);
    g_mpu_abierto = 1;
    g_mpu_dormido = 1;
    pthread_mutex_unlock(&g_lock_sim);
    return MPU_HANDLE;
}

int i2c_close(int pi, unsigned handle) {
    (void)pi;
    if (handle != MPU_HANDLE) return PI_BAD_HANDLE;
    pthread_mutex_lock(&g_lock_sim);
    g_mpu_abierto = 0;
    pthread_mutex_unlock(&g_lock_sim);
    return 0;
}

int i2c_write_byte_data(int pi, unsigned handle, unsigned i2c_reg, unsigned bVal) {
    (void)pi;
    if (handle != MPU_HANDLE || !g_mpu_abierto) return PI_BAD_HANDLE;
    /* PWR_MGMT_1: el bit 6 (SLEEP) duerme el sensor. Los demas registros de
       configuracion se aceptan sin efecto: el simulado no tiene ruido de
       motores que filtrar. */
    if (i2c_reg == 0x6B) {
        pthread_mutex_lock(&g_lock_sim);
        g_mpu_dormido = (bVal & 0x40) != 0;
        pthread_mutex_unlock(&g_lock_sim);
    }
    return 0;
}

int i2c_read_byte_data(int pi, unsigned handle, unsigned i2c_reg) {
    (void)pi;
    if (handle != MPU_HANDLE || !g_mpu_abierto) return PI_BAD_HANDLE;
    if (i2c_reg == 0x75) return MPU_DIR;                    /* WHO_AM_I */
    if (i2c_reg == 0x6B) return g_mpu_dormido ? 0x40 : 0;   /* PWR_MGMT_1 */
    return 0;
}

int i2c_read_i2c_block_data(int pi, unsigned handle, unsigned i2c_reg,
                            char *buf, unsigned count)
{
    (void)pi;
    if (handle != MPU_HANDLE || !g_mpu_abierto) return PI_BAD_HANDLE;
    if (i2c_reg != 0x3B || count != 14) return PI_I2C_READ_FAILED;

    memset(buf, 0, count);

    pthread_mutex_lock(&g_lock_sim);
    int dormido = g_mpu_dormido;
    pthread_mutex_unlock(&g_lock_sim);
    if (dormido) return (int)count;          /* dormido no mide: todo en cero */

    double avance, lateral, giro;
    mundo_imu(&avance, &lateral, &giro);

    /* Del mundo al marco del sensor: X adelante, Y a la izquierda, Z arriba.
       El giroscopo mide positivo el giro antihorario, al reves del rumbo. */
    pthread_mutex_lock(&g_lock_sim);
    double ax = avance  / 980.665 + MPU_SESGO_AX_G + ruido(MPU_RUIDO_G);
    double ay = lateral / 980.665 + ruido(MPU_RUIDO_G);
    double az = 1.0 + ruido(MPU_RUIDO_G);
    double gz = -giro + MPU_SESGO_GZ_DPS + ruido(MPU_RUIDO_DPS);
    pthread_mutex_unlock(&g_lock_sim);

    poner_be16(buf + 0,  ax * 16384.0);
    poner_be16(buf + 2,  ay * 16384.0);
    poner_be16(buf + 4,  az * 16384.0);
    poner_be16(buf + 6,  (25.0 - 36.53) * 340.0);   /* 25 grados C */
    poner_be16(buf + 12, gz * 131.0);
    return (int)count;
}
