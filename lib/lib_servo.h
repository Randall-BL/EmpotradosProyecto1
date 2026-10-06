/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#ifndef LIB_SERVO_H
#define LIB_SERVO_H

/*
 * Servo de 180 grados que orienta el sensor ultrasonico del radar.
 *
 * Angulos del servo, vistos desde arriba con el robot mirando al frente:
 * 0 = derecha, 90 = al frente, 180 = izquierda.
 *
 * El pulso lo genera pigpiod por DMA a 50 Hz (set_servo_pulsewidth), asi que
 * no se deforma aunque el proceso tarde en recibir CPU. La senal va directo
 * del GPIO al servo, que se alimenta de los 5 V de la Raspberry Pi: con el
 * GPIO en bajo el servo no recibe pulsos y se queda quieto. Ver
 * docs/hardware-aislamiento.md.
 */

/** GPIO (BCM) de la senal del servo. Pin fisico 22. */
#define SERVO_GPIO 25

/**
 * Pulso que lleva el servo a 0 y a 180 grados, en microsegundos. Varian entre
 * unidades: se calibran en campo (docs/odometria.md).
 */
#define SERVO_PULSO_0_US    500
#define SERVO_PULSO_180_US 2500

/** Velocidad sin carga de un SG90/MG90S a 5 V: unos 0.1 s cada 60 grados. */
#define SERVO_MS_POR_GRADO 1.7

/**
 * El servo no se deja ir a su velocidad maxima: servo_mover() lo lleva en
 * rampa, a 1/SERVO_DIVISOR_VELOCIDAD de ella (3 -> 5.1 ms por grado). A toda
 * velocidad el vaiven sacudia el sensor y el chasis.
 */
#define SERVO_DIVISOR_VELOCIDAD 3

/** Cada cuanto avanza la rampa: un periodo del pulso de 50 Hz. Un escalon
    mas corto no sirve, el servo solo lee un pulso cada 20 ms. */
#define SERVO_PERIODO_MS 20

/** Espera extra al llegar, para que el sensor deje de vibrar antes de medir. */
#define SERVO_ASENTAMIENTO_MS 30

/** Configura el pin del servo. No lo mueve: la posicion inicial es desconocida. */
void servo_init(int pi);

/**
 * @brief Lleva el servo a un angulo, a 1/SERVO_DIVISOR_VELOCIDAD de su
 *        velocidad maxima.
 *
 * Bloquea mientras dura la rampa: manda un pulso intermedio cada
 * SERVO_PERIODO_MS hasta el angulo pedido. Devuelve cuanto falta despues para
 * que el servo llegue al ultimo escalon y se asiente, para que quien mide sepa
 * cuanto esperar antes de disparar el sensor. Desde una posicion desconocida
 * (el primer movimiento, o tras servo_liberar) no hay desde donde hacer la
 * rampa: el servo va de un salto y la espera es la de media vuelta.
 *
 * @param grados De 0 (derecha) a 180 (izquierda). Se satura.
 * @return Milisegundos estimados hasta que el servo queda quieto en el
 *         angulo pedido, o -1 si pigpiod rechazo el pulso.
 */
int servo_mover(int grados);

/** Ultimo angulo ordenado, o -1 si todavia no se movio o esta liberado. */
int servo_angulo(void);

/** Deja de mandar pulsos: el servo queda suelto, sin fuerza y sin consumo. */
void servo_liberar(void);

#endif /* LIB_SERVO_H */
