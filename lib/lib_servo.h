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
 * no se deforma aunque el proceso tarde en recibir CPU. La senal cruza al
 * dominio de potencia por un PC817 en seguidor de emisor, que NO la invierte:
 * con el GPIO en bajo el servo no recibe pulsos y se queda quieto. Ver
 * docs/hardware-aislamiento.md.
 */

/** GPIO (BCM) de la senal del servo. Pin fisico 22. */
#define SERVO_GPIO 25

/**
 * Pulso que lleva el servo a 0 y a 180 grados, en microsegundos. Varian entre
 * unidades y el optoacoplador corre el flanco unas decenas de microsegundos:
 * se calibran en campo (docs/odometria.md).
 */
#define SERVO_PULSO_0_US    500
#define SERVO_PULSO_180_US 2500

/** Velocidad sin carga de un SG90/MG90S a 5 V: unos 0.1 s cada 60 grados. */
#define SERVO_MS_POR_GRADO 1.7

/** Espera extra al llegar, para que el sensor deje de vibrar antes de medir. */
#define SERVO_ASENTAMIENTO_MS 30

/** Configura el pin del servo. No lo mueve: la posicion inicial es desconocida. */
void servo_init(int pi);

/**
 * @brief Ordena al servo ir a un angulo.
 *
 * No espera a que llegue. Devuelve cuanto tarda en llegar y asentarse, para
 * que quien mide sepa cuanto esperar antes de disparar el sensor.
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
