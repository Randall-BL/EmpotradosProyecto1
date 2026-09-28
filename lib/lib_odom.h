/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#ifndef LIB_ODOM_H
#define LIB_ODOM_H

/*
 * Odometria del robot — estimacion de posicion, rumbo y velocidad.
 *
 * El chasis no lleva encoders en las llantas. La posicion se estima por
 * navegacion a la estima (dead reckoning) con dos fuentes:
 *
 *   - El MPU-6050 (lib_imu), cuando esta presente. Su aceleracion de avance,
 *     integrada, da la velocidad; su giroscopo, integrado, da el rumbo. Ve lo
 *     que el modelo de los motores no puede ver: el arranque y la frenada
 *     reales, la llanta que patina, el choque que detiene al robot.
 *   - El modelo de traccion diferencial: la velocidad que se le ORDENO a cada
 *     motor. Es la unica fuente si no hay MPU, y aun con el tira suavemente de
 *     la velocidad integrada para que no derive.
 *
 * Por que hace falta esa correccion: un acelerometro no mide velocidad, mide
 * cambios de velocidad. Integrar su sesgo residual (unos pocos cm/s2 despues
 * de calibrar) acumula un error que crece sin limite. El filtro complementario
 * de abajo deja pasar la dinamica rapida del MPU y toma del modelo de los
 * motores solo la tendencia lenta, con la constante de tiempo
 * ODOM_TAU_FUSION_S. Y con los motores detenidos la velocidad se fuerza a
 * cero (ZUPT, zero-velocity update), que es la correccion mas fuerte que hay.
 *
 * Es una estimacion, no una medicion: sirve para el mapa de recorrido y para
 * el tiempo antes de chocar, no para posicionamiento absoluto.
 *
 * Las constantes de abajo se CALIBRAN EN CAMPO: docs/odometria.md.
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

/**
 * Constante de tiempo, en s, con la que el modelo de los motores corrige la
 * deriva de la velocidad integrada del MPU. Mas chica: se parece mas al
 * modelo. Mas grande: sigue mas al MPU, pero un sesgo residual b deja un
 * error de b * tau en la velocidad.
 */
#define ODOM_TAU_FUSION_S 1.0

/** Con los dos motores detenidos este tiempo, la velocidad se fuerza a cero. */
#define ODOM_ZUPT_S 0.3

/** Periodo del hilo propio de la odometria (50 Hz). */
#define ODOM_PERIODO_MS 20

/** Arranca la odometria en el origen. Idempotente. */
void odom_init(void);

/**
 * @brief Lanza el hilo que mantiene la odometria al dia a 50 Hz.
 *
 * robot_init() la llama: el cliente ya no necesita llamar a odom_update().
 * Idempotente. @return 0 si el hilo quedo corriendo, -1 si no se pudo crear.
 */
int odom_arrancar(void);

/** Detiene el hilo de odom_arrancar(). Es seguro llamarla sin haberlo lanzado. */
void odom_parar(void);

/**
 * @brief Integra el movimiento ocurrido desde la llamada anterior.
 *
 * La llama el hilo de odom_arrancar(); llamarla ademas desde otro hilo no
 * hace dano, solo integra en pasos mas finos. El paso de integracion es el
 * tiempo real transcurrido, medido con CLOCK_MONOTONIC.
 */
void odom_update(void);

/** Vuelve a poner el robot en el origen, quieto y mirando al norte. */
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

/**
 * @brief Velocidad del robot hacia adelante, en cm/s; negativa al retroceder.
 *
 * Con MPU es la velocidad integrada de su acelerometro, corregida como se
 * explica arriba; sin MPU, la del modelo de los motores.
 */
double odom_velocidad_cm_s(void);

/**
 * @brief Avance neto acumulado, en cm: suma con signo de lo recorrido.
 *
 * A diferencia de odom_distancia_recorrida(), retroceder resta. Restando dos
 * valores se sabe cuanto se acerco el robot a lo que tenia al frente.
 */
double odom_avance_cm(void);

/** Camino total recorrido en cm, sumando avances y retrocesos. */
double odom_distancia_recorrida(void);

/** Velocidad del modelo de cada llanta en cm/s, con signo. NULL permitido. */
void odom_get_velocidades(double *izq_cm_s, double *der_cm_s);

/** @return 1 si la ultima integracion uso el MPU-6050, 0 si el modelo. */
int odom_usa_imu(void);

#endif /* LIB_ODOM_H */
