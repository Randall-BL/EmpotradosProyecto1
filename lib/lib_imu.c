/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 */

/* Tipo ImuLectura, constantes IMU_* y prototipos. */
#include "lib_imu.h"

/* Cliente de pigpiod: funciones i2c_* para hablar con el sensor. */
#include <pigpiod_if2.h>
/* Mutex que protege el estado del modulo. */
#include <pthread.h>
/* int16_t y uint8_t, para armar las muestras de 16 bits. */
#include <stdint.h>
/* printf y fprintf. */
#include <stdio.h>
/* usleep. */
#include <unistd.h>

/* Registros del MPU-6050 (mapa de registros RM-MPU-6000A-00, rev. 4.2). */
/* Divisor de la frecuencia de muestreo. */
#define REG_SMPLRT_DIV   0x19
/* Filtro pasabajos digital. */
#define REG_CONFIG       0x1A
/* Rango del giroscopo. */
#define REG_GYRO_CONFIG  0x1B
/* Rango del acelerometro. */
#define REG_ACCEL_CONFIG 0x1C
/* Primer registro de la rafaga de 14 bytes con todas las medidas. */
#define REG_ACCEL_XOUT_H 0x3B
/* Control de energia: aqui se despierta al sensor. */
#define REG_PWR_MGMT_1   0x6B
/* Identidad del dispositivo. */
#define REG_WHO_AM_I     0x75

/* Valor que devuelve WHO_AM_I en un MPU-6050 original. */
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
/* Cuentas del conversor por cada g, en el rango de +-2 g. */
#define LSB_POR_G   16384.0
/* Cuentas por cada grado por segundo, en el rango de +-250 grados/s. */
#define LSB_POR_DPS   131.0
/* Gravedad estandar en cm/s2, para pasar de g a cm/s2. */
#define G_CM_S2       980.665

/* Una muestra en unidades fisicas, todavia con el sesgo del sensor. */
typedef struct {
    /* Aceleracion en los tres ejes, en g. */
    double ax_g, ay_g, az_g;
    /* Velocidad de giro alrededor del eje vertical, en grados por segundo. */
    double gz_dps;
    /* Temperatura del chip, en grados Celsius. */
    double temp_c;
} Crudo;

/* Protege la sesion, el handle y los sesgos. */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
/* Sesion con pigpiod. */
static int g_pi = -1;
static int g_h  = -1;   /* handle de pigpiod para el dispositivo I2C */

/* Sesgo medido por imu_calibrar(): se le resta a cada lectura. */
static double g_sesgo_ax = 0.0, g_sesgo_ay = 0.0, g_sesgo_gz = 0.0;

/* Arma un entero de 16 bits con signo a partir de dos bytes en orden
   big-endian, que es como los entrega el sensor: primero el byte alto. */
static int16_t be16(const char *b) {
    return (int16_t)(((uint8_t)b[0] << 8) | (uint8_t)b[1]);
}

/* Una rafaga de 14 bytes trae las tres aceleraciones, la temperatura y los
   tres giros de la misma muestra: leerlos por separado mezclaria instantes. */
static int leer_crudo(int pi, int h, Crudo *c) {
    /* Buffer de la rafaga: 6 bytes de aceleracion, 2 de temperatura y 6 de
       giro. */
    char b[14];
    /* Lee los 14 registros seguidos. Si no llegan los 14 bytes, error. */
    if (i2c_read_i2c_block_data(pi, (unsigned)h, REG_ACCEL_XOUT_H, b, sizeof(b))
            != (int)sizeof(b))
        return -1;

    /* Aceleraciones X, Y y Z: bytes 0 a 5. */
    c->ax_g   = be16(b + 0)  / LSB_POR_G;
    c->ay_g   = be16(b + 2)  / LSB_POR_G;
    c->az_g   = be16(b + 4)  / LSB_POR_G;
    /* Temperatura: bytes 6 y 7, con la formula de la hoja de datos. */
    c->temp_c = be16(b + 6)  / 340.0 + 36.53;
    /* Giro en Z: bytes 12 y 13. Los giros en X e Y (bytes 8 a 11) no se
       usan. */
    c->gz_dps = be16(b + 12) / LSB_POR_DPS;
    return 0;
}

/* Escribe un byte en un registro. Devuelve 0 si pigpiod lo acepto y -1 si
   no. */
static int escribir(int pi, int h, unsigned reg, unsigned valor) {
    return i2c_write_byte_data(pi, (unsigned)h, reg, valor) == 0 ? 0 : -1;
}

/* Abre el sensor y lo configura. Descripcion completa en lib_imu.h. */
int imu_init(int pi) {
    /* Si ya esta abierto no se abre otra vez. */
    pthread_mutex_lock(&g_lock);
    if (g_h >= 0) { pthread_mutex_unlock(&g_lock); return 0; }
    pthread_mutex_unlock(&g_lock);

    /* Abre el dispositivo en el bus I2C: devuelve un handle o un codigo
       negativo. */
    int h = i2c_open(pi, IMU_BUS_I2C, IMU_DIRECCION, 0);
    if (h < 0) {
        fprintf(stderr, "[imu] no se pudo abrir /dev/i2c-%d (codigo %d): "
                        "revisar dtparam=i2c_arm=on y el modulo i2c-dev\n",
                IMU_BUS_I2C, h);
        return -1;
    }

    /* Pregunta la identidad: sirve para saber si hay alguien en esa
       direccion. */
    int quien = i2c_read_byte_data(pi, (unsigned)h, REG_WHO_AM_I);
    if (quien < 0) {
        fprintf(stderr, "[imu] nadie contesta en 0x%02x: revisar el cableado "
                        "de SDA/SCL y que AD0 este a GND\n", IMU_DIRECCION);
        /* El handle se cierra antes de salir con error. */
        i2c_close(pi, (unsigned)h);
        return -1;
    }
    /* Muchos modulos GY-521 traen clones (0x70, 0x72, 0x98) con el mismo mapa
       de registros para lo que se usa aqui: se avisa pero se sigue. */
    if (quien != WHO_AM_I_MPU6050)
        fprintf(stderr, "[imu] WHO_AM_I = 0x%02x (se esperaba 0x68): "
                        "se asume un clon compatible\n", quien);

    /* Configuracion, en orden: despertar, filtro, frecuencia de muestreo y
       los
       dos rangos (0x00 es el mas fino). Basta que una escritura falle. */
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

    /* Publica la sesion y el handle, y parte sin sesgo hasta que se calibre. */
    pthread_mutex_lock(&g_lock);
    g_pi = pi;
    g_h  = h;
    g_sesgo_ax = g_sesgo_ay = g_sesgo_gz = 0.0;
    pthread_mutex_unlock(&g_lock);

    printf("[imu] MPU-6050 listo en /dev/i2c-%d, direccion 0x%02x\n",
           IMU_BUS_I2C, IMU_DIRECCION);
    return 0;
}

/* Mide el sesgo del sensor promediando lecturas con el robot quieto. */
int imu_calibrar(int muestras) {
    /* Copia la sesion y el handle para leer sin retener el candado. */
    pthread_mutex_lock(&g_lock);
    int pi = g_pi, h = g_h;
    pthread_mutex_unlock(&g_lock);
    /* Sin sensor o sin muestras no se puede calibrar. */
    if (h < 0 || muestras <= 0) return -1;

    /* Acumuladores de cada eje y contador de lecturas validas. */
    double sx = 0.0, sy = 0.0, sz = 0.0, sgz = 0.0;
    int validas = 0;

    /* Toma una muestra cada 5 ms. */
    for (int i = 0; i < muestras; i++) {
        Crudo c;
        /* Solo se acumulan las lecturas que el sensor contesto. */
        if (leer_crudo(pi, h, &c) == 0) {
            sx  += c.ax_g;
            sy  += c.ay_g;
            sz  += c.az_g;
            sgz += c.gz_dps;
            validas++;
        }
        usleep(5000);
    }
    /* Si ninguna lectura salio bien, no hay con que calibrar. */
    if (validas == 0) return -1;

    /* El sesgo de cada eje es el promedio de lo leido en reposo. */
    pthread_mutex_lock(&g_lock);
    g_sesgo_ax = sx  / validas;
    g_sesgo_ay = sy  / validas;
    g_sesgo_gz = sgz / validas;
    /* El eje vertical no se calibra, pero se informa: +1 g confirma que el
       modulo quedo con Z hacia arriba; -1 g, que esta dado vuelta. */
    printf("[imu] calibrado con %d muestras: sesgo ax %+.4f g, ay %+.4f g, "
           "gz %+.2f grados/s; vertical %+.2f g\n",
           validas, g_sesgo_ax, g_sesgo_ay, g_sesgo_gz, sz / validas);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

/* Entrega una lectura en unidades fisicas y sin sesgo. */
int imu_leer(ImuLectura *out) {
    /* Copia sesion, handle y sesgos bajo el candado. */
    pthread_mutex_lock(&g_lock);
    int pi = g_pi, h = g_h;
    double sx = g_sesgo_ax, sy = g_sesgo_ay, sgz = g_sesgo_gz;
    pthread_mutex_unlock(&g_lock);
    /* Sin sensor o sin destino, error. */
    if (h < 0 || !out) return -1;

    /* Muestra cruda; se lee fuera del candado. */
    Crudo c;
    if (leer_crudo(pi, h, &c) < 0) return -1;

    /* A cada eje se le quita el sesgo y se pasa a cm/s2, con el signo que
       corresponde al montaje. */
    out->avance_cm_s2  = IMU_SIGNO_AVANCE * (c.ax_g - sx) * G_CM_S2;
    out->lateral_cm_s2 = IMU_SIGNO_AVANCE * (c.ay_g - sy) * G_CM_S2;
    /* La componente vertical se entrega tal cual, en g. */
    out->vertical_g    = c.az_g;
    /* El giro, sin sesgo y con el signo de la brujula. */
    out->giro_dps      = IMU_SIGNO_GIRO * (c.gz_dps - sgz);
    out->temperatura_c = c.temp_c;
    return 0;
}

/* Devuelve 1 si el sensor esta abierto. */
int imu_disponible(void) {
    pthread_mutex_lock(&g_lock);
    int ok = g_h >= 0;
    pthread_mutex_unlock(&g_lock);
    return ok;
}

/* Cierra el dispositivo I2C y deja el modulo sin sensor. */
void imu_cerrar(void) {
    pthread_mutex_lock(&g_lock);
    /* Solo se cierra si habia un handle abierto. */
    if (g_h >= 0) i2c_close(g_pi, (unsigned)g_h);
    g_h  = -1;
    g_pi = -1;
    pthread_mutex_unlock(&g_lock);
}
