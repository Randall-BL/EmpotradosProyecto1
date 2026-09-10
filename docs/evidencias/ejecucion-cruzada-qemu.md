# Evidencia — El binario ARM cross-compilado ejecuta correctamente

Prueba de que la compilación cruzada no solo produce un ELF de ARM, sino que
ese binario **ejecuta y funciona**, sin la Raspberry: se compila la prueba de
la biblioteca con el Toolchain-SDK (para `cortexa72`/aarch64) y se corre bajo
`qemu-aarch64` en modo usuario, que viene incluido en el propio SDK.

Reproducible: `sim/construir_arm.sh` (necesita el SDK instalado, ver `docs/sdk.md`).

## Compilación y arquitectura

```
$ . ~/robot-sdk/environment-setup-cortexa72-poky-linux
$ $CC ... prueba_librobot.c -o prueba_librobot_arm -lm -lpthread
$ file prueba_librobot_arm
prueba_librobot_arm: ELF 64-bit LSB pie executable, ARM aarch64
```

## Ejecución bajo emulación ARM

```
$ qemu-aarch64 -L $SDKTARGETSYSROOT ./prueba_librobot_arm

== Arranque ==
  [OK ] robot_init() abre la sesion con el hardware
  [OK ] robot_activo() lo confirma
== Sensores de proximidad ==
  [OK ] la lectura frontal coincide con el mundo
  [OK ] la lectura izquierda coincide con el mundo
  [OK ] la lectura derecha coincide con el mundo
== LEDs indicadores ==
  [OK ] el LED de encendido queda prendido
  [OK ] el LED de obstaculo se apaga
== Motores: movimientos basicos ==
  [OK ] motores_avanzar mueve al robot hacia adelante
  [OK ] motores_retroceder lo devuelve
  [OK ] motores_girar_derecha aumenta el rumbo
== Motores: control diferencial ==
  [OK ] motores_set guarda la velocidad de cada motor
  [OK ] con el motor izquierdo mas rapido describe una curva a la derecha
  [OK ] motores_set satura en +-255
  [OK ] motores_curva(v, 100) detiene la llanta interior
== Odometria ==
  error de posicion: 5.4 cm sobre 70.9 cm recorridos (8%)
  [OK ] la odometria acumulo camino recorrido
  [OK ] el error de la navegacion a la estima se mantiene bajo el 20%
== Cierre ==
  [OK ] robot_shutdown cierra la sesion
  [OK ] los motores quedan detenidos

TODAS LAS PRUEBAS PASARON   (18/18)
```

## Qué prueba y qué no

- **Prueba:** el binario aarch64 producido por el SDK ejecuta y ejercita toda
  la API de `librobot` con resultados correctos. La cadena de compilación
  cruzada (SDK → binario ARM → ejecución) está validada de punta a punta.
- **No sustituye al target:** es emulación de CPU, no la Raspberry Pi 4 física
  con sus GPIO reales. La captura del binario corriendo **en la Pi** (issue #10)
  y el arranque de la imagen **en la Pi** (issue #7) siguen pendientes del kit.
