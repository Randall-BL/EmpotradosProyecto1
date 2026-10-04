/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

/*
 * Motores DC sobre un L298N con los jumpers ENA/ENB puestos: el puente queda
 * siempre habilitado y el movimiento se controla con las entradas de sentido
 * IN1-IN4. No hay pines de habilitacion en la Pi.
 */

/** Tope de velocidad: el rango de PWM que se le configura a IN1-IN4. */
#define MOTOR_PWM_MAX 255

/**
 * 1: la velocidad es variable, con PWM sobre IN1-IN4.
 * 0: cada entrada es un nivel fijo, y cualquier velocidad distinta de cero
 *    lleva el motor al maximo. Es lo que se usa por ahora.
 *
 * Por que 0: a 1 kHz, el PC817 con pull-up de 4.7 kOhm tarda decenas de
 * microsegundos en apagarse y le recorta a cada ciclo parte del tiempo en
 * que el motor recibe tension. Sumado a los ~2 V que cae el L298N, a
 * velocidades medias el motor no llega a la tension de arranque y no se
 * mueve. Para recuperar la velocidad variable: bajar PWM_FREQ (lib_motors.c)
 * a 100 Hz, o el pull-up a 1 kOhm, y poner esto en 1. Ver
 * docs/hardware-aislamiento.md.
 */
#ifndef MOTOR_VELOCIDAD_VARIABLE
#define MOTOR_VELOCIDAD_VARIABLE 0
#endif

/**
 * @brief Configura IN1-IN4 como salidas (PWM si MOTOR_VELOCIDAD_VARIABLE) y
 *        deja los motores frenados.
 */
void motores_init(int pi);

/**
 * @brief Frena ambos motores: las dos entradas de cada uno en bajo.
 *
 * Con el puente habilitado, el L298N cortocircuita el motor: frena en seco
 * en vez de dejarlo girar libre.
 */
void motores_detener(void);

/**
 * @brief Control diferencial: fija la velocidad de cada motor por separado.
 *
 * Es la primitiva sobre la que se construyen todas las demás. El signo elige
 * cuál de las dos entradas del motor se activa (la otra queda en bajo) y la
 * magnitud es su duty cycle. Con MOTOR_VELOCIDAD_VARIABLE en 0 la magnitud no
 * importa: cualquier valor distinto de cero es velocidad máxima.
 *
 *   motores_set( 200,  200)  → avanza recto
 *   motores_set(-200, -200)  → retrocede
 *   motores_set(-200,  200)  → gira sobre su propio eje hacia la izquierda
 *   motores_set( 200,  120)  → curva a la derecha con radio amplio
 *                              (con velocidad fija: recto, los dos al máximo)
 *
 * @param vel_izq Velocidad del motor izquierdo, de -255 a 255. Se satura.
 * @param vel_der Velocidad del motor derecho, de -255 a 255. Se satura.
 */
void motores_set(int vel_izq, int vel_der);

/**
 * @brief Devuelve la velocidad que recibe cada motor, con signo.
 *
 * Es la ordenada, ya saturada; con MOTOR_VELOCIDAD_VARIABLE en 0, cualquier
 * velocidad distinta de cero se informa como +-MOTOR_PWM_MAX, que es la que
 * el motor recibe de verdad. Es la entrada de la odometría (ver lib_odom.h):
 * sin encoders en las llantas, la posición se estima a partir de lo que
 * reciben los motores. Cualquiera de los dos punteros puede ser NULL.
 */
void motores_get(int *vel_izq, int *vel_der);

/* ── Movimientos básicos, todos sobre motores_set() ────────────────────────── */

void motores_avanzar(int velocidad);
void motores_retroceder(int velocidad);
void motores_girar_izquierda(int velocidad);
void motores_girar_derecha(int velocidad);

/**
 * @brief Avanza describiendo una curva, sin detenerse a girar.
 *
 * Con MOTOR_VELOCIDAD_VARIABLE en 0 no hay curvas de radio variable: con
 * cualquier giro distinto de cero gira sobre su eje hacia ese lado, con una
 * llanta hacia adelante y la otra hacia atrás.
 *
 * @param velocidad Velocidad del motor exterior, de -255 a 255.
 * @param giro      De -100 (curva cerrada a la izquierda) a 100 (a la derecha);
 *                  0 es recto. Reduce proporcionalmente el motor interior.
 */
void motores_curva(int velocidad, int giro);

#endif // MOTOR_CONTROL_H
