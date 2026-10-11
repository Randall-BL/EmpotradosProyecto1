# Prueba 02 — `librobot` contra el simulador

| | |
|---|---|
| **Fecha** | 11 de octubre de 2026 |
| **Commit probado** | `b1187e1` (mismo árbol que `main` en `82f6472`) |
| **Entorno** | WSL2, Ubuntu 24.04.3 LTS, gcc 13.3.0, x86_64 |
| **Tipo** | Ejecución real de la biblioteca sobre el hardware simulado de `sim/` |
| **Resultado** | **Pasa**: 49 de 49 comprobaciones, código de salida 0, 23 s |

## Qué verifica

Compila todos los módulos de `lib/` con el gcc del host, los enlaza con el simulador
(`sim/pigpio_sim.c` y `sim/mundo.c`) y ejecuta `sim/prueba_librobot`, que ejercita
cada módulo y compara lo que la biblioteca reporta contra el mundo simulado.

## Cómo se ejecuta

```sh
sudo apt install libmpg123-dev libasound2-dev     # cabeceras de desarrollo
cd sim
./construir.sh
./prueba_librobot; echo "salida: $?"
```

En esta ejecución no había permisos de administrador, así que las cabeceras y las
bibliotecas se tomaron de los mismos paquetes `.deb` extraídos en una carpeta aparte
(`dpkg -x`), indicada con `C_INCLUDE_PATH`, `LIBRARY_PATH` y `LD_LIBRARY_PATH`. Los
scripts de `sim/` se corrieron sin modificar.

## Resultado

| Sección | Comprobaciones | Estado |
|---|---|---|
| Arranque | 3 | OK |
| Sensores de desnivel | 2 | OK |
| MPU-6050 | 5 | OK |
| Radar: HC-SR04 sobre el servo | 7 | OK |
| Servo | 4 | OK |
| LEDs indicadores | 2 | OK |
| Motores: movimientos básicos | 3 | OK |
| Motores: control diferencial | 4 | OK |
| Velocidad con el MPU-6050 | 3 | OK |
| Tiempo antes de chocar | 6 | OK |
| Odometría | 3 | OK |
| Audio (API, sin reproducción real) | 3 | OK |
| Cierre | 4 | OK |
| **Total** | **49** | **OK** |

Valores medidos en esta corrida:

- Radar: los 7 ángulos coinciden con el mundo simulado con un error máximo de 0,1 cm.
- Odometría: 2,8 cm de error sobre 91,2 cm recorridos (3 %) y 0,6° de error de rumbo.
  Los límites de la prueba son 20 % y 5°.
- Tiempo de choque: 3,97 s estimado contra 4,35 s real, dentro del ±15 % exigido.

### Compilación

`./construir.sh` terminó con código 0 y un único aviso del compilador:

```text
../lib/lib_audio.c: In function ‘lib_audio_set_volume’:
../lib/lib_audio.c:826:5: warning: this ‘if’ clause does not guard... [-Wmisleading-indentation]
  826 |     if (vol < 0) vol = 0; if (vol > 100) vol = 100;
      |     ^~
../lib/lib_audio.c:826:27: note: ...this statement, but the latter is misleadingly indented as if it were guarded by the ‘if’
  826 |     if (vol < 0) vol = 0; if (vol > 100) vol = 100;
      |                           ^~
listo: ./prueba_librobot
```

### Salida completa de `./prueba_librobot`

<details>
<summary>159 líneas de salida estándar</summary>

```text
Prueba de librobot contra el robot simulado

== Arranque ==
[sim] pigpiod simulado: robot con radar y MPU-6050 en el centro de una sala de 4x4 m
[caida] sensores de desnivel en GPIO 4 (izq) y 8 (der)
[leds] Inicializando pines GPIOs (via pigpiod)
[leds] GPIO 16 configurado para LED_POWER
[leds] GPIO 20 configurado para LED_AUTONOMOUS
[leds] GPIO 21 configurado para LED_MANUAL
[leds] GPIO 26 configurado para LED_OBSTACLE
[sim] LED encendido  ENCENDIDO
[leds] LED_POWER      GPIO 16 → ON
[leds] Inicializado GPIOs OK
[imu] MPU-6050 listo en /dev/i2c-1, direccion 0x68
[imu] calibrado con 100 muestras: sesgo ax +0.0199 g, ay +0.0001 g, gz +1.51 grados/s; vertical +1.00 g
[radar] barriendo de 0 a 180 grados en pasos de 30
  [OK ] robot_init() abre la sesion con el hardware
  [OK ] robot_activo() lo confirma
  [OK ] el MPU-6050 contesta en el bus I2C

== Sensores de desnivel ==
  [OK ] sobre el piso ningun sensor marca desnivel
  [OK ] en el centro de la sala hay piso

== MPU-6050 ==
  en reposo: avance -2.49 cm/s2 | giro +0.018 grados/s | vertical 0.999 g
  [OK ] imu_leer() entrega una lectura
  [OK ] la calibracion quita el sesgo del acelerometro
  [OK ] la calibracion quita el sesgo del giroscopo
  [OK ] el eje vertical mide la gravedad
  [OK ] la odometria usa el MPU

== Radar: HC-SR04 sobre el servo ==
  [OK ] el radar completa un barrido de 0 a 180 grados
  [OK ] hay una lectura por cada angulo
  servo   0 grados ->  200.0 cm   (mundo  200.0 cm)
  servo  30 grados ->   60.0 cm   (mundo   60.0 cm)
  servo  60 grados ->   80.1 cm   (mundo   80.0 cm)
  servo  90 grados ->  190.0 cm   (mundo  190.0 cm)
  servo 120 grados ->  219.3 cm   (mundo  219.4 cm)
  servo 150 grados ->  230.9 cm   (mundo  230.9 cm)
  servo 180 grados ->  200.0 cm   (mundo  200.0 cm)
  [OK ] cada angulo mide lo que hay en el mundo en esa direccion
  [OK ] robot_distancia_frontal() es el servo a 90
  [OK ] robot_distancia_izquierda() es el servo a 180
  [OK ] robot_distancia_derecha() es el servo a 0
  eco frontal en el mundo: (-0.0, 200.0)
  [OK ] radar_rayo() proyecta el eco frontal sobre la pared norte

== Servo ==
  [OK ] servo_mover(45) manda un pulso de 1000 us
  [OK ] servo_angulo() recuerda el angulo ordenado
  [OK ] satura en 0 y 180 grados (500 y 2500 us)
  media vuelta en 902 ms
  [OK ] la media vuelta va a 1/3 de la velocidad del servo (~0.9 s)

== LEDs indicadores ==
  [OK ] el LED de encendido queda prendido
[sim] LED obstaculo  ENCENDIDO
[leds] LED_OBSTACLE   GPIO 26 → ON
[sim] LED obstaculo  apagado
[leds] LED_OBSTACLE   GPIO 26 → OFF
  [OK ] el LED de obstaculo se apaga

== Motores: movimientos basicos ==
  avanzo 24.7 cm hacia el norte
  [OK ] motores_avanzar mueve al robot hacia adelante
  [OK ] motores_retroceder lo devuelve
  giro de 360 a 100 grados
  [OK ] motores_girar_derecha aumenta el rumbo

== Motores: control diferencial ==
  [OK ] con velocidad fija, cualquier velocidad va al maximo
  [OK ] motores_curva gira con una llanta adelante y la otra atras
  curva: se desplazo 0.0 cm y giro 83 grados
  [OK ] la curva a la derecha gira sobre el eje hacia la derecha
  [OK ] motores_set satura en +-255

== Velocidad con el MPU-6050 ==
  arrancando: real 15.8 | MPU 17.2 | modelo de motores 30.0 cm/s
  [OK ] al arrancar, el MPU ve la inercia que el modelo de motores ignora
  en crucero: real 28.0 | MPU 30.6 cm/s
  [OK ] en crucero la velocidad estimada sigue a la real (+-15%)
  detenido: MPU 0.00 cm/s
  [OK ] detenido, la velocidad vuelve a cero (ZUPT)

== Tiempo antes de chocar ==
  [OK ] con el robot quieto no hay riesgo de choque (-1)
  [OK ] el sensor vuelve a mirar al frente
  pared a 122.5 cm, medida a 30.6 cm/s: choque en 3.97 s (real 4.35 s)
  [OK ] la lectura frontal detecta la pared
  [OK ] el tiempo de choque coincide con el real (+-15%)
  medio segundo despues: 3.50 s
  [OK ] entre lecturas frontales la cuenta regresiva sigue bajando
  [OK ] al detenerse, el riesgo desaparece (-1)

== Odometria ==
  odometria : dx   27.1  dy    9.9  rumbo   145
  mundo real: dx   25.2  dy    7.8  rumbo   145
  error de posicion: 2.8 cm sobre 91.2 cm recorridos (3%); error de rumbo: 0.6 grados
  [OK ] la odometria acumulo camino recorrido
  [OK ] el error de la navegacion a la estima se mantiene bajo el 20%
  [OK ] con el giroscopo, el rumbo estimado queda a menos de 5 grados

== Audio (API, sin reproduccion real) ==
[audio] Escaneo: 9 pista(s) en '../audio', playlist de 9
[audio] Inicializado — dir='../audio'  pistas=9  volumen=85%
  [OK ] el volumen se fija y se lee (roundtrip)
  [OK ] el estado inicial es DETENIDO
  pistas encontradas en ../audio: 8
  [OK ] el listado de pistas no es negativo
[audio] destruido

== Cierre ==
[leds] Apagando LEDs
[sim] LED encendido  apagado
[leds] LED_POWER      GPIO 16 → OFF
[leds] LED_AUTONOMOUS GPIO 20 → OFF
[leds] LED_MANUAL     GPIO 21 → OFF
[leds] LED_OBSTACLE   GPIO 26 → OFF
[sim] pigpiod simulado cerrado
  [OK ] robot_shutdown cierra la sesion
  [OK ] los motores quedan detenidos
  [OK ] el servo queda sin pulsos
  [OK ] el bus I2C del MPU queda liberado

Recorrido del robot en la sala simulada:

+-------------------------------------------------------------+
|                                                             |
|                                                             |
|                                                             |
|                                                             |
|                                    ##########               |
|                                    ##########               |
|                                    ##########               |
|                                    ##########               |
|                                    ##########               |
|                              ..    ##########               |
|                              ...                            |
|                              . .v                           |
|                              .                              |
|       ##########                                            |
|       ##########                                            |
|       ##########                                            |
|                                                             |
|                                                             |
|                                              ~~~~~~~~~~~~~~ |
|                                              ~~~~~~~~~~~~~~ |
|                         ###############      ~~~~~~~~~~~~~~ |
|                         ###############      ~~~~~~~~~~~~~~ |
|                         ###############      ~~~~~~~~~~~~~~ |
|                                              ~~~~~~~~~~~~~~ |
|                                                             |
+-------------------------------------------------------------+
  # mueble   ~ grada   . recorrido   ^>v< robot (25, 8) rumbo 145

49/49 comprobaciones
TODAS LAS PRUEBAS PASARON
```

</details>

Salida de error (2 líneas):

```text
ALSA lib confmisc.c:165:(snd_config_get_card) Cannot get card index for 1
ALSA lib confmisc.c:165:(snd_config_get_card) Cannot get card index for 1
```

## Observaciones

- **El conteo documentado está desactualizado.** `sim/README.md` habla de 47
  comprobaciones; el programa ejecuta 49. Las dos de más son las de "Sensores de
  desnivel", que el README no lista.
- **Aviso de compilación en `lib/lib_audio.c:826`** (`-Wmisleading-indentation`): dos
  `if` en una misma línea. El comportamiento es correcto, porque el volumen se
  limita a 0–100, pero `CONTRIBUTING.md` pide compilar sin avisos.
- **Los dos mensajes de ALSA son esperados.** `lib_audio` abre la tarjeta 1, la salida
  PWM de la Raspberry Pi, y este host no tiene tarjeta de sonido.

## Límites

- El hardware es simulado: no hay GPIO, I2C ni audio reales.
- El binario es x86_64 compilado con el gcc del host. No prueba la compilación
  cruzada ni el `.so` que construye la receta `librobot`.
