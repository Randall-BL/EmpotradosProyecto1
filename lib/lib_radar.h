/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#ifndef LIB_RADAR_H
#define LIB_RADAR_H

#include <stdint.h>

/*
 * Radar ultrasonico: un HC-SR04 montado sobre el servo de 180 grados.
 *
 * Un hilo propio barre el servo de lado a lado, de 0 a 180 grados y de vuelta,
 * en pasos de RADAR_PASO_GRADOS, y dispara el sensor en cada posicion. De cada
 * angulo guarda la ultima lectura junto con la pose del robot en el instante
 * de medir (lib_odom): es lo que necesita el mapa para proyectar el obstaculo
 * sobre la grilla aunque el robot se haya movido desde entonces.
 *
 * Tiempo antes de chocar: cada vez que el sensor apunta al frente (90 grados)
 * y detecta un obstaculo, se congela la distancia medida junto con el avance
 * de la odometria en ese instante. La estimacion vigente es
 *
 *     t = (distancia medida - lo que el robot avanzo desde entonces) / v
 *
 * con v la velocidad hacia adelante que estima el MPU-6050 (lib_odom). Entre
 * dos lecturas frontales la estimacion no se congela: sigue bajando conforme
 * el robot se acerca, y se invalida si el robot gira o se detiene.
 *
 * Angulos del servo: 0 = derecha, 90 = frente, 180 = izquierda.
 */

#define RADAR_PASO_GRADOS 30
#define RADAR_N_ANGULOS   (180 / RADAR_PASO_GRADOS + 1)   /* 0, 30, ..., 180 */
#define RADAR_FRENTE       90

/** Pines del HC-SR04 (BCM). El ECHO va con divisor 1k/2k: saca 5 V. */
#define RADAR_TRIG_GPIO 17
#define RADAR_ECHO_GPIO 27

/** Distancia del centro del robot al eje del servo, hacia adelante, en cm. */
#define RADAR_EJE_ADELANTE_CM 10.0

/** Por debajo de esta velocidad el robot no se esta acercando a nada. */
#define RADAR_V_MIN_CM_S 2.0

/** Una lectura frontal mas vieja que esto ya no sostiene un tiempo de choque. */
#define RADAR_TTC_VIGENCIA_S 2.0

/** Si el robot giro mas que esto desde la lectura frontal, ese obstaculo ya
    no esta al frente y su tiempo de choque no aplica. */
#define RADAR_TTC_GIRO_MAX_GRADOS 20.0

/** Ultima lectura de un angulo del barrido. */
typedef struct {
    int      angulo;        /**< del servo, 0..180 */
    double   distancia_cm;  /**< -1 si no volvio eco (nada en 4 m, o rebote perdido) */
    double   edad_s;        /**< segundos desde que se midio */
    uint32_t seq;           /**< numero de lectura; crece con cada una, 0 = nunca medido */
    double   x_cm, y_cm, rumbo_grados;   /**< pose del robot al medir */
} RadarLectura;

/** Estado del tiempo antes de chocar con lo que hay al frente. */
typedef struct {
    int    hay_obstaculo;   /**< la ultima lectura frontal vio algo */
    double distancia_cm;    /**< distancia de esa lectura */
    double velocidad_cm_s;  /**< velocidad del robot al medirla */
    double edad_s;          /**< segundos desde esa lectura */
    double ttc_s;           /**< estimacion vigente en segundos; -1 = sin riesgo */
} RadarChoque;

/**
 * @brief Configura el sensor y el servo y lanza el hilo de barrido.
 *
 * Necesita la odometria corriendo (odom_arrancar), porque cada lectura se
 * guarda con la pose del robot. robot_init() hace todo en el orden correcto.
 *
 * @return 0 si el hilo quedo corriendo, -1 si no.
 */
int radar_iniciar(int pi);

/** Detiene el barrido, suelta el servo y deja el TRIG en bajo. Idempotente. */
void radar_detener(void);

/**
 * @brief Pausa (1) o reanuda (0) el barrido.
 *
 * El hilo termina la medicion en curso y se queda quieto. Sirve para apuntar
 * el sensor a mano con servo_mover() durante la calibracion y las pruebas.
 */
void radar_pausar(int pausado);

/** Angulo al que apunta el servo ahora, 0..180. */
int radar_angulo_actual(void);

/**
 * @brief Copia la ultima lectura de cada angulo, de 0 a 180.
 * @return Cuantas copio: RADAR_N_ANGULOS, o menos si @p max es menor.
 */
int radar_lecturas(RadarLectura *out, int max);

/** Ultima distancia medida en @p angulo, en cm; -1 sin eco o sin medir. */
double radar_distancia(int angulo);

/** Numero de la ultima lectura tomada; sirve de marca para radar_barrido_completo(). */
uint32_t radar_seq(void);

/** 1 si todos los angulos tienen una lectura posterior a @p desde_seq. */
int radar_barrido_completo(uint32_t desde_seq);

/**
 * @brief Bloquea hasta que todos los angulos se hayan medido de nuevo.
 * @return 0 al completarse el barrido, -1 si se agoto @p timeout_ms.
 */
int radar_esperar_barrido(int timeout_ms);

/** Segundos estimados antes de chocar con lo que hay al frente; -1 sin riesgo. */
double radar_tiempo_choque(void);

/** Detalle del tiempo de choque: la lectura que lo origino y la estimacion. */
void radar_estado_choque(RadarChoque *out);

/**
 * @brief Rayo de una lectura en coordenadas del mundo (las de lib_odom).
 *
 * El origen es el sensor —el eje del servo, adelante del centro del robot—
 * y la direccion es un rumbo de brujula. El eco, si lo hubo, esta en
 * origen + distancia * (sin(rumbo), cos(rumbo)).
 */
void radar_rayo(const RadarLectura *l, double *origen_x_cm, double *origen_y_cm,
                double *rumbo_rayo_grados);

#endif /* LIB_RADAR_H */
