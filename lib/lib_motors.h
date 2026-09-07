/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

/**
 * @brief Inicializa los pines del L298N como salidas.
 */
void motores_init(int pi);

/**
 * @brief Detiene ambos motores apagando el PWM y los pines de dirección.
 */
void motores_detener();

void motores_avanzar(int velocidad);
void motores_retroceder(int velocidad);
void motores_girar_izquierda(int velocidad);
void motores_girar_derecha(int velocidad);

#endif // MOTOR_CONTROL_H
