# `sim/` — Simulador de hardware para probar en la laptop

Corre la biblioteca de control **sin la Raspberry Pi**. Sustituye a `pigpiod`
por una implementación falsa que mueve un robot virtual dentro de una sala de
4×4 m con muebles, y responde las lecturas de los sensores midiendo sobre ese
mundo. Sirve para dos cosas:

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

## Qué comprueba `prueba_librobot` (issue #19)

| Módulo | Verificación |
|---|---|
| Arranque | `robot_init()` abre la sesión y `robot_activo()` lo confirma |
| Sensores | las tres lecturas coinciden con la distancia real del mundo |
| LEDs | encender y apagar cada indicador cambia su estado |
| Motores básicos | avanzar, retroceder y girar mueven al robot como se espera |
| Control diferencial | `motores_set` guarda la velocidad por motor, satura en ±255, y con velocidades distintas describe una curva; `motores_curva(v,100)` detiene la llanta interior |
| Odometría | tras un recorrido en L, la posición estimada queda a menos del 20 % de error respecto de la real |
| Cierre | `robot_shutdown()` cierra la sesión y deja los motores detenidos |

## Piezas

| Archivo | Qué es |
|---|---|
| `pigpiod_if2.h` | Cabecera de reemplazo: el subconjunto de la API de pigpio que usa `librobot` |
| `pigpio_sim.c` | Implementación falsa: pines → movimiento, sensores → medición sobre el mundo |
| `mundo.h` / `mundo.c` | El robot virtual y la sala; avanza solo, al ritmo del reloj real |
| `prueba_librobot.c` | El programa de prueba |
| `construir.sh` | Compila todo con el gcc del host |

## Límites

- El simulador **no** reproduce audio ni levanta el servidor web: prueba la
  biblioteca de control (motores, sensores, LEDs, odometría), que es la que no
  se podía ejercitar sin la Pi. Para el servidor completo en la laptop hacen
  falta las cabeceras de desarrollo de `libmicrohttpd`, `mpg123` y `alsa`.
- Los parámetros físicos del mundo (`REAL_VEL_MAX_CM_S`, `REAL_ENTRE_EJES_CM`)
  difieren a propósito de las constantes de `lib_odom`, para que la prueba de
  odometría muestre el error real que acumula la navegación a la estima.
