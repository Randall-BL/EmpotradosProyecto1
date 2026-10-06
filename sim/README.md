# `sim/` — Simulador de hardware para probar en la laptop

Corre la biblioteca de control **sin la Raspberry Pi**. Sustituye a `pigpiod`
por una implementación falsa que mueve un robot virtual dentro de una sala de
4×4 m con muebles, y responde el hardware midiendo sobre ese mundo: el servo del
radar, el HC-SR04 que mide hacia donde apunta el servo y el MPU-6050 por I2C.
Sirve para dos cosas:

- **probar `librobot` de verdad** (no solo compilarla) mientras no está el kit;
- **plan B para la demo** si el hardware falla el día de la presentación.

## Cómo se conecta sin tocar `lib/`

Los fuentes de `lib/` quedan intactos. El truco está en el include:

```
lib_motors.c  →  #include <pigpiod_if2.h>
```

`construir.sh` compila con `-I.`, así que ese `#include` resuelve contra el
`pigpiod_if2.h` de esta carpeta —el del simulador— en vez del real. El enlace
usa `pigpio_sim.c`, que traduce cada escritura de pin a un movimiento del robot
virtual (`mundo.c`). El mismo código que corre en la Pi corre aquí.

## Uso

```sh
cd sim
./construir.sh          # compila con gcc del host, no necesita Yocto
./prueba_librobot       # ejercita cada módulo y compara contra el mundo real
```

Devuelve 0 si todas las comprobaciones pasan, e imprime al final el recorrido
del robot dibujado en la terminal.

Para el binario ARM bajo `qemu-aarch64` (`construir_arm.sh`) y el servidor
completo (`construir_servidor.sh` + `prueba_api.sh`), ver
[`../docs/qemu.md`](../docs/qemu.md) y
[`../docs/evidencias/`](../docs/evidencias/).

## Qué comprueba `prueba_librobot` (issue #19) — 47 comprobaciones

| Módulo | Verificación |
|---|---|
| Arranque | `robot_init()` abre la sesión, el MPU-6050 contesta en el bus I2C |
| MPU-6050 | la calibración quita el sesgo de fábrica del sensor simulado (0.02 g y 1.5 °/s); el eje vertical mide 1 g; la odometría lo usa |
| Radar | completa un barrido; cada uno de los 7 ángulos mide lo que hay en el mundo en esa dirección; las distancias de la fachada son los ángulos 90/180/0; `radar_rayo()` proyecta el eco frontal sobre la pared |
| Servo | pulsos de 1000 µs a 45°, saturación en 500 y 2500 µs, estimación del tiempo de viaje |
| LEDs | encender y apagar cada indicador cambia su estado |
| Motores básicos | avanzar, retroceder y girar mueven al robot como se espera |
| Control diferencial | `motores_set` satura en ±255; con velocidad fija (la que usa el robot) cualquier velocidad va al máximo y `motores_curva` gira sobre el eje, una llanta adelante y la otra atrás; con `-DMOTOR_VELOCIDAD_VARIABLE=1`, velocidades distintas describen una curva y `motores_curva(v,100)` detiene la llanta interior |
| Velocidad (MPU) | al arrancar sigue la inercia real mejor que el modelo de motores; en crucero queda a ±15 % de la real; detenido vuelve a 0 (ZUPT) |
| Tiempo de choque | detecta la pared al frente, coincide con el real a ±15 %, la cuenta regresiva baja entre lecturas y se anula al detenerse |
| Odometría | tras un recorrido en L, error de posición bajo el 20 % (medido: 1–5 %) y de rumbo bajo 5° (medido: 1–2°) |
| Audio | API: volumen, estado, listado (si hay tarjeta de sonido) |
| Cierre | `robot_shutdown()` cierra la sesión, deja los motores detenidos, el servo sin pulsos y el I2C libre |

## Piezas

| Archivo | Qué es |
|---|---|
| `pigpiod_if2.h` | Cabecera de reemplazo: el subconjunto de la API de pigpio que usa `librobot` (GPIO, PWM, servo, I2C) |
| `pigpio_sim.c` | Implementación falsa: pines → movimiento; servo que viaja a 600°/s; HC-SR04 que mide hacia donde apunta el servo; MPU-6050 en `/dev/i2c-1` que arranca dormido y trae sesgo |
| `mundo.h` / `mundo.c` | El robot virtual y la sala; avanza solo, al ritmo del reloj real, con inercia en las llantas. Avisa por `stderr` cada choque (`[sim] CHOQUE`) |
| `prueba_librobot.c` | El programa de prueba |
| `construir.sh` | Compila todo con el gcc del host |
| `construir_arm.sh` | Lo mismo con el Toolchain-SDK, y lo corre bajo `qemu-aarch64` |
| `construir_servidor.sh` / `prueba_api.sh` | El servidor completo sobre el simulador, y su prueba por HTTP |

## Límites

- El simulador **no** reproduce audio: prueba la biblioteca de control y el
  servidor. Para compilar en la laptop hacen falta las cabeceras de desarrollo
  de `libmicrohttpd`, `mpg123` y `alsa`.
- Los parámetros físicos del mundo (`REAL_VEL_MAX_CM_S`, `REAL_ENTRE_EJES_CM`)
  difieren a propósito de las constantes de `lib_odom`, para que las pruebas
  muestren el error que deja una calibración imperfecta.
- El HC-SR04 simulado mide a lo largo de un rayo: no tiene cono ni rebotes
  especulares. En la sala real habrá lecturas que el simulador no produce.
