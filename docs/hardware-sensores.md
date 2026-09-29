# Radar, MPU-6050, LEDs y salida de audio

Montaje y acondicionamiento de todo lo que cuelga del riel lógico, más el servo del
radar, que es un motor y vive en el riel de potencia. El pinout completo está en
[`hardware-pinout.md`](hardware-pinout.md); cómo usa el software estas lecturas
(barrido, velocidad, tiempo de choque, mapa), en
[`navegacion-radar.md`](navegacion-radar.md).

---

## 1. Radar: un HC-SR04 sobre un servo de 180°

En vez de tres sensores fijos (frontal y dos laterales a 45°), **un solo HC-SR04 va
montado sobre un servo** que lo barre de derecha a izquierda. El servo se detiene en
siete ángulos —0°, 30°, …, 180°— y en cada uno se dispara el sensor.

| | 3 × HC-SR04 fijos | **HC-SR04 sobre servo** |
|---|---|---|
| Direcciones medidas | 3 | **7**, de lado a lado |
| Datos para el mapa | 3 rayos por lectura | 7 rayos por barrido, con la pose de cada uno |
| Elegir hacia dónde girar | izquierda o derecha | el más libre de 6 direcciones |
| Frecuencia de la lectura frontal | ~16 Hz | ~1.7 Hz (una vez cada ~0.6 s) |
| GPIO | 6 | 3 (`TRIG`, `ECHO` y la señal del servo) |
| Ecos cruzados entre sensores | posibles, hay que secuenciar | imposibles: hay un solo sensor |

La desventaja es la frecuencia de la lectura frontal: el sensor mira al frente una vez
por pasada. Se compensa con el **tiempo antes de chocar**, que entre dos lecturas
frontales se proyecta con lo que avanza el robot (ver
[`navegacion-radar.md`](navegacion-radar.md)), y con las lecturas a 60° y 120°, que
también cubren el ancho del robot a poca distancia.

> ⚠️ **Confirmar con el profesor.** El enunciado pide "al menos dos sensores de
> proximidad para detección frontal y lateral". El radar cubre las dos detecciones,
> pero con un solo sensor físico. Si la rúbrica cuenta sensores, el segundo HC-SR04 se
> monta fijo al frente en los GPIO 22 y 10, que quedaron libres.

### ⚠️ El pin `ECHO` saca 5 V — divisor obligatorio

El HC-SR04 se alimenta a 5 V y su salida `ECHO` es de 5 V. **Los GPIO de la Raspberry Pi
toleran 3.3 V como máximo absoluto y no tienen protección de entrada.** Conectar `ECHO`
directo a un GPIO destruye el pin, y con frecuencia el SoC completo.

Es el error que más Raspberry Pi mata en este tipo de proyecto, y **no es opcional**.

```
   HC-SR04 ECHO (5 V)
          │
        [R1 = 1 kΩ]
          │
          ├────────────► GPIO 27 (3.3 V máx)
          │
        [R2 = 2 kΩ]
          │
        GND_LOG
```

```
Vout = 5 V × R2 / (R1 + R2) = 5 V × 2k / 3k = 3.33 V
```

3.33 V está dentro de lo tolerable (el máximo absoluto del pin es 3.3 V más la caída del
diodo de protección de la entrada) y por encima del umbral de nivel alto, que es de
unos 2.0 V.

Alternativas si no hay resistencias de 2 kΩ:

| R1 | R2 | Vout |
|---|---|---|
| 1 kΩ | 2 kΩ | 3.33 V ✅ |
| 1.8 kΩ | 3.3 kΩ | 3.24 V ✅ |
| 10 kΩ | 20 kΩ | 3.33 V ⚠️ lento: la capacitancia del cable redondea el flanco |
| 1 kΩ | 1 kΩ | 2.50 V ⚠️ funciona, pero con poco margen sobre el umbral |

**Se usa 1 kΩ / 2 kΩ.** Un solo divisor.

`TRIG` es entrada del sensor y se maneja desde un GPIO de 3.3 V. El HC-SR04 lo reconoce
como nivel alto sin problema: **no lleva divisor**.

### El servo

| Parámetro | Valor |
|---|---|
| Modelo | SG90 (plástico) o **MG90S** (engranajes metálicos, recomendado: aguanta mejor el vaivén continuo) |
| Recorrido | 180° |
| Alimentación | **5 V desde el riel de potencia** (buck propio, ver [`hardware-alimentacion.md`](hardware-alimentacion.md)) |
| Señal | pulsos de 500–2500 µs a 50 Hz, desde GPIO 25 **a través de un PC817** |
| Velocidad sin carga | ~0.1 s cada 60° a 5 V |
| Consumo | 100–250 mA moviéndose; ~700 mA trabado |

El servo es un motor con su propio driver: sus picos de corriente y su ruido de
conmutación no pueden entrar al riel de la Raspberry Pi, que además ya está cerca de su
tope de 3 A. Por eso se alimenta del dominio de potencia y su señal cruza la barrera
óptica como las del L298N, en un quinto canal. Ese canal, a diferencia de los otros
cuatro, **no invierte la señal**: ver
[`hardware-aislamiento.md`](hardware-aislamiento.md).

Los pulsos los genera `pigpiod` por DMA (`set_servo_pulsewidth`), no el proceso: no se
deforman aunque la CPU esté ocupada con el audio o el servidor web.

### Barrido y tasa de muestreo

El barrido es de vaivén: 0 → 180 → 0, parando cada 30°. En cada parada:

| Fase | Duración |
|---|---|
| Viaje del servo, 30° a 1.7 ms/° | 51 ms |
| Asentamiento, para que el sensor deje de vibrar | 30 ms |
| Eco del HC-SR04 (a 2 m; 23 ms sin eco dentro de 4 m) | ~12 ms |
| **Total por lectura** | **~95 ms** |

De ahí la **tasa de muestreo del sistema** (issue #20):

| Magnitud | Valor |
|---|---|
| Lecturas por segundo | **~10** |
| Una pasada completa, 0 → 180 | 6 pasos, ~0.6 s |
| Lectura frontal (90°) | una vez por pasada: **cada ~0.6 s** |
| Cono frontal (60°, 90° y 120°) | 6 lecturas cada ~1.2 s |

El propio viaje del servo deja más de los 60 ms que pide el HC-SR04 entre disparos
para no oír el eco del pulso anterior.

Pasos de 30° no dejan huecos: el cono del HC-SR04 tiene ~15° de semiapertura, así que
lecturas contiguas se solapan en el borde.

### Montaje

| Parámetro | Valor | Motivo |
|---|---|---|
| Posición | Borde frontal, sobre el eje longitudinal | Ve los dos lados por igual |
| Eje del servo | **10 cm por delante del centro del robot** | Es `RADAR_EJE_ADELANTE_CM` en `lib/lib_radar.h`: si se monta en otro lado, cambiar la constante |
| Altura del sensor | 5–8 cm sobre el piso | Por debajo de 3 cm el cono rebota en el piso |
| Servo a 90° | Sensor mirando exactamente al frente | Se calibra con el pulso, no mecánicamente (ver [`odometria.md`](odometria.md)) |

El sensor gira sobre el eje del servo: al barrer no puede rozar el chasis, los
separadores ni los cables. El cable del sensor necesita holgura para media vuelta y
tiene que salir por el eje de giro, no por un costado.

**El sensor debe quedar horizontal**, sin inclinación hacia arriba o hacia abajo. Una
inclinación de pocos grados cambia por completo la distancia medida.

### Características a considerar en el software

| Parámetro | Valor | Implicación |
|---|---|---|
| Rango útil | 2 cm – 4 m | Por debajo de 2 cm devuelve basura |
| Ángulo del cono | ~15° | Objetos delgados (una pata de silla) pueden pasar desapercibidos |
| Tiempo de eco máximo | ~38 ms | El código usa un timeout de 50 ms, correcto |
| Superficie oblicua | El eco se desvía y no vuelve | "Sin eco" no significa "libre": el mapa no lo usa para despejar |

El cálculo de distancia en `lib/lib_sensors.c` usa 34300 cm/s como velocidad del sonido,
que corresponde a 20 °C. La variación con la temperatura ambiente es de menos de 2 % en
el rango de una sala: irrelevante para detectar obstáculos.

### Materiales

| Cantidad | Componente |
|---|---|
| 1 | Sensor HC-SR04 |
| 1 | Servo MG90S (o SG90), 180° |
| 1 | Soporte del HC-SR04 atornillable al brazo del servo |
| 1 | Soporte del servo al chasis |
| 1 | Resistencia 1 kΩ, 1/4 W |
| 1 | Resistencia 2 kΩ, 1/4 W |
| 1 | Capacitor 100 nF cerámico (desacople en `VCC` del sensor) |

---

## 2. MPU-6050: velocidad y giro

Acelerómetro y giroscopio de tres ejes por I2C, en el módulo GY-521. Aporta dos
magnitudes que el modelo de los motores no puede medir:

- **Aceleración en el eje de avance.** Integrada, da la velocidad real del robot: el
  arranque y la frenada, la llanta que patina, el choque que lo detiene. Con esa
  velocidad se calcula el **tiempo antes de chocar** con lo que el radar ve al frente.
- **Velocidad de giro en el eje vertical.** Integrada, da el rumbo. Reemplaza al rumbo
  estimado por diferencia de velocidades de las llantas, que es lo que más error mete
  en el mapa.

### Conexión

| GY-521 | Raspberry Pi | Pin físico |
|---|---|---|
| `VCC` | **3V3** | 1 |
| `GND` | GND (lógica) | 9 |
| `SDA` | GPIO 2 (SDA1) | 3 |
| `SCL` | GPIO 3 (SCL1) | 5 |
| `AD0` | GND → dirección `0x68` | — |
| `INT` | sin conectar: el software consulta a 50 Hz | — |
| `XDA`, `XCL` | sin conectar | — |

**Se alimenta a 3.3 V, no a 5 V.** El GY-521 trae regulador propio y acepta 5 V, pero
según la revisión del módulo sus resistencias pull-up de SDA/SCL pueden quedar
referidas a la entrada de alimentación: a 5 V pondrían 5 V en GPIO 2 y 3. A 3.3 V no
hay revisión que pueda hacerlo.

No hacen falta pull-up externas: la Raspberry Pi trae 1.8 kΩ a 3.3 V en GPIO 2 y 3.

### Montaje

- **Plano sobre el chasis y lo más cerca posible del punto medio entre las dos
  llantas.** Ahí el giro sobre el propio eje no produce aceleración centrípeta que se
  confunda con avance.
- **Eje X hacia el frente, Z hacia arriba** (la serigrafía del módulo lo indica). Si se
  monta de otra forma, ajustar `IMU_SIGNO_AVANCE` e `IMU_SIGNO_GIRO` en
  `lib/lib_imu.h`.
- **Rígido**: tornillos con separadores de nylon, no cinta de espuma. La espuma deja
  al sensor bamboleándose y lo que mide es el bamboleo.
- Lejos de la mano: tocarlo durante la calibración arruina el cero.

### Calibración

`robot_init()` promedia 100 lecturas (medio segundo) **con el robot quieto** y las toma
como el cero del acelerómetro y del giroscopio. Eso quita el sesgo de fábrica y también
la gravedad que se cuela por una inclinación leve del chasis. El servidor arranca antes
de que nada se mueva, así que la condición se cumple sola; lo único que hay que evitar
es encender el robot en la mano.

### Qué se puede esperar de él

Un acelerómetro **no mide velocidad**: mide cambios de velocidad. Integrar su sesgo
residual, aun después de calibrar, acumula un error que crece sin límite. Por eso la
odometría no usa la integral pura: un filtro complementario deja pasar la dinámica
rápida del MPU y toma del modelo de los motores la tendencia lenta, y con los motores
detenidos la velocidad se fuerza a cero. El detalle y los números medidos en el
simulador están en [`navegacion-radar.md`](navegacion-radar.md).

El filtro pasabajos interno del sensor se configura en 10 Hz: lo que interesa es el
movimiento del chasis, no la vibración de los motores.

### Materiales

| Cantidad | Componente |
|---|---|
| 1 | Módulo GY-521 (MPU-6050) |
| 2 | Separador de nylon M3 + tornillos |
| 4 | Cables Dupont hembra-hembra |

---

## 3. LEDs indicadores

Los cuatro estados que exige el enunciado:

| LED | Color sugerido | Significado | GPIO |
|---|---|---|---|
| Encendido | Verde | Sistema energizado y servidor activo | 16 |
| Autónomo | Azul | Modo autónomo en curso | 20 |
| Manual | Amarillo | Modo manual en curso | 21 |
| Obstáculo | Rojo | Obstáculo detectado | 26 |

Colores distintos entre sí: durante la demostración se evalúa que el evaluador pueda
leer el estado del robot de un vistazo, desde un metro de distancia.

### Resistencia limitadora

```
R = (3.3 V − Vf) / If
```

| Color | `Vf` | `If` = 5 mA | Comercial |
|---|---|---|---|
| Rojo | 2.0 V | 260 Ω | **270 Ω** |
| Amarillo | 2.1 V | 240 Ω | **270 Ω** |
| Verde | 2.2 V | 220 Ω | **220 Ω** |
| Azul | 3.0 V | 60 Ω | **68 Ω** |

Se usan 5 mA y no 15 mA por el presupuesto de corriente del conector
(ver [`hardware-pinout.md`](hardware-pinout.md)): los cuatro optoacopladores de los
motores consumen hasta 20 mA de los 50 mA disponibles. Autónomo y manual nunca están
encendidos a la vez, así que los LEDs suman como mucho 15 mA.

Un LED difuso de 5 mm a 5 mA es perfectamente visible en interiores. Si hiciera falta
más brillo, la salida correcta es un transistor por LED, no subir la corriente del GPIO.

> El LED azul tiene `Vf ≈ 3.0 V`, muy cerca de los 3.3 V del GPIO. Con 68 Ω la corriente
> real queda alrededor de 4 mA y es sensible a la variación entre unidades. Si se ve
> tenue, bajar a 47 Ω, nunca por debajo.

### Montaje

Los cuatro juntos, en la cara superior del chasis, ordenados en el mismo orden de la
tabla y **rotulados**. Un LED sin rótulo no comunica nada a quien evalúa.

Cátodo (patilla corta, lado achatado del encapsulado) a `GND_LOG`, ánodo al GPIO a
través de su resistencia.

### Materiales

| Cantidad | Componente |
|---|---|
| 1 | LED 5 mm verde + resistencia 220 Ω |
| 1 | LED 5 mm azul + resistencia 68 Ω |
| 1 | LED 5 mm amarillo + resistencia 270 Ω |
| 1 | LED 5 mm rojo + resistencia 270 Ω |
| 4 | Portaled 5 mm (opcional, mejora el acabado) |

---

## 4. Salida de audio: PAM8403 y un parlante

El audio **no sale por el jack de 3.5 mm**. La salida PWM analógica de la Raspberry Pi
—la misma que normalmente alimenta el jack— se saca al conector por **GPIO 18** con el
overlay `audremap`, pasa por un filtro RC y entra a un amplificador **PAM8403**, que
mueve un parlante.

| | Jack + LM386 (antes) | **GPIO 18 + PAM8403** |
|---|---|---|
| Cable | jack macho cortado | un cable desde el pin 12 |
| Amplificador | clase AB, ~50 % de eficiencia | **clase D, ~85 %**: menos calor y menos consumo del riel lógico |
| Componentes | LM386 + 5 capacitores + resistencia + zócalo | módulo armado + filtro de 3 pasivos |
| Potencia a 5 V | ~0.5 W | 1.3 W en 8 Ω, ~2.5 W en 4 Ω |
| GPIO | ninguno | GPIO 18 (19 queda tomado pero sin conectar) |
| Configuración | `dtparam=audio=on` | además `dtoverlay=audremap,pins_18_19` |

### Por qué `pins_18_19` y no el par por defecto

`audremap` saca el PWM de audio por GPIO 12/13 (por defecto) o 18/19. Se eligió
**`pins_18_19`** cuando 12/13 llevaban `ENA`/`ENB` de los motores; hoy 12/13 están libres
(el L298N va con jumpers), pero no hay motivo para mover el audio. Con el overlay, el
jack de la placa queda sin señal. La configuración está en la capa Yocto:

- `recipes-bsp/bootfiles/rpi-config_%.bbappend` agrega `dtoverlay=audremap,pins_18_19`
  a `config.txt`;
- `conf/local.conf.sample` agrega `overlays/audremap.dtbo` a
  `RPI_KERNEL_DEVICETREE_OVERLAYS`: meta-raspberrypi no lo copia por defecto, y sin el
  archivo en la partición de arranque el firmware ignora la línea sin avisar.

Para ALSA **nada cambia**: sigue siendo la card 1 con el control `PCM`, y `lib_audio`
abre el mismo dispositivo.

`pigpiod` temporiza su PWM por software con el periférico PCM (opción `-t 1`, la que usa
por defecto la unidad `pigpiod.service`). **No usar `-t 0`**: tomaría el periférico PWM y
silenciaría el audio.

### Un solo parlante: mezcla a mono

El PAM8403 es estéreo, pero el robot lleva un parlante, colgado de un solo canal. Para
no perder lo que una pista trae solo por la derecha, `lib_audio` mezcla cada bloque a
mono, `(L + R) / 2`, antes de mandarlo a ALSA (`LIB_AUDIO_MONO` en `lib/lib_audio.h`).
Los dos canales PWM llevan entonces la misma señal y da igual cuál de los dos GPIO
quede cableado; se usa el 18.

### Filtro y nivel

La salida es PWM de 0–3.3 V a una frecuencia muy por encima del audio. Antes del
amplificador hace falta quitar la portadora y bajar el nivel:

```
                         R1 4.7 kΩ          C2 1 µF
   GPIO 18 (PWM) ──────/\/\/\──────┬─────────┤├──────── L in  ┌─────────┐
                                   │                          │ PAM8403 │ L+ ──┐
                                 ┌─┴─┐                        │         │      ⊏ parlante
                        R2 1 kΩ  │   │  ═╪═ C1 10 nF          │         │ L− ──┘  8 Ω
                                 └─┬─┘   │                    │         │
   GND_LOG ────────────────────────┴─────┴──────────── G in ──┤         │
                                                     +5V_LOG ─┤ VDD     │
                                                              └─────────┘
```

| Componente | Valor | Función |
|---|---|---|
| R1 / R2 | 4.7 kΩ / 1 kΩ | Divisor: 3.3 V de pico a pico → 0.58 V |
| C1 | 10 nF | Con R1‖R2 = 824 Ω, pasabajos en **19 kHz**: deja el audio y quita la portadora |
| C2 | 1 µF | Bloquea la componente continua (0.29 V) antes de la entrada del amplificador |
| Desacople de `VDD` | 100 µF + 100 nF | En los bornes del módulo: el clase D consume a pulsos |

```
Nivel a plena escala:  1.65 V de pico × 1 k / 5.7 k = 0.29 V de pico ≈ 0.20 V RMS
Ganancia del PAM8403:  24 dB ≈ × 15.8                 → 3.2 V RMS en el parlante
Potencia:              3.2² / 8 Ω ≈ 1.3 W   (≈ 2.5 W en un parlante de 4 Ω)
```

Con el volumen de software al máximo el amplificador llega justo a su potencia útil, sin
saturar. Muchos módulos PAM8403 traen sus propios capacitores de entrada; C2 queda en
serie con ellos y no molesta.

### ⚠️ Salidas en puente: ninguna va a tierra

El PAM8403 maneja el parlante **en puente** (BTL): `L+` y `L−` son dos salidas que se
mueven en contrafase, y ninguna es tierra. **Conectar `L−` a GND —o unir `L−` con `R−`—
cortocircuita una salida** y quema el amplificador. El parlante va entre `L+` y `L−` y
nada más.

### Alimentación y ruido

El PAM8403 se alimenta de **`+5V_LOG`**, no del riel de potencia: el ruido de conmutación
de los motores y del servo no debe entrar al audio. Cables de audio cortos, separados de
los de motor y lejos del cable del servo.

### Control de volumen

1. **Software** — `amixer set PCM <n>%` desde `librobot`, controlado desde el panel web.
   Es el que exige el enunciado.
2. **Hardware** — si el módulo trae potenciómetro, fija el nivel máximo durante el
   montaje. Sin potenciómetro, el nivel máximo lo fija el divisor R1/R2.

### Materiales

| Cantidad | Componente |
|---|---|
| 1 | Módulo amplificador PAM8403 (con o sin potenciómetro) |
| 1 | Parlante 8 Ω, 1–3 W, 40–57 mm |
| 1 | Resistencia 4.7 kΩ, 1/4 W |
| 1 | Resistencia 1 kΩ, 1/4 W |
| 1 | Capacitor 10 nF cerámico |
| 1 | Capacitor 1 µF (cerámico o de película) |
| 1 | Capacitor 100 µF / 10 V electrolítico |
| 1 | Capacitor 100 nF cerámico |

---

## 5. Diagrama eléctrico del dominio lógico

```
                          ┌───────────────────────────┐
                          │    Raspberry Pi 4 B       │
        ┌─────────────────┤                           │
        │  5 V USB-C      │  GPIO 17 ──► TRIG ┐       │
        │  (riel lógico)  │  GPIO 27 ◄── ECHO ┤HC-SR04│──[div 1k/2k]
        │                 │                   ┘ (sobre el servo)
        │                 │  GPIO  2 ◄─► SDA ┐        │
        │                 │  GPIO  3 ──► SCL ┤MPU-6050│  VCC = 3V3
        │                 │                  ┘        │
        │                 │  GPIO 16 ──[220Ω]──► LED verde
        │                 │  GPIO 20 ──[ 68Ω]──► LED azul
        │                 │  GPIO 21 ──[270Ω]──► LED amarillo
        │                 │  GPIO 26 ──[270Ω]──► LED rojo
        │                 │                           │
        │                 │  GPIO  5 ──[390Ω]──►┐ 4×  │
        │                 │  GPIO  6 ──[390Ω]──►│PC817│──► IN1–IN4 del L298N
        │                 │  GPIO 23 ──[390Ω]──►│     │    (PWM, aislado)
        │                 │  GPIO 24 ──[390Ω]──►┘     │
        │                 │  GPIO 25 ──[680Ω]──► PC817 ──► servo (aislado)
        │                 │                           │
        │                 │  GPIO 18 ──[RC]──► PAM8403 ──► parlante 8 Ω
        │                 └───────────────────────────┘
        │
   ┌────┴─────┐
   │ Buck 5 V │◄── BMS ◄── pack 2S 18650
   │  MP2307  │
   └──────────┘
```

El dominio de potencia y el aislamiento están en
[`hardware-aislamiento.md`](hardware-aislamiento.md); la alimentación, en
[`hardware-alimentacion.md`](hardware-alimentacion.md). Los mismos diagramas, dibujados,
están en [`../documentación/hardware-diagramas.pdf`](../documentación/hardware-diagramas.pdf).

---

## Verificación

### HC-SR04

**Antes de conectar al GPIO**, con el sensor alimentado a 5 V y el divisor armado:

```
Medir con el multímetro entre la salida del divisor y GND_LOG,
mientras se dispara TRIG a mano.
  → Debe leer un máximo de 3.3 V. Nunca 5 V.
```

Si mide 5 V, el divisor está mal conectado. **No conectar al GPIO hasta corregirlo.**

Ya con el sistema corriendo y el robot quieto frente a una pared a distancia conocida,
la lectura "Frente" del radar en el panel web:

| Distancia real | Lectura aceptable |
|---|---|
| 10 cm | 9–11 cm |
| 50 cm | 48–52 cm |
| 100 cm | 96–104 cm |

Un error mayor al 5 % suele ser inclinación del sensor o una superficie que absorbe el
ultrasonido (tela, espuma).

### Servo

1. Con el servo **desconectado**, medir la señal a la salida del optoacoplador contra
   `GND_POT`: 0 V en reposo; ~4.8 V durante los pulsos.
2. Conectar el servo sin el sensor montado y mirar el panel web: el radar tiene que
   barrer de lado a lado sin trabarse en los extremos. Si golpea el tope a 0° o 180°,
   ajustar `SERVO_PULSO_0_US` / `SERVO_PULSO_180_US`.

### MPU-6050

```bash
ls /dev/i2c-1                                  # existe el bus
journalctl -u robot-server | grep imu          # "MPU-6050 listo" y el sesgo calibrado
```

En el panel web, tarjeta de movimiento: con el robot quieto la velocidad marca 0 y la
fuente dice **MPU**. Si dice **MOTORES**, el sensor no contestó: revisar SDA/SCL, `AD0`
y que `VCC` esté en 3.3 V.

### Audio

```bash
aplay -l                                    # la card 1 (bcm2835 Headphones) sigue ahí
speaker-test -c 2 -t sine -f 440 -l 1       # tono de prueba: debe sonar por el parlante
amixer set PCM 80%                          # control de volumen
mpg123 /opt/robot/audio/notify_startup.mp3  # un sonido de evento real
```

Si `speaker-test` corre pero no suena nada: medir con el multímetro en DC el pin 12 del
conector mientras suena el tono. Con el PWM activo marca alrededor de 1.6 V (su valor
medio); si marca 0 V, el overlay no se cargó: revisar que `audremap.dtbo` esté en
`/boot/overlays/` y la línea `dtoverlay=audremap,pins_18_19` en `/boot/config.txt`.

Con el robot navegando y el audio sonando a la vez, escuchar si aparece zumbido
sincronizado con el PWM de los motores o con el vaivén del servo. Si aparece, revisar
la separación física de los cables de audio y de motor.

---

## Estado

- [x] Radar definido: HC-SR04 sobre servo de 180°, siete ángulos, montaje y geometría
- [x] Divisor de tensión especificado para `ECHO` — **crítico para no dañar la Pi**
- [x] Tasa de muestreo del barrido calculada (~10 lecturas/s, frente cada ~0.6 s)
- [x] MPU-6050: conexión a 3.3 V, montaje y calibración definidos
- [x] LEDs, colores y resistencias calculados contra el presupuesto de corriente
- [x] Salida de audio decidida (GPIO 18 + PAM8403) con el filtro calculado
- [x] Diagrama eléctrico del dominio lógico
- [x] Procedimientos de verificación
- [x] Barrido, velocidad y tiempo de choque implementados en `librobot` y probados en el simulador
- [ ] Confirmar con el profesor que el radar cumple "al menos dos sensores"
- [ ] Componentes conseguidos y montados — **pendiente**
- [ ] Calibración del servo, del sensor y del montaje del MPU — **pendiente**
