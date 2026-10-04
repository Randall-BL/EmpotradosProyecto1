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
 * entrada — abre la sesion con pigpiod, inicializa motores, LEDs, el MPU-6050,
 * la odometria y el radar (HC-SR04 sobre un servo), y expone lecturas ya
 * interpretadas. Ningun cliente necesita incluir pigpiod_if2.h ni conocer un
 * numero de pin.
 *
 * El mapa de pines vive en esta biblioteca (lib_motors.c, lib_leds.h,
 * lib_servo.h, lib_radar.h y lib_imu.h) y esta documentado en
 * docs/hardware-pinout.md.
 */

/**
 * @brief Abre la sesion con pigpiod e inicializa todo el hardware.
 *
 * Deja los motores detenidos, los LEDs apagados y la odometria en el origen;
 * calibra el MPU-6050 (el robot tiene que estar quieto, tarda medio segundo) y
 * arranca los hilos de la odometria y del barrido del radar.
 *
 * Si el MPU-6050 no contesta, sigue sin el: la velocidad y el rumbo salen del
 * modelo de los motores y se avisa por stderr.
 *
 * @return 0 si todo quedo listo, -1 si no se pudo conectar al demonio pigpiod.
 */
int robot_init(void);

/**
 * @brief Detiene el radar y la odometria, frena los motores, apaga los LEDs y
 *        cierra la sesion con pigpiod.
 *
 * Es seguro llamarla mas de una vez o sin haber llamado a robot_init().
 */
void robot_shutdown(void);

/** @return 1 si hay sesion abierta con pigpiod, 0 si no. */
int robot_activo(void);

/* ── Sensores de proximidad ───────────────────────────────────────────────────
 * Ultima lectura del radar en tres direcciones: al frente (servo a 90), a la
 * izquierda (180) y a la derecha (0). Cada una se refresca una vez por
 * barrido; el barrido completo y el tiempo de choque estan en lib_radar.h.
 *
 * Distancia en centimetros, o -1.0 si todavia no se midio o si el eco no
 * volvio (obstaculo mas alla del alcance del HC-SR04, o rebote perdido).
 */
double robot_distancia_frontal(void);
double robot_distancia_izquierda(void);
double robot_distancia_derecha(void);

#endif /* LIB_ROBOT_H */
