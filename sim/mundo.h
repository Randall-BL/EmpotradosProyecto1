/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#ifndef MUNDO_H
#define MUNDO_H

/*
 * El mundo virtual donde vive el robot simulado.
 *
 * Una habitacion rectangular con obstaculos rectangulares adentro. Guarda la
 * posicion VERDADERA del robot —la que la odometria intenta estimar— y
 * responde a que distancia hay pared o mueble en una direccion dada.
 *
 * El tiempo avanza solo: cada consulta integra los segundos transcurridos
 * desde la anterior, asi que el robot se mueve al ritmo del reloj real.
 */

/** Distancia del centro del robot virtual al eje del servo del radar, en cm.
    Es la geometria del robot simulado, no la constante de la biblioteca. */
#define SIM_SENSOR_ADELANTE_CM 10.0

/** Prepara la habitacion por defecto y pone el robot en el centro, quieto. */
void mundo_init(void);

/** Velocidad ordenada a cada motor, de -255 a 255. La llama el simulador. */
void mundo_set_motores(int vel_izq, int vel_der);

/**
 * @brief Distancia al obstaculo mas cercano en una direccion, desde el centro.
 * @param rumbo_offset Grados respecto al frente del robot: 0 al frente,
 *                     -90 a la izquierda, 90 a la derecha.
 * @return Distancia en cm, o -1.0 si no hay nada dentro del alcance del
 *         HC-SR04 (400 cm).
 */
double mundo_distancia(double rumbo_offset);

/**
 * @brief Igual que mundo_distancia(), pero midiendo desde un punto que esta
 *        @p adelante_cm por delante del centro del robot: donde va el sensor.
 */
double mundo_distancia_desde(double adelante_cm, double rumbo_offset);

/**
 * @brief Velocidad real del cuerpo del robot.
 * @param v_cm_s    Hacia adelante; 0 si esta trabado contra algo aunque las
 *                  llantas giren. Puede ser NULL.
 * @param giro_dps  Grados/s en sentido horario (el del rumbo). Puede ser NULL.
 */
void mundo_cinematica(double *v_cm_s, double *giro_dps);

/**
 * @brief Lo que mediria un acelerometro/giroscopo montado en el centro.
 *
 * La aceleracion es la media desde la consulta anterior —como la entrega el
 * filtro pasabajos del MPU-6050—, asi que integrarla reproduce exactamente
 * el cambio de velocidad. La usa el MPU-6050 simulado de pigpio_sim.c.
 *
 * @param avance_cm_s2  Hacia adelante.
 * @param lateral_cm_s2 Hacia la izquierda (la centripeta de las curvas).
 * @param giro_dps      Grados/s en sentido horario.
 */
void mundo_imu(double *avance_cm_s2, double *lateral_cm_s2, double *giro_dps);

/** Posicion y rumbo verdaderos. Cualquier puntero puede ser NULL. */
void mundo_pose(double *x_cm, double *y_cm, double *rumbo_grados);

/** 1 si el robot esta pegado a una pared o mueble. */
int mundo_choco(void);

/** Dibuja la habitacion en la terminal, con el robot y su rastro. */
void mundo_dibujar(void);

#endif /* MUNDO_H */
