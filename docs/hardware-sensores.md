# Sensores, LEDs y salida de audio

Montaje y acondicionamiento de todo lo que cuelga del riel lógico. El pinout completo
está en [`hardware-pinout.md`](hardware-pinout.md).

---

## 1. Sensores de proximidad HC-SR04

Tres sensores: uno frontal y dos laterales. El enunciado pide un mínimo de dos con
cobertura frontal y lateral; el tercero permite girar hacia el lado libre en vez de
elegir al azar, y alimenta el mapa de recorrido con celdas a ambos lados.

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
          ├────────────► GPIO (3.3 V máx)
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

**Se usa 1 kΩ / 2 kΩ.** Tres divisores, uno por sensor.

`TRIG` es entrada del sensor y se maneja desde un GPIO de 3.3 V. El HC-SR04 lo reconoce
como nivel alto sin problema: **no lleva divisor**.

### Montaje

| Sensor | Orientación | Altura sobre el piso |
|---|---|---|
| Frontal | 0°, al frente | 5–8 cm |
| Lateral izquierdo | 45° a la izquierda | 5–8 cm |
| Lateral derecho | 45° a la derecha | 5–8 cm |

Los laterales van a 45° y no a 90°: a 90° el sensor solo ve la pared cuando el robot ya
está pegado a ella. A 45° la detecta con anticipación y da tiempo a corregir el rumbo
sin frenar.

La altura de 5–8 cm evita que el cono de ultrasonido rebote en el piso. Por debajo de
3 cm el sensor reporta la distancia al suelo en vez de al obstáculo.

**Los tres deben quedar en el mismo plano horizontal**, sin inclinación hacia arriba o
hacia abajo. Una inclinación de pocos grados cambia por completo la distancia medida.

### Características a considerar en el software

| Parámetro | Valor | Implicación |
|---|---|---|
| Rango útil | 2 cm – 4 m | Por debajo de 2 cm devuelve basura |
| Ángulo del cono | ~15° | Objetos delgados (una pata de silla) pueden pasar desapercibidos |
| Tiempo de eco máximo | ~38 ms | El código usa un timeout de 50 ms, correcto |
| Frecuencia máxima de disparo | 20 Hz por sensor | Disparar más rápido lee el eco del pulso anterior |

**Los tres sensores no deben dispararse a la vez**: el eco de uno llega al receptor de
otro y da lecturas falsas. Hay que secuenciarlos, dejando al menos 10 ms entre disparos.
Con tres sensores en secuencia el ciclo completo dura unos 60 ms, o sea **~16 Hz de
frecuencia de muestreo efectiva**, que es lo que hay que documentar como tasa de
muestreo del sistema (issue #20).

El cálculo de distancia en `lib/lib_sensors.c` usa 34300 cm/s como velocidad del sonido,
que corresponde a 20 °C. La variación con la temperatura ambiente es de menos de 2 % en
el rango de una sala: irrelevante para detectar obstáculos.

### Materiales

| Cantidad | Componente |
|---|---|
| 3 | Sensor HC-SR04 |
| 3 | Resistencia 1 kΩ, 1/4 W |
| 3 | Resistencia 2 kΩ, 1/4 W |
| 3 | Capacitor 100 nF cerámico (desacople en `VCC`) |
| 3 | Soporte de montaje HC-SR04 |

---

## 2. LEDs indicadores

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

Se usan 5 mA y no 15 mA por el presupuesto de corriente del conector: los seis
optoacopladores ya consumen 30 mA de los 50 mA totales disponibles
(ver [`hardware-aislamiento.md`](hardware-aislamiento.md)). Cuatro LEDs a 5 mA suman
20 mA y cierran justo el presupuesto.

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

## 3. Salida de audio

Se usa el **jack analógico de 3.5 mm** de la Raspberry Pi con un amplificador LM386,
en vez de un DAC I2S.

| | **Jack 3.5 mm + LM386** | DAC I2S |
|---|---|---|
| Calidad | PWM de 11 bits, suficiente para MP3 y avisos | Superior |
| GPIO ocupados | **Ninguno** | 3 (BCK, LRCK, DATA) |
| Configuración en Yocto | `dtparam=audio=on` | Overlay de árbol de dispositivos + módulo |
| Componentes | LM386 + pasivos | Módulo comprado |
| Riesgo | Bajo | Overlay que puede no cargar |

**Se elige el jack.** No consume GPIO —que están al límite— y la configuración ya está
resuelta en la capa Yocto: `alsa-config` fuerza la card 1 como salida por defecto y
`rpi-config.bbappend` activa `dtparam=audio=on` con `audio_pwm_mode=2`.

La calidad del PWM de la Pi es mediocre para música, pero el requerimiento es reproducir
MP3 y avisos por un altavoz pequeño montado en un robot. La diferencia no se percibe.

### Amplificador LM386

La salida del jack entrega unos 100 mV RMS: mueve audífonos, no un altavoz. El LM386
amplifica con ganancia de 20 (por defecto) o 200 (con un capacitor entre los pines 1
y 8).

```
                     +5V_LOG
                        │
                     [100 µF]──┐
                        │       │
   Jack 3.5mm           │       │
    (canal L) ──[C 10µF]┤       │
                        │       │
                   ┌────┴────┐  │
                   │ 2 (+in) │  │
              GND──┤ 3 (−in) │  │
                   │       6 ├──┘  Vs
      Potenciómetro│         │
        10 kΩ ─────┤ 1     5 ├──[C 220µF]──┬── Altavoz 8 Ω
      (volumen)    │       8 │             │
                   │ 4 (GND) │           [R 10 Ω]
                   └────┬────┘             │
                        │              [C 47 nF]
                    GND_LOG                │
                                        GND_LOG
```

| Componente | Función |
|---|---|
| C de entrada 10 µF | Bloquea el offset DC del jack |
| Potenciómetro 10 kΩ | Ganancia de entrada: volumen físico, independiente del software |
| C de salida 220 µF | Acopla el altavoz, bloquea DC |
| Red 10 Ω + 47 nF | Red de Zobel: estabiliza el amplificador ante la carga inductiva del altavoz |
| C 100 µF en `Vs` | Desacople de alimentación |

El LM386 se alimenta del **riel lógico**, no del de potencia: así el ruido de conmutación
de los motores no entra al audio. Aun así, los cables de audio deben ir separados de los
de motor y, si es posible, apantallados.

### Control de volumen

Dos niveles, complementarios:

1. **Software** — `amixer set PCM <n>%` desde `librobot`, controlado desde el panel web.
   Es el que exige el enunciado.
2. **Hardware** — el potenciómetro de 10 kΩ, para fijar el nivel máximo durante el
   montaje y no tener que ajustarlo en la demostración.

### Materiales

| Cantidad | Componente |
|---|---|
| 1 | Amplificador LM386N-1 |
| 1 | Altavoz 8 Ω, 0.5–2 W |
| 1 | Potenciómetro 10 kΩ lineal |
| 1 | Cable jack 3.5 mm macho |
| 1 | Capacitor 10 µF electrolítico |
| 1 | Capacitor 220 µF electrolítico |
| 1 | Capacitor 100 µF electrolítico |
| 1 | Capacitor 47 nF cerámico |
| 1 | Resistencia 10 Ω, 1/4 W |
| 1 | Zócalo DIP-8 |

---

## 4. Diagrama eléctrico del dominio lógico

```
                          ┌───────────────────────────┐
                          │    Raspberry Pi 4 B       │
        ┌─────────────────┤                           │
        │  5 V USB-C      │  GPIO 17 ──► TRIG frontal │
        │  (riel lógico)  │  GPIO 27 ◄── ECHO frontal │──[div 1k/2k]
        │                 │  GPIO 22 ──► TRIG lat.izq │
        │                 │  GPIO 10 ◄── ECHO lat.izq │──[div 1k/2k]
        │                 │  GPIO  9 ──► TRIG lat.der │
        │                 │  GPIO 11 ◄── ECHO lat.der │──[div 1k/2k]
        │                 │                           │
        │                 │  GPIO 16 ──[220Ω]──► LED verde
        │                 │  GPIO 20 ──[ 68Ω]──► LED azul
        │                 │  GPIO 21 ──[270Ω]──► LED amarillo
        │                 │  GPIO 26 ──[270Ω]──► LED rojo
        │                 │                           │
        │                 │  GPIO 12 ──[390Ω]──►┐     │
        │                 │  GPIO  5 ──[390Ω]──►│     │
        │                 │  GPIO  6 ──[390Ω]──►│ 6×  │
        │                 │  GPIO 13 ──[390Ω]──►│PC817│──► etapa de potencia
        │                 │  GPIO 23 ──[390Ω]──►│     │    (aislada)
        │                 │  GPIO 24 ──[390Ω]──►┘     │
        │                 │                           │
        │                 │  Jack 3.5 mm ──► LM386 ──► altavoz 8 Ω
        │                 └───────────────────────────┘
        │
   ┌────┴─────┐
   │ Buck 5 V │◄── BMS ◄── pack 2S 18650
   │  MP2307  │
   └──────────┘
```

El dominio de potencia y el aislamiento están en
[`hardware-aislamiento.md`](hardware-aislamiento.md); la alimentación, en
[`hardware-alimentacion.md`](hardware-alimentacion.md).

---

## Verificación

### Sensores

**Antes de conectar al GPIO**, con el sensor alimentado a 5 V y el divisor armado:

```
Medir con el multímetro entre la salida del divisor y GND_LOG,
mientras se dispara TRIG a mano.
  → Debe leer un máximo de 3.3 V. Nunca 5 V.
```

Si mide 5 V, el divisor está mal conectado. **No conectar al GPIO hasta corregirlo.**

Ya con el sistema corriendo, contra una pared a distancia conocida:

| Distancia real | Lectura aceptable |
|---|---|
| 10 cm | 9–11 cm |
| 50 cm | 48–52 cm |
| 100 cm | 96–104 cm |

Un error mayor al 5 % suele ser inclinación del sensor o una superficie que absorbe el
ultrasonido (tela, espuma).

### LEDs

```bash
# Encender los cuatro, uno por uno
for p in 16 20 21 26; do pigs modes $p w; pigs w $p 1; sleep 1; pigs w $p 0; done
```

Medir la corriente en serie con uno de ellos: debe rondar los 5 mA.

### Audio

```bash
aplay -l                                    # la tarjeta debe aparecer como card 1
speaker-test -c 2 -t sine -f 440 -l 1       # tono de prueba
amixer set PCM 80%                          # control de volumen
mpg123 /opt/robot/audio/notify_startup.mp3  # un sonido de evento real
```

Con el robot navegando y el audio sonando a la vez, escuchar si aparece zumbido
sincronizado con el PWM de los motores. Si aparece, revisar la separación física de los
cables de audio y de motor.

---

## Estado

- [x] Cantidad, orientación y altura de los sensores definidas y justificadas
- [x] Divisor de tensión especificado para `ECHO` — **crítico para no dañar la Pi**
- [x] Secuenciación de sensores y frecuencia de muestreo resultante (~16 Hz) definidas
- [x] LEDs, colores y resistencias calculados contra el presupuesto de corriente
- [x] Salida de audio decidida (jack + LM386) con la alternativa I2S descartada y por qué
- [x] Diagrama eléctrico del dominio lógico
- [x] Procedimientos de verificación
- [ ] Componentes conseguidos y montados — **pendiente**
- [ ] Calibración de sensores contra distancias conocidas — **pendiente**
- [ ] Secuenciación de sensores implementada en el software (issue #16)
