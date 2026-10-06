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

Capturada el 5 de octubre de 2026 con `sim/construir_arm.sh`. Se omiten las líneas
informativas y el dibujo del mapa; quedan las comprobaciones.

```
$ qemu-aarch64 -L $SDKTARGETSYSROOT ./prueba_librobot_arm

== Arranque ==
  [OK ] robot_init() abre la sesion con el hardware
  [OK ] robot_activo() lo confirma
  [OK ] el MPU-6050 contesta en el bus I2C
== MPU-6050 ==
  [OK ] imu_leer() entrega una lectura
  [OK ] la calibracion quita el sesgo del acelerometro
  [OK ] la calibracion quita el sesgo del giroscopo
  [OK ] el eje vertical mide la gravedad
  [OK ] la odometria usa el MPU
== Radar: HC-SR04 sobre el servo ==
  [OK ] el radar completa un barrido de 0 a 180 grados
  [OK ] hay una lectura por cada angulo
  [OK ] cada angulo mide lo que hay en el mundo en esa direccion
  [OK ] robot_distancia_frontal() es el servo a 90
  [OK ] robot_distancia_izquierda() es el servo a 180
  [OK ] robot_distancia_derecha() es el servo a 0
  [OK ] radar_rayo() proyecta el eco frontal sobre la pared norte
== Servo ==
  [OK ] servo_mover(45) manda un pulso de 1000 us
  [OK ] servo_angulo() recuerda el angulo ordenado
  [OK ] satura en 0 y 180 grados (500 y 2500 us)
  [OK ] la media vuelta va a 1/3 de la velocidad del servo (~0.9 s)
== LEDs indicadores ==
  [OK ] el LED de encendido queda prendido
  [OK ] el LED de obstaculo se apaga
== Motores: movimientos basicos ==
  [OK ] motores_avanzar mueve al robot hacia adelante
  [OK ] motores_retroceder lo devuelve
  [OK ] motores_girar_derecha aumenta el rumbo
== Motores: control diferencial ==
  [OK ] con velocidad fija, cualquier velocidad va al maximo
  [OK ] motores_curva gira con una llanta adelante y la otra atras
  [OK ] la curva a la derecha gira sobre el eje hacia la derecha
  [OK ] motores_set satura en +-255
== Velocidad con el MPU-6050 ==
  [OK ] al arrancar, el MPU ve la inercia que el modelo de motores ignora
  [OK ] en crucero la velocidad estimada sigue a la real (+-15%)
  [OK ] detenido, la velocidad vuelve a cero (ZUPT)
== Tiempo antes de chocar ==
  [OK ] con el robot quieto no hay riesgo de choque (-1)
  [OK ] el sensor vuelve a mirar al frente
  [OK ] la lectura frontal detecta la pared
  [OK ] el tiempo de choque coincide con el real (+-15%)
  [OK ] entre lecturas frontales la cuenta regresiva sigue bajando
  [OK ] al detenerse, el riesgo desaparece (-1)
== Odometria ==
  [OK ] la odometria acumulo camino recorrido
  [OK ] el error de la navegacion a la estima se mantiene bajo el 20%
  [OALSA lib /usr/src/debug/alsa-lib/1.2.11/src/confmisc.c:165:(snd_config_get_card) Cannot get card index for 1
== Audio (API, sin reproduccion real) ==
  [OK ] el volumen se fija y se lee (roundtrip)
  [OK ] el estado inicial es DETENIDO
  [OK ] el listado de pistas no es negativo
== Cierre ==
  [OK ] robot_shutdown cierra la sesion
  [OK ] los motores quedan detenidos
  [OK ] el servo queda sin pulsos
  [OK ] el bus I2C del MPU queda liberado
47/47 comprobaciones
TODAS LAS PRUEBAS PASARON
```

## Qué prueba y qué no

- **Prueba:** el binario aarch64 producido por el SDK ejecuta y ejercita toda
  la API de `librobot` con resultados correctos. La cadena de compilación
  cruzada (SDK → binario ARM → ejecución) está validada de punta a punta.
- **No sustituye al target:** es emulación de CPU, no la Raspberry Pi 4 física
  con sus GPIO reales. La ejecución **en la Pi** está en
  [`ejecucion-target.md`](ejecucion-target.md).
