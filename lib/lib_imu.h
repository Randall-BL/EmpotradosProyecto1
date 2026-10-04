/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#ifndef LIB_IMU_H
#define LIB_IMU_H

/*
 * MPU-6050: acelerometro y giroscopo de tres ejes, por I2C.
 *
 * Aporta dos magnitudes que el modelo de los motores no puede medir:
 *   - la aceleracion en el eje de avance, que integrada da la velocidad real
 *     del robot (arranques, frenadas, patinadas, choques);
 *   - la velocidad de giro en el eje vertical, que integrada da el rumbo.
 *
 * Este modulo solo lee el sensor y le quita el sesgo. La integracion y la
 * fusion con el modelo de los motores viven en lib_odom, y la velocidad que
 * resulta es la que usa lib_radar para el tiempo antes de chocar.
 *
 * Montaje: plano sobre el chasis, lo mas cerca posible del punto medio entre
 * las dos llantas (ahi el giro no produce aceleracion centripeta), con el eje
 * X hacia el frente y Z hacia arriba. Si se monta de otra forma, ajustar los
 * signos de abajo.
 *
 * El bus se usa a traves de pigpiod (i2c_open y compania), igual que el GPIO:
 * asi el simulador de sim/ lo reemplaza sin tocar este codigo.
 */

/** /dev/i2c-1: SDA en GPIO 2 (pin 3), SCL en GPIO 3 (pin 5). */
#define IMU_BUS_I2C   1

/** Direccion con AD0 a GND. Con AD0 a VCC seria 0x69. */
#define IMU_DIRECCION 0x68

/** Convierte el eje X del sensor en "hacia adelante". -1 si se monto al reves. */
#define IMU_SIGNO_AVANCE 1

/**
 * El giroscopo mide positivo el giro antihorario visto desde arriba (regla de
 * la mano derecha con Z hacia arriba). El rumbo de la odometria es de brujula
 * y crece en sentido horario: de ahi el -1.
 */
#define IMU_SIGNO_GIRO (-1)

/** Muestras que promedia la calibracion; a 5 ms cada una, medio segundo. */
#define IMU_MUESTRAS_CALIBRACION 100

/** Una lectura ya convertida a unidades fisicas y sin sesgo. */
typedef struct {
    double avance_cm_s2;   /**< aceleracion hacia adelante */
    double lateral_cm_s2;  /**< aceleracion hacia la izquierda */
    double vertical_g;     /**< ~1.0 con el robot apoyado en el piso */
    double giro_dps;       /**< grados/s, positivo en sentido horario, como el rumbo */
    double temperatura_c;
} ImuLectura;

/**
 * @brief Abre el sensor en el bus I2C y lo configura.
 *
 * Lo despierta (arranca dormido), fija los rangos en +-2 g y +-250 grados/s
 * y el filtro pasabajos interno en 10 Hz.
 *
 * @return 0 si el sensor contesto, -1 si no hay nada en el bus.
 */
int imu_init(int pi);

/**
 * @brief Mide el sesgo del sensor. **El robot tiene que estar quieto.**
 *
 * Promedia @p muestras lecturas y las toma como el cero de la aceleracion de
 * avance, la lateral y el giro. Tambien absorbe la gravedad que se cuela por
 * una inclinacion leve del chasis.
 *
 * @return 0 si calibro, -1 si el sensor no esta disponible o no contesto.
 */
int imu_calibrar(int muestras);

/** @return 0 y la lectura en @p out, o -1 si el sensor no contesto. */
int imu_leer(ImuLectura *out);

/** @return 1 si imu_init() encontro el sensor, 0 si no. */
int imu_disponible(void);

/** Libera el bus. Es seguro llamarla sin haber llamado a imu_init(). */
void imu_cerrar(void);

#endif /* LIB_IMU_H */
