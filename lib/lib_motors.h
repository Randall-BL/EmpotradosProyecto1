/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

/** Duty cycle maximo del PWM: el rango que se le configura al pin ENA/ENB. */
#define MOTOR_PWM_MAX 255

/**
 * @brief Inicializa los pines del L298N como salidas.
 */
void motores_init(int pi);

/**
 * @brief Detiene ambos motores apagando el PWM y los pines de dirección.
 */
void motores_detener(void);

/**
 * @brief Control diferencial: fija la velocidad de cada motor por separado.
 *
 * Es la primitiva sobre la que se construyen todas las demás. El signo define
 * el sentido de giro y la magnitud el duty cycle del PWM:
 *
 *   motores_set( 200,  200)  → avanza recto
 *   motores_set(-200, -200)  → retrocede
 *   motores_set(-200,  200)  → gira sobre su propio eje hacia la izquierda
 *   motores_set( 200,  120)  → curva a la derecha con radio amplio
 *
 * @param vel_izq Velocidad del motor izquierdo, de -255 a 255. Se satura.
 * @param vel_der Velocidad del motor derecho, de -255 a 255. Se satura.
 */
void motores_set(int vel_izq, int vel_der);

/**
 * @brief Devuelve la última velocidad ordenada a cada motor.
 *
 * Es la entrada de la odometría (ver lib_odom.h): sin encoders en las llantas,
 * la posición se estima a partir de lo que se le ordenó a los motores.
 * Cualquiera de los dos punteros puede ser NULL.
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
 * @param velocidad Velocidad del motor exterior, de -255 a 255.
 * @param giro      De -100 (curva cerrada a la izquierda) a 100 (a la derecha);
 *                  0 es recto. Reduce proporcionalmente el motor interior.
 */
void motores_curva(int velocidad, int giro);

#endif // MOTOR_CONTROL_H
