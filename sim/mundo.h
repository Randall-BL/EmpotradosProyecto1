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

/** Prepara la habitacion por defecto y pone el robot en el centro. */
void mundo_init(void);

/** Velocidad ordenada a cada motor, de -255 a 255. La llama el simulador. */
void mundo_set_motores(int vel_izq, int vel_der);

/**
 * @brief Distancia al obstaculo mas cercano en una direccion.
 * @param rumbo_offset Grados respecto al frente del robot: 0 al frente,
 *                     -90 a la izquierda, 90 a la derecha.
 * @return Distancia en cm, o -1.0 si no hay nada dentro del alcance del
 *         HC-SR04 (400 cm).
 */
double mundo_distancia(double rumbo_offset);

/** Posicion y rumbo verdaderos. Cualquier puntero puede ser NULL. */
void mundo_pose(double *x_cm, double *y_cm, double *rumbo_grados);

/** 1 si el robot esta pegado a una pared o mueble. */
int mundo_choco(void);

/** Dibuja la habitacion en la terminal, con el robot y su rastro. */
void mundo_dibujar(void);

#endif /* MUNDO_H */
