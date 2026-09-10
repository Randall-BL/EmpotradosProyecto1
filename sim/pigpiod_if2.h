/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * MIT License. Ver LICENSE y NOTICE.md.
 */

#ifndef PIGPIOD_IF2_SIM_H
#define PIGPIOD_IF2_SIM_H

/*
 * Reemplazo de <pigpiod_if2.h> para correr la biblioteca en la laptop.
 *
 * Declara el mismo subconjunto de la API de pigpio que usa librobot, con las
 * mismas firmas. El codigo de lib/ no se modifica ni se recompila distinto:
 * basta anteponer este directorio a la ruta de includes (-Isim) para que el
 * enlace resuelva contra el simulador en vez de contra el demonio pigpiod.
 *
 * Detras, pigpio_sim.c traduce cada escritura de pin a un movimiento del
 * robot virtual y cada lectura de sensor a una medicion sobre el mundo
 * simulado (mundo.h).
 */

#include <stdint.h>

#define PI_INPUT  0
#define PI_OUTPUT 1

int      pigpio_start(const char *addrStr, const char *portStr);
void     pigpio_stop(int pi);
int      set_mode(int pi, unsigned gpio, unsigned mode);
int      gpio_write(int pi, unsigned gpio, unsigned level);
int      gpio_read(int pi, unsigned gpio);
int      set_PWM_frequency(int pi, unsigned user_gpio, unsigned frequency);
int      set_PWM_range(int pi, unsigned user_gpio, unsigned range);
int      set_PWM_dutycycle(int pi, unsigned user_gpio, unsigned dutycycle);
uint32_t get_current_tick(int pi);

#endif /* PIGPIOD_IF2_SIM_H */
