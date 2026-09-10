/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#ifndef LIB_ODOM_H
#define LIB_ODOM_H

/*
 * Odometria del robot — estimacion de posicion y orientacion.
 *
 * El chasis no lleva encoders en las llantas: los 40 pines del conector ya
 * estan asignados (ver docs/hardware-pinout.md), asi que no hay entradas
 * libres para leerlos. La posicion se estima entonces por navegacion a la
 * estima (dead reckoning): se integra en el tiempo la velocidad que se le
 * ORDENO a cada motor, con el modelo cinematico de traccion diferencial.
 *
 * Es una estimacion, no una medicion: acumula error con cada patinada de las
 * llantas y con cada choque. Sirve para dibujar el mapa de recorrido, que es
 * para lo que la pide el enunciado, no para posicionamiento absoluto.
 *
 * Las dos constantes de abajo son las que hay que CALIBRAR EN CAMPO antes de
 * la demo; el procedimiento esta en docs/odometria.md.
 */

/** Velocidad lineal de una llanta con el PWM al maximo, en cm/s. */
#define ODOM_VEL_MAX_CM_S 30.0

/** Distancia entre el centro de las dos llantas, en cm. */
#define ODOM_ENTRE_EJES_CM 15.0

/**
 * Duty cycle por debajo del cual el motor no vence su propia friccion y no
 * gira. Se modela como velocidad cero para no acumular avance imaginario.
 */
#define ODOM_PWM_ARRANQUE 60

/** Arranca la odometria en el origen. Idempotente. */
void odom_init(void);

/**
 * @brief Integra el movimiento ocurrido desde la llamada anterior.
 *
 * Se llama periodicamente desde el lazo de navegacion. El paso de integracion
 * es el tiempo real transcurrido entre llamadas, medido con CLOCK_MONOTONIC,
 * asi que no depende de que el lazo corra a una frecuencia fija.
 */
void odom_update(void);

/** Vuelve a poner el robot en el origen mirando al norte. */
void odom_reset(void);

/**
 * @brief Posicion estimada.
 *
 * @param x_cm        Este positivo, oeste negativo. Puede ser NULL.
 * @param y_cm        Norte positivo, sur negativo. Puede ser NULL.
 * @param rumbo_grados Rumbo tipo brujula: 0 norte, 90 este, 180 sur, 270 oeste.
 *                     Puede ser NULL.
 */
void odom_get(double *x_cm, double *y_cm, double *rumbo_grados);

/** Camino total recorrido en cm, sumando avances y retrocesos. */
double odom_distancia_recorrida(void);

/** Velocidad instantanea de cada llanta en cm/s, con signo. NULL permitido. */
void odom_get_velocidades(double *izq_cm_s, double *der_cm_s);

#endif /* LIB_ODOM_H */
