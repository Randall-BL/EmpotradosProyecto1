/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 */

/* Guarda de inclusion: el contenido se procesa una sola vez por archivo. */
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

/* uint32_t. */
#include <stdint.h>

/* Modos de un pin. */
#define PI_INPUT  0
#define PI_OUTPUT 1

/* Resistencias de pull internas. */
#define PI_PUD_OFF  0
#define PI_PUD_DOWN 1
#define PI_PUD_UP   2

/* Codigos de error: los mismos valores de pigpio.h. */
#define PI_BAD_USER_GPIO     -2
#define PI_BAD_PULSEWIDTH    -7
#define PI_BAD_HANDLE       -25
#define PI_I2C_OPEN_FAILED  -71
#define PI_I2C_READ_FAILED  -83

/* Abre la sesion con el demonio; aqui, reinicia el simulador. */
int      pigpio_start(const char *addrStr, const char *portStr);
/* Cierra la sesion. */
void     pigpio_stop(int pi);
/* Fija un pin como entrada o salida. */
int      set_mode(int pi, unsigned gpio, unsigned mode);
/* Escribe un nivel en un pin. */
int      gpio_write(int pi, unsigned gpio, unsigned level);
/* Lee el nivel de un pin. */
int      gpio_read(int pi, unsigned gpio);
/* Activa la resistencia de pull de un pin. */
int      set_pull_up_down(int pi, unsigned gpio, unsigned pud);
/* Frecuencia de la PWM de un pin. */
int      set_PWM_frequency(int pi, unsigned user_gpio, unsigned frequency);
/* Rango del ciclo de trabajo. */
int      set_PWM_range(int pi, unsigned user_gpio, unsigned range);
/* Ciclo de trabajo de la PWM. */
int      set_PWM_dutycycle(int pi, unsigned user_gpio, unsigned dutycycle);
/* Reloj de microsegundos del demonio. */
uint32_t get_current_tick(int pi);

/* Servo del radar */
/* Ancho del pulso del servo, en microsegundos; 0 lo apaga. */
int      set_servo_pulsewidth(int pi, unsigned user_gpio, unsigned pulsewidth);
/* Ultimo ancho de pulso fijado. */
int      get_servo_pulsewidth(int pi, unsigned user_gpio);

/* I2C, para el MPU-6050 */
/* Abre un dispositivo I2C; devuelve un handle. */
int      i2c_open(int pi, unsigned i2c_bus, unsigned i2c_addr, unsigned i2c_flags);
/* Cierra el dispositivo. */
int      i2c_close(int pi, unsigned handle);
/* Escribe un byte en un registro. */
int      i2c_write_byte_data(int pi, unsigned handle, unsigned i2c_reg, unsigned bVal);
/* Lee un byte de un registro. */
int      i2c_read_byte_data(int pi, unsigned handle, unsigned i2c_reg);
/* Lee varios registros seguidos. */
int      i2c_read_i2c_block_data(int pi, unsigned handle, unsigned i2c_reg,
                                 char *buf, unsigned count);

#endif /* PIGPIOD_IF2_SIM_H */
