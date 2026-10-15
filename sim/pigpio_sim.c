/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 */

/*
 * Un pigpiod falso. Implementa las mismas funciones que la biblioteca llama
 * (gpio_write, gpio_read, set_servo_pulsewidth, i2c_*...), pero en vez de
 * mover pines mueve un robot virtual de mundo.c.
 *
 * Como se engancha sin tocar lib/: la biblioteca hace #include
 * <pigpiod_if2.h>. Al compilar con -I. dentro de sim/, el compilador encuentra
 * primero el pigpiod_if2.h de esta carpeta. Y al enlazar, las funciones salen
 * de este archivo en vez de la biblioteca real de pigpio.
 *
 * Estado que guarda:
 *
 * - g_gpio_in[4]: lo que la biblioteca escribio en IN1-IN4.
 * - g_leds[4].
 * - Un reloj virtual: g_t0 (arranque) y g_tick_extra (microsegundos que suman
 *   los ecos simulados).
 * - g_sensor: la fase de una medicion del HC-SR04 (una pequena maquina de
 *   estados).
 * - El servo: de donde sale, a donde va, desde cuando y el ultimo pulso.
 * - El MPU: abierto, dormido, y un generador de ruido repetible.
 *
 * Constantes "fisicas" del simulador: servo a 600  grados/s, sesgo del MPU de
 * 0,02 g y 1,5  grados/s, sensores de desnivel 12 cm adelante y 8 cm a cada
 * lado, 58,3 us de eco por centimetro.
 */

/* La misma interfaz que ve la biblioteca: aqui se implementa. */
#include "pigpiod_if2.h"
/* El mundo virtual sobre el que se mide y se mueve. */
#include "mundo.h"

/* fabs y lround. */
#include <math.h>
/* Mutex del servo y del MPU simulados. */
#include <pthread.h>
/* printf. */
#include <stdio.h>
/* memset. */
#include <string.h>
/* clock_gettime. */
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
/* Los mismos GPIO que usa lib_motors.c. */
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
/* Posicion de cada sensor respecto del centro del robot: 12 cm adelante y
   8 cm a cada lado. */
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
/* Instante de arranque del reloj. */
static struct timespec g_t0;
/* Microsegundos extra, sumados por los ecos simulados. */
static uint32_t g_tick_extra = 0;
/* 1 cuando g_t0 ya esta fijado. */
static int g_reloj_listo = 0;

/* ── Sensores ────────────────────────────────────────────────────────────── */

/* Fases de una medicion del HC-SR04 simulado: en reposo, TRIG en alto,
   esperando que ECHO suba, ECHO en alto y fin del pulso. */
typedef enum { S_IDLE = 0, S_ARMADO, S_ESPERA_BAJO, S_PULSO_ALTO, S_PULSO_FIN } FaseSensor;

/* Estado del sensor: la fase y la duracion del eco de la medicion en curso. */
typedef struct {
    FaseSensor fase;
    uint32_t   pulso_us;       /* 0 = sin eco */
} SensorSim;

/* Un solo HC-SR04: la direccion en que mide la decide el servo. */
static SensorSim g_sensor = { S_IDLE, 0 };

/* ── Servo ───────────────────────────────────────────────────────────────── */

/* Protege el estado del servo y del MPU. */
static pthread_mutex_t g_lock_sim = PTHREAD_MUTEX_INITIALIZER;

static double          g_servo_desde = 90.0;   /* se mueve de aqui ...      */
static double          g_servo_hacia = 90.0;   /* ... hasta aca             */
static struct timespec g_servo_t;              /* desde este instante       */
/* Ultimo ancho de pulso recibido; 0 = sin pulsos. */
static unsigned        g_servo_pulso = 0;

/* Segundos transcurridos desde t. */
static double segundos_desde(const struct timespec *t) {
    /* Segundos desde un instante, igual que en lib_radar.c. */

    struct timespec ahora;
    clock_gettime(CLOCK_MONOTONIC, &ahora);
    return (ahora.tv_sec - t->tv_sec) + (ahora.tv_nsec - t->tv_nsec) / 1e9;
}

/* Donde esta el servo ahora: viaja a velocidad constante hacia el destino.
   Con g_lock_sim tomado. */
static double servo_angulo_real(void) {
    /*
     * Donde esta el servo ahora mismo. El servo viaja a 600  grados/s: lo
     * recorrido = 600 x segundos desde la orden. Si ya recorrio lo que
     * faltaba, esta en el destino; si no, en un punto intermedio.
     *
     * Esto hace que, si la biblioteca disparara el sensor antes de que el
     * servo llegue, mida en la direccion equivocada, igual que en el robot
     * real.
     */

    /* Grados que pudo recorrer desde que recibio la orden. */
    double recorrido = SERVO_GRADOS_POR_S * segundos_desde(&g_servo_t);
    /* Grados que le faltaban al recibirla. */
    double falta     = g_servo_hacia - g_servo_desde;
    /* Si ya recorrio lo que faltaba, esta en el destino; si no, en un punto
       intermedio. */
    if (fabs(falta) <= recorrido) return g_servo_hacia;
    return g_servo_desde + (falta > 0 ? recorrido : -recorrido);
}

/* ── MPU-6050 ────────────────────────────────────────────────────────────── */

/* 1 mientras el dispositivo I2C esta abierto. */
static int g_mpu_abierto = 0;
static int g_mpu_dormido = 1;      /* el sensor arranca dormido */
static uint32_t g_ruido = 12345;   /* generador congruencial: ruido repetible */

/* Ruido uniforme en [-amplitud, amplitud]. */
static double ruido(double amplitud) {
    /*
     * Un numero "aleatorio" entre -amplitud y +amplitud, con un generador
     * congruencial (x = x x 1103515245 + 12345). Siempre da la misma
     * secuencia, asi las pruebas son repetibles.
     */

    /* Generador congruencial lineal: el mismo ruido en cada corrida. */
    g_ruido = g_ruido * 1103515245u + 12345u;
    /* Toma 16 bits del estado y los lleva al rango [-1, 1]. */
    return amplitud * ((((g_ruido >> 8) & 0xFFFF) / 32767.5) - 1.0);
}

/* Escribe un valor como entero de 16 bits con signo, en big-endian,
   saturando al rango. */
static void poner_be16(char *b, double valor) {
    /*
     * Lo contrario de be16 de lib_imu.c: redondea un valor, lo recorta a
     * -32768..32767 y lo escribe en dos bytes, el alto primero.
     */

    long v = lround(valor);
    if (v >  32767) v =  32767;
    if (v < -32768) v = -32768;
    b[0] = (char)((v >> 8) & 0xFF);
    b[1] = (char)(v & 0xFF);
}

/* ── Motores ─────────────────────────────────────────────────────────────── */

/* Indice de IN1..IN4 en g_gpio_in, o -1 si el GPIO no es de un motor. */
static int indice_in(unsigned gpio) {
    /*
     * Convierte un numero de GPIO (5, 6, 23, 24) en su indice 0-3 con un
     * switch, o -1 si no es un pin de motor.
     */

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
    /*
     * Lo que le llega al L298N: 255 - lo escrito en el GPIO. Imita la
     * inversion del optoacoplador, para comprobar que la biblioteca la
     * compensa.
     */

    return PWM_RANGO - g_gpio_in[i];
}

/* Con el puente siempre habilitado, cada motor es empujado hacia adelante la
   fraccion del ciclo en que solo IN1 esta en alto, hacia atras la fraccion en
   que solo IN2 lo esta, y frenado el resto (las dos iguales). Con una sola
   entrada modulando por vez, el neto es la diferencia de los dos ciclos. */
static void empujar_motores(void) {
    /*
     * Calcula la orden neta de cada motor (entrada de avance - entrada de
     * retroceso) y se la pasa al mundo con mundo_set_motores. Izquierdo en
     * IN3/IN4, derecho en IN1/IN2, como en el robot armado.
     */

    /* Como en el robot armado: el motor izquierdo va a la salida B del L298N
       (IN3/IN4) y el derecho a la A (IN1/IN2). */
    mundo_set_motores(entrada_l298n(2) - entrada_l298n(3),
                      entrada_l298n(0) - entrada_l298n(1));
}

/* ── API de pigpio ───────────────────────────────────────────────────────── */

/* Abre la "sesion": reinicia el mundo, los pines, el servo, el MPU y el
   reloj. Devuelve un handle valido. */
int pigpio_start(const char *addrStr, const char *portStr) {
    /*
     * Abre la "sesion": reinicia el mundo (robot al centro), los pines, el
     * servo (a 90 grados, sin pulsos), el MPU (cerrado y dormido) y el reloj
     * virtual. Devuelve 1.
     */

    /* La direccion y el puerto del demonio no aplican. */
    (void)addrStr; (void)portStr;
    /* Robot en el centro, quieto. */
    mundo_init();
    /* LEDs apagados y entradas de motor en bajo. */
    memset(g_leds, 0, sizeof(g_leds));
    memset(g_gpio_in, 0, sizeof(g_gpio_in));
    g_sensor.fase = S_IDLE;

    /* Servo al frente y sin pulsos; MPU cerrado y dormido. */
    pthread_mutex_lock(&g_lock_sim);
    g_servo_desde = g_servo_hacia = 90.0;
    g_servo_pulso = 0;
    clock_gettime(CLOCK_MONOTONIC, &g_servo_t);
    g_mpu_abierto = 0;
    g_mpu_dormido = 1;
    pthread_mutex_unlock(&g_lock_sim);

    /* El reloj virtual arranca ahora. */
    clock_gettime(CLOCK_MONOTONIC, &g_t0);
    g_tick_extra  = 0;
    g_reloj_listo = 1;
    printf("[sim] pigpiod simulado: robot con radar y MPU-6050 en el centro de una sala de 4x4 m\n");
    return 1;                        /* handle cualquiera, pero >= 0 */
}

/* Cierra la sesion: frena el robot virtual. */
void pigpio_stop(int pi) {
    /* Frena el robot virtual. */

    (void)pi;
    mundo_set_motores(0, 0);
    printf("[sim] pigpiod simulado cerrado\n");
}

/* El modo de los pines no se simula. */
int set_mode(int pi, unsigned gpio, unsigned mode) {
    /*
     * No hace nada: el modo de los pines no se simula. Existe para que la
     * biblioteca compile.
     */

    (void)pi; (void)gpio; (void)mode;
    return 0;
}

/* La frecuencia de la PWM no se simula. */
int set_PWM_frequency(int pi, unsigned user_gpio, unsigned frequency) {
    /* No hace nada (no se simula). */

    (void)pi; (void)user_gpio; (void)frequency;
    return 0;
}

/* El rango de la PWM se da por sentado: PWM_RANGO. */
int set_PWM_range(int pi, unsigned user_gpio, unsigned range) {
    /* No hace nada (se asume rango 255). */

    (void)pi; (void)user_gpio; (void)range;
    return 0;
}

/* Fija el ciclo de trabajo de una entrada de motor y recalcula el empuje. */
int set_PWM_dutycycle(int pi, unsigned user_gpio, unsigned dutycycle) {
    /*
     * Guarda el ciclo de trabajo de una entrada de motor y recalcula el
     * empuje. Solo se usaria con la variante con PWM.
     */

    (void)pi;
    int i = indice_in(user_gpio);
    if (i < 0) return -1;                  /* no hay motor en ese pin */
    /* Satura al rango. */
    g_gpio_in[i] = dutycycle > PWM_RANGO ? PWM_RANGO : (int)dutycycle;
    empujar_motores();
    return 0;
}

/* Escritura digital de un pin: motores, LEDs o el TRIG del sensor. */
int gpio_write(int pi, unsigned gpio, unsigned level) {
    /*
     * Escribe un pin. Segun cual:
     *
     * - IN1-IN4: guarda 255 o 0 y recalcula los motores.
     * - LEDs: guarda el estado e imprime [sim] LED ... ENCENDIDO cuando
     *   cambia.
     * - TRIG del sensor:
     *    - Al subir: la medicion queda "armada".
     *    - Al bajar (estando armada): dispara. Lee donde esta el servo en ese
     *      instante, pregunta al mundo la distancia en esa direccion
     *      (mundo_distancia_desde) y la convierte en duracion del eco
     *      (distancia x 58,3 us). Pasa a la fase "esperando que ECHO suba".
     */

    (void)pi;
    /* Normaliza a 0 o 1. */
    int nivel = level ? 1 : 0;

    switch (gpio) {
        case IN1: case IN2: case IN3: case IN4:
            /* Un nivel alto equivale al ciclo de trabajo maximo. */
            g_gpio_in[indice_in(gpio)] = nivel ? PWM_RANGO : 0;
            empujar_motores();
            return 0;

        case LED_POWER_PIN:      case LED_AUTONOMOUS_PIN:
        case LED_MANUAL_PIN:     case LED_OBSTACLE_PIN: {
            /* Nombre de cada LED para el mensaje, e indice del que se
               escribe. */
            static const char *nombre[] = { "encendido", "autonomo", "manual", "obstaculo" };
            int idx = gpio == LED_POWER_PIN      ? 0 :
                      gpio == LED_AUTONOMOUS_PIN ? 1 :
                      gpio == LED_MANUAL_PIN     ? 2 : 3;
            /* Solo se anuncia cuando cambia. */
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
        /* El unico sensor. */
        SensorSim *s = &g_sensor;
        if (nivel) {
            /* Flanco de subida del TRIG: el sensor queda armado. */
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

            /* Mide en el mundo y convierte la distancia en la duracion del
               eco. */
            double d = mundo_distancia_desde(SIM_SENSOR_ADELANTE_CM, 90.0 - servo);
            s->pulso_us = (d < 0.0) ? 0 : (uint32_t)(d * US_POR_CM);
            s->fase     = S_ESPERA_BAJO;
        }
        return 0;
    }
    return 0;
}

/* Las resistencias de pull no se simulan. */
int set_pull_up_down(int pi, unsigned gpio, unsigned pud) {
    /* No hace nada (no se simula). */

    (void)pi; (void)gpio; (void)pud;
    return 0;
}

/* Lectura digital de un pin: sensores de desnivel o el ECHO. */
int gpio_read(int pi, unsigned gpio) {
    /*
     * Lee un pin.
     *
     * - Desnivel: pregunta al mundo si hay piso bajo esa esquina.
     * - ECHO: avanza la maquina de estados del sensor una fase por lectura:
     *    1. "Esperando que suba": si no hubo eco, suma 10 ms al reloj virtual
     *       y devuelve 0 (asi la biblioteca llega a su limite de 50 ms
     *       enseguida). Si hubo eco, pasa a la fase siguiente y devuelve 0.
     *    2. "Pulso en alto": suma la duracion del eco al reloj virtual y
     *       devuelve 1.
     *    3. "Fin": devuelve 0.
     *
     * El truco esta en el paso 2: la biblioteca mide el ancho del pulso con
     * get_current_tick, y como el reloj virtual salto justo la duracion del
     * eco, mide exactamente la distancia correcta sin tener que esperar de
     * verdad.
     */

    (void)pi;
    /* Cada sensor de desnivel consulta si hay piso bajo su posicion. */
    if (gpio == CAIDA_IZQ_PIN) return mundo_sin_piso(CAIDA_ADELANTE_CM,  CAIDA_LATERAL_CM);
    if (gpio == CAIDA_DER_PIN) return mundo_sin_piso(CAIDA_ADELANTE_CM, -CAIDA_LATERAL_CM);
    /* Cualquier otro pin que no sea ECHO se lee en bajo. */
    if (gpio != RADAR_ECHO) return 0;
    SensorSim *s = &g_sensor;

    /* Maquina de estados del pulso de eco: cada lectura de ECHO la hace
       avanzar
       una fase. */
    switch (s->fase) {
        case S_ESPERA_BAJO:
            if (s->pulso_us == 0) {
                /* Sin eco: el reloj corre hasta que el lazo se rinde por timeout. */
                g_tick_extra += 10000;
                return 0;
            }
            /* Con eco: la proxima lectura ya vera ECHO en alto. */
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

/* Reloj de microsegundos: el tiempo real desde el arranque, mas lo que
   duraron los ecos simulados. */
uint32_t get_current_tick(int pi) {
    /*
     * El reloj en microsegundos: tiempo real desde el arranque mas
     * g_tick_extra, lo que sumaron los ecos simulados.
     */

    (void)pi;
    /* Antes de pigpio_start solo cuenta el tiempo extra. */
    if (!g_reloj_listo) return g_tick_extra;

    /* Microsegundos reales desde g_t0. */
    struct timespec ahora;
    clock_gettime(CLOCK_MONOTONIC, &ahora);
    uint64_t us = (uint64_t)(ahora.tv_sec - g_t0.tv_sec) * 1000000ULL +
                  (ahora.tv_nsec - g_t0.tv_nsec) / 1000ULL;
    return (uint32_t)(us + g_tick_extra);
}

/* ── Servo ───────────────────────────────────────────────────────────────── */

/* Recibe el pulso del servo y lo pone a viajar hacia el angulo que le
   corresponde. */
int set_servo_pulsewidth(int pi, unsigned user_gpio, unsigned pulsewidth) {
    /*
     * Recibe el pulso del servo. Rechaza pulsos fuera de 500-2500 (salvo 0).
     * El viaje nuevo parte de donde esta el servo ahora; el destino se calcula
     * del pulso ((pulso - 500) x 180 / 2000). Con pulso 0 se queda donde esta.
     */

    (void)pi;
    if (user_gpio != SERVO_PIN) return PI_BAD_USER_GPIO;   /* no hay servo ahi */
    /* Un pulso distinto de cero tiene que estar en el rango del servo. */
    if (pulsewidth != 0 &&
        (pulsewidth < SERVO_PULSO_MIN || pulsewidth > SERVO_PULSO_MAX))
        return PI_BAD_PULSEWIDTH;

    pthread_mutex_lock(&g_lock_sim);
    /* El viaje nuevo parte de donde esta el servo en este instante. */
    double ahora = servo_angulo_real();
    g_servo_desde = ahora;
    /* Sin pulsos (0) el servo queda suelto donde estaba. */
    g_servo_hacia = pulsewidth == 0 ? ahora
                  : (pulsewidth - SERVO_PULSO_MIN) * 180.0 /
                    (SERVO_PULSO_MAX - SERVO_PULSO_MIN);
    /* Marca el inicio del viaje y recuerda el pulso. */
    clock_gettime(CLOCK_MONOTONIC, &g_servo_t);
    g_servo_pulso = pulsewidth;
    pthread_mutex_unlock(&g_lock_sim);
    return 0;
}

/* Devuelve el ultimo ancho de pulso recibido. La usa la prueba. */
int get_servo_pulsewidth(int pi, unsigned user_gpio) {
    /*
     * Devuelve el ultimo pulso recibido. La prueba lo usa para comprobar que
     * servo_mover(45) mando 1000 us.
     */

    (void)pi;
    if (user_gpio != SERVO_PIN) return PI_BAD_USER_GPIO;
    pthread_mutex_lock(&g_lock_sim);
    int p = (int)g_servo_pulso;
    pthread_mutex_unlock(&g_lock_sim);
    return p;
}

/* ── I2C: MPU-6050 ───────────────────────────────────────────────────────── */

/* Abre el MPU-6050 simulado: solo existe en el bus 1, direccion 0x68. */
int i2c_open(int pi, unsigned i2c_bus, unsigned i2c_addr, unsigned i2c_flags) {
    /*
     * Solo acepta el bus 1 y la direccion 0x68 (si no, falla como fallaria sin
     * sensor). Lo marca abierto y dormido, como el sensor real.
     */

    (void)pi; (void)i2c_flags;
    if (i2c_bus != MPU_BUS || i2c_addr != MPU_DIR) return PI_I2C_OPEN_FAILED;
    pthread_mutex_lock(&g_lock_sim);
    /* Al abrirlo esta dormido, como el sensor real. */
    g_mpu_abierto = 1;
    g_mpu_dormido = 1;
    pthread_mutex_unlock(&g_lock_sim);
    return MPU_HANDLE;
}

/* Cierra el MPU simulado. */
int i2c_close(int pi, unsigned handle) {
    /* Marca el MPU como cerrado. */

    (void)pi;
    if (handle != MPU_HANDLE) return PI_BAD_HANDLE;
    pthread_mutex_lock(&g_lock_sim);
    g_mpu_abierto = 0;
    pthread_mutex_unlock(&g_lock_sim);
    return 0;
}

/* Escribe un registro; solo PWR_MGMT_1 tiene efecto. */
int i2c_write_byte_data(int pi, unsigned handle, unsigned i2c_reg, unsigned bVal) {
    /*
     * Solo el registro 0x6B (energia) tiene efecto: el bit 6 duerme o
     * despierta el sensor. Las demas escrituras se aceptan sin hacer nada.
     */

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

/* Lee un registro: la identidad o el estado de energia. */
int i2c_read_byte_data(int pi, unsigned handle, unsigned i2c_reg) {
    /*
     * Contesta WHO_AM_I (0x68) y el estado de energia. Cualquier otro registro
     * devuelve 0.
     */

    (void)pi;
    if (handle != MPU_HANDLE || !g_mpu_abierto) return PI_BAD_HANDLE;
    if (i2c_reg == 0x75) return MPU_DIR;                    /* WHO_AM_I */
    if (i2c_reg == 0x6B) return g_mpu_dormido ? 0x40 : 0;   /* PWR_MGMT_1 */
    return 0;
}

/* Lee la rafaga de 14 bytes con aceleraciones, temperatura y giros. */
int i2c_read_i2c_block_data(int pi, unsigned handle, unsigned i2c_reg,
                            char *buf, unsigned count)
{
    /*
     * La rafaga de 14 bytes del MPU.
     *
     * 1. Solo acepta la lectura completa desde 0x3B.
     * 2. Si esta dormido, devuelve todo en 0 (por eso imu_init tiene que
     *    despertarlo).
     * 3. Pide al mundo la aceleracion y el giro verdaderos del robot.
     * 4. Les suma el sesgo de fabrica (0,02 g y 1,5  grados/s) y ruido. La
     *    calibracion de la biblioteca tiene que quitar ese sesgo; la prueba lo
     *    comprueba.
     * 5. Pone la gravedad en Z (1 g), 25  C de temperatura, y escribe cada
     *    valor en su posicion con poner_be16. El giro va con el signo
     *    invertido, como lo mide el sensor real.
     */

    (void)pi;
    if (handle != MPU_HANDLE || !g_mpu_abierto) return PI_BAD_HANDLE;
    /* Solo se admite la rafaga completa a partir de ACCEL_XOUT_H. */
    if (i2c_reg != 0x3B || count != 14) return PI_I2C_READ_FAILED;

    /* Todo en cero; despues se llenan los campos que se simulan. */
    memset(buf, 0, count);

    /* Estado de energia, leido bajo el candado. */
    pthread_mutex_lock(&g_lock_sim);
    int dormido = g_mpu_dormido;
    pthread_mutex_unlock(&g_lock_sim);
    if (dormido) return (int)count;          /* dormido no mide: todo en cero */

    /* Aceleraciones y giro verdaderos del robot virtual. */
    double avance, lateral, giro;
    mundo_imu(&avance, &lateral, &giro);

    /* Del mundo al marco del sensor: X adelante, Y a la izquierda, Z arriba.
       El giroscopo mide positivo el giro antihorario, al reves del rumbo. */
    pthread_mutex_lock(&g_lock_sim);
    /* Pasa a g, y suma el sesgo y el ruido del sensor. */
    double ax = avance  / 980.665 + MPU_SESGO_AX_G + ruido(MPU_RUIDO_G);
    double ay = lateral / 980.665 + ruido(MPU_RUIDO_G);
    double az = 1.0 + ruido(MPU_RUIDO_G);
    double gz = -giro + MPU_SESGO_GZ_DPS + ruido(MPU_RUIDO_DPS);
    pthread_mutex_unlock(&g_lock_sim);

    /* Escribe cada medida en su posicion de la rafaga, en cuentas del
       conversor. */
    poner_be16(buf + 0,  ax * 16384.0);
    poner_be16(buf + 2,  ay * 16384.0);
    poner_be16(buf + 4,  az * 16384.0);
    poner_be16(buf + 6,  (25.0 - 36.53) * 340.0);   /* 25 grados C */
    poner_be16(buf + 12, gz * 131.0);
    return (int)count;
}
