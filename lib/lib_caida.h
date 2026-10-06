/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#ifndef LIB_CAIDA_H
#define LIB_CAIDA_H

/*
 * Deteccion de desnivel (requerimiento opcional del enunciado).
 *
 * Dos modulos infrarrojos de reflexion (TCRT5000 o FC-51) en las esquinas
 * delanteras del chasis, mirando al piso a 1-2 cm de altura. Con piso debajo
 * el haz se refleja y la salida digital del modulo queda en bajo; sobre el
 * borde de una grada no vuelve nada y la salida sube.
 *
 * Los modulos se alimentan de 3.3 V, asi que su salida entra directo al GPIO.
 * Las entradas llevan el pull-down interno: un modulo desconectado se lee
 * como "hay piso" y el robot navega igual que sin los sensores, en vez de
 * quedarse frenado por un falso desnivel.
 *
 * Pines en docs/hardware-pinout.md.
 */

#define CAIDA_GPIO_IZQ 4    /* esquina delantera izquierda, pin fisico 7  */
#define CAIDA_GPIO_DER 8    /* esquina delantera derecha,   pin fisico 24 */

/* Nivel de la salida del modulo cuando no ve piso. */
#define CAIDA_NIVEL_SIN_PISO 1

/* Bits que devuelve caida_leer(). */
#define CAIDA_IZQ 0x1
#define CAIDA_DER 0x2

/**
 * @brief Configura los dos GPIO como entradas con pull-down.
 * @param pi Sesion abierta con pigpiod.
 * @return 0 si quedaron listos, -1 si pigpiod rechazo la configuracion.
 */
int caida_init(int pi);

/**
 * @brief Lee los dos sensores.
 *
 * Cada sensor se lee dos veces, separadas por 200 us, y solo cuenta como
 * desnivel si las dos lecturas coinciden: filtra el ruido de los motores sin
 * agregar retardo apreciable.
 *
 * @return Combinacion de CAIDA_IZQ y CAIDA_DER; 0 si hay piso bajo los dos, o
 *         si caida_init() no se llamo.
 */
int caida_leer(void);

#endif /* LIB_CAIDA_H */
