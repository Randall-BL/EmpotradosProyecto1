/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#ifndef LIB_ROBOT_H
#define LIB_ROBOT_H

/*
 * Fachada del hardware del robot.
 *
 * El enunciado exige que el servidor web no toque GPIO: todo acceso al
 * hardware pasa por esta biblioteca. Este modulo es la unica puerta de
 * entrada — abre la sesion con pigpiod, inicializa motores, sensores, LEDs y
 * odometria, y expone lecturas ya interpretadas. Ningun cliente necesita
 * incluir pigpiod_if2.h ni conocer un numero de pin.
 *
 * El mapa de pines vive en esta biblioteca (lib_motors.c, lib_leds.c y este
 * archivo) y esta documentado en docs/hardware-pinout.md.
 */

/**
 * @brief Abre la sesion con pigpiod e inicializa todo el hardware.
 *
 * Deja los motores detenidos, los sensores listos para disparar, los LEDs
 * apagados y la odometria en el origen.
 *
 * @return 0 si todo quedo listo, -1 si no se pudo conectar al demonio pigpiod.
 */
int robot_init(void);

/**
 * @brief Detiene los motores, apaga los LEDs y cierra la sesion con pigpiod.
 *
 * Es seguro llamarla mas de una vez o sin haber llamado a robot_init().
 */
void robot_shutdown(void);

/** @return 1 si hay sesion abierta con pigpiod, 0 si no. */
int robot_activo(void);

/* ── Sensores de proximidad ───────────────────────────────────────────────────
 * Distancia en centimetros, o -1.0 si la lectura se paso del tiempo de espera
 * (eco perdido, u obstaculo mas alla del alcance del HC-SR04).
 */
double robot_distancia_frontal(void);
double robot_distancia_izquierda(void);
double robot_distancia_derecha(void);

#endif /* LIB_ROBOT_H */
