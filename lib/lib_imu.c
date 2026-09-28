/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#include "lib_imu.h"

#include <pigpiod_if2.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

/* Registros del MPU-6050 (mapa de registros RM-MPU-6000A-00, rev. 4.2). */
#define REG_SMPLRT_DIV   0x19
#define REG_CONFIG       0x1A
#define REG_GYRO_CONFIG  0x1B
#define REG_ACCEL_CONFIG 0x1C
#define REG_ACCEL_XOUT_H 0x3B
#define REG_PWR_MGMT_1   0x6B
#define REG_WHO_AM_I     0x75

#define WHO_AM_I_MPU6050 0x68

/* PWR_MGMT_1 = 1: despierto, con el PLL del giroscopo X como reloj, que es
   mas estable que el oscilador interno de 8 MHz. */
#define PWR_DESPIERTO_PLL_X 0x01

/* CONFIG = 5: filtro pasabajos de 10 Hz en acelerometro y giroscopo. Lo que
   interesa es el movimiento del chasis, no la vibracion de los motores, y la
   odometria muestrea a 50 Hz: 10 Hz queda holgado bajo Nyquist. */
#define DLPF_10_HZ 0x05

/* SMPLRT_DIV = 9: 1 kHz / (1 + 9) = 100 muestras por segundo. */
#define DIVISOR_100_HZ 9

/* Rangos de +-2 g y +-250 grados/s: el robot nunca se acerca a sus limites,
   asi que se usa la escala mas fina. */
#define LSB_POR_G   16384.0
#define LSB_POR_DPS   131.0
#define G_CM_S2       980.665

typedef struct {
    double ax_g, ay_g, az_g;
    double gz_dps;
    double temp_c;
} Crudo;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_pi = -1;
static int g_h  = -1;   /* handle de pigpiod para el dispositivo I2C */

static double g_sesgo_ax = 0.0, g_sesgo_ay = 0.0, g_sesgo_gz = 0.0;

static int16_t be16(const char *b) {
    return (int16_t)(((uint8_t)b[0] << 8) | (uint8_t)b[1]);
}

/* Una rafaga de 14 bytes trae las tres aceleraciones, la temperatura y los
   tres giros de la misma muestra: leerlos por separado mezclaria instantes. */
static int leer_crudo(int pi, int h, Crudo *c) {
    char b[14];
    if (i2c_read_i2c_block_data(pi, (unsigned)h, REG_ACCEL_XOUT_H, b, sizeof(b))
            != (int)sizeof(b))
        return -1;

    c->ax_g   = be16(b + 0)  / LSB_POR_G;
    c->ay_g   = be16(b + 2)  / LSB_POR_G;
    c->az_g   = be16(b + 4)  / LSB_POR_G;
    c->temp_c = be16(b + 6)  / 340.0 + 36.53;
    c->gz_dps = be16(b + 12) / LSB_POR_DPS;
    return 0;
}

static int escribir(int pi, int h, unsigned reg, unsigned valor) {
    return i2c_write_byte_data(pi, (unsigned)h, reg, valor) == 0 ? 0 : -1;
}

int imu_init(int pi) {
    pthread_mutex_lock(&g_lock);
    if (g_h >= 0) { pthread_mutex_unlock(&g_lock); return 0; }
    pthread_mutex_unlock(&g_lock);

    int h = i2c_open(pi, IMU_BUS_I2C, IMU_DIRECCION, 0);
    if (h < 0) {
        fprintf(stderr, "[imu] no se pudo abrir /dev/i2c-%d (codigo %d): "
                        "revisar dtparam=i2c_arm=on y el modulo i2c-dev\n",
                IMU_BUS_I2C, h);
        return -1;
    }

    int quien = i2c_read_byte_data(pi, (unsigned)h, REG_WHO_AM_I);
    if (quien < 0) {
        fprintf(stderr, "[imu] nadie contesta en 0x%02x: revisar el cableado "
                        "de SDA/SCL y que AD0 este a GND\n", IMU_DIRECCION);
        i2c_close(pi, (unsigned)h);
        return -1;
    }
    /* Muchos modulos GY-521 traen clones (0x70, 0x72, 0x98) con el mismo mapa
       de registros para lo que se usa aqui: se avisa pero se sigue. */
    if (quien != WHO_AM_I_MPU6050)
        fprintf(stderr, "[imu] WHO_AM_I = 0x%02x (se esperaba 0x68): "
                        "se asume un clon compatible\n", quien);

    if (escribir(pi, h, REG_PWR_MGMT_1,   PWR_DESPIERTO_PLL_X) < 0 ||
        escribir(pi, h, REG_CONFIG,       DLPF_10_HZ)          < 0 ||
        escribir(pi, h, REG_SMPLRT_DIV,   DIVISOR_100_HZ)      < 0 ||
        escribir(pi, h, REG_GYRO_CONFIG,  0x00)                < 0 ||
        escribir(pi, h, REG_ACCEL_CONFIG, 0x00)                < 0) {
        fprintf(stderr, "[imu] el sensor no acepto la configuracion\n");
        i2c_close(pi, (unsigned)h);
        return -1;
    }

    /* El PLL tarda unos milisegundos en engancharse tras despertar. */
    usleep(50000);

    pthread_mutex_lock(&g_lock);
    g_pi = pi;
    g_h  = h;
    g_sesgo_ax = g_sesgo_ay = g_sesgo_gz = 0.0;
    pthread_mutex_unlock(&g_lock);

    printf("[imu] MPU-6050 listo en /dev/i2c-%d, direccion 0x%02x\n",
           IMU_BUS_I2C, IMU_DIRECCION);
    return 0;
}

int imu_calibrar(int muestras) {
    pthread_mutex_lock(&g_lock);
    int pi = g_pi, h = g_h;
    pthread_mutex_unlock(&g_lock);
    if (h < 0 || muestras <= 0) return -1;

    double sx = 0.0, sy = 0.0, sgz = 0.0;
    int validas = 0;

    for (int i = 0; i < muestras; i++) {
        Crudo c;
        if (leer_crudo(pi, h, &c) == 0) {
            sx  += c.ax_g;
            sy  += c.ay_g;
            sgz += c.gz_dps;
            validas++;
        }
        usleep(5000);
    }
    if (validas == 0) return -1;

    pthread_mutex_lock(&g_lock);
    g_sesgo_ax = sx  / validas;
    g_sesgo_ay = sy  / validas;
    g_sesgo_gz = sgz / validas;
    printf("[imu] calibrado con %d muestras: sesgo ax %+.4f g, ay %+.4f g, "
           "gz %+.2f grados/s\n", validas, g_sesgo_ax, g_sesgo_ay, g_sesgo_gz);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

int imu_leer(ImuLectura *out) {
    pthread_mutex_lock(&g_lock);
    int pi = g_pi, h = g_h;
    double sx = g_sesgo_ax, sy = g_sesgo_ay, sgz = g_sesgo_gz;
    pthread_mutex_unlock(&g_lock);
    if (h < 0 || !out) return -1;

    Crudo c;
    if (leer_crudo(pi, h, &c) < 0) return -1;

    out->avance_cm_s2  = IMU_SIGNO_AVANCE * (c.ax_g - sx) * G_CM_S2;
    out->lateral_cm_s2 = IMU_SIGNO_AVANCE * (c.ay_g - sy) * G_CM_S2;
    out->vertical_g    = c.az_g;
    out->giro_dps      = IMU_SIGNO_GIRO * (c.gz_dps - sgz);
    out->temperatura_c = c.temp_c;
    return 0;
}

int imu_disponible(void) {
    pthread_mutex_lock(&g_lock);
    int ok = g_h >= 0;
    pthread_mutex_unlock(&g_lock);
    return ok;
}

void imu_cerrar(void) {
    pthread_mutex_lock(&g_lock);
    if (g_h >= 0) i2c_close(g_pi, (unsigned)g_h);
    g_h  = -1;
    g_pi = -1;
    pthread_mutex_unlock(&g_lock);
}
