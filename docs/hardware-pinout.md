# Mapa de pines GPIO

Referencia única del cableado. Cualquier cambio aquí **tiene que** reflejarse en el
código, y viceversa: los pines están escritos como `#define` en la biblioteca.

Numeración **BCM** (la que usa pigpio), con el pin físico del conector de 40 pines.

---

## Tabla completa

| Función | BCM | Pin físico | Dirección | Definido en |
|---|---|---|---|---|
| **Motor izquierdo (A)** ||||
| `ENA` — PWM velocidad | 12 | 32 | Salida | `lib/lib_motors.c` |
| `IN1` — sentido | 5 | 29 | Salida | `lib/lib_motors.c` |
| `IN2` — sentido | 6 | 31 | Salida | `lib/lib_motors.c` |
| **Motor derecho (B)** ||||
| `ENB` — PWM velocidad | 13 | 33 | Salida | `lib/lib_motors.c` |
| `IN3` — sentido | 23 | 16 | Salida | `lib/lib_motors.c` |
| `IN4` — sentido | 24 | 18 | Salida | `lib/lib_motors.c` |
| **Sensor frontal HC-SR04** ||||
| `TRIG` | 17 | 11 | Salida | `server/src/robot_hardware.c` |
| `ECHO` | 27 | 13 | Entrada | `server/src/robot_hardware.c` |
| **Sensor lateral izquierdo HC-SR04** ||||
| `TRIG` | 22 | 15 | Salida | `server/src/robot_hardware.c` |
| `ECHO` | 10 | 19 | Entrada | `server/src/robot_hardware.c` |
| **Sensor lateral derecho HC-SR04** ||||
| `TRIG` | 9 | 21 | Salida | `server/src/robot_hardware.c` |
| `ECHO` | 11 | 23 | Entrada | `server/src/robot_hardware.c` |
| **LEDs indicadores** ||||
| Sistema encendido | 16 | 36 | Salida | `lib/lib_leds.h` |
| Modo autónomo | 20 | 38 | Salida | `lib/lib_leds.h` |
| Modo manual | 21 | 40 | Salida | `lib/lib_leds.h` |
| Obstáculo detectado | 26 | 37 | Salida | `lib/lib_leds.h` |
| **Audio** ||||
| Salida analógica | — | jack 3.5 mm | Salida | — |

---

## Diagrama del conector

Solo los pines usados. `·` = pin libre.

```
          3V3  ( 1) ( 2)  5V
        GPIO2  ( 3) ( 4)  5V
        GPIO3  ( 5) ( 6)  GND ────── GND lógica
        GPIO4  ( 7) ( 8)  ·
          GND  ( 9) (10)  ·
 TRIG frontal ─(11) (12)  ·                  GPIO17
 ECHO frontal ─(13) (14)  GND                GPIO27
 TRIG lat.izq ─(15) (16) ─ IN3 motor der.    GPIO22 / GPIO23
          3V3  (17) (18) ─ IN4 motor der.    GPIO24
 ECHO lat.izq ─(19) (20)  GND                GPIO10
 TRIG lat.der ─(21) (22)  ·                  GPIO9
 ECHO lat.der ─(23) (24)  ·                  GPIO11
          GND  (25) (26)  ·
        ID_SD  (27) (28)  ID_SC
  IN1 motor iz─(29) (30)  GND                GPIO5
  IN2 motor iz─(31) (32) ─ ENA PWM motor iz  GPIO6 / GPIO12
  ENB PWM m.der(33) (34)  GND                GPIO13
            ·  (35) (36) ─ LED encendido     GPIO16
LED obstáculo─(37) (38) ─ LED autónomo       GPIO26 / GPIO20
          GND  (39) (40) ─ LED manual        GPIO21
```

---

## Por qué estos pines

**GPIO 12 y 13 para PWM.** Son los pines de los canales PWM0 y PWM1 del SoC. El código
actual usa `set_PWM_dutycycle()` de pigpio, que es PWM por software temporizado con DMA
—estable, pero no tan preciso como el periférico. Estando en 12 y 13, migrar a
`hardware_PWM()` no requiere recablear: es un cambio de una línea si más adelante se
necesita mejor resolución de velocidad.

**GPIO 9, 10 y 11 para los sensores laterales.** Son los pines de SPI0. El robot no usa
SPI, así que están libres, y quedan juntos en el conector, lo que simplifica el ruteo
del cable plano hacia los sensores.

**LEDs en 16, 20, 21 y 26.** Bloque contiguo al final del conector, lejos de las señales
de motor. Reduce el acople de ruido de conmutación del PWM hacia las señales lógicas.

---

## Restricciones eléctricas

| Límite | Valor | Consecuencia |
|---|---|---|
| Corriente por GPIO | 16 mA | Nunca conectar un LED sin resistencia limitadora |
| Corriente total de todos los GPIO | 50 mA | Con 6 optoacopladores a 5 mA quedan 30 mA: es el presupuesto dominante |
| Tensión de entrada de un GPIO | **3.3 V máximo** | El `ECHO` del HC-SR04 saca 5 V. **Va con divisor obligatorio** — ver [`hardware-sensores.md`](hardware-sensores.md) |
| Tensión de salida de un GPIO | 3.3 V | Los optoacopladores y el L298N se dimensionan para esa tensión |

> Meter 5 V a un GPIO destruye el pin, y a veces el SoC completo. No hay protección
> interna. Es el error que más Raspberry Pi mata en este tipo de proyecto.

---

## Pines deliberadamente libres

| Pin | Motivo |
|---|---|
| GPIO 14, 15 (UART) | Consola serie de depuración. `ENABLE_UART = "1"` en `local.conf` |
| GPIO 2, 3 (I2C) | Reservados por si se agrega un DAC I2C o un sensor adicional |
| GPIO 18, 19 | Alternativa de PWM por hardware si 12/13 dan problema |
| GPIO 7, 8, 25 | Margen para los requerimientos opcionales (sensores de desnivel) |

---

## Verificación del cableado

Antes de energizar, con la Raspberry Pi **apagada y desconectada**, verificar con el
multímetro en modo continuidad que cada pin del conector llega a donde dice la tabla.
Un cable cruzado entre una señal de motor y una entrada de sensor puede meter 5 V a un
GPIO configurado como salida.

Con el sistema encendido y el software corriendo:

```bash
# Estado de todos los pines: modo y nivel
pigs mode 12 r ; pigs read 12

# Encender un LED a mano para confirmar el cableado
pigs modes 16 w ; pigs w 16 1 ; sleep 1 ; pigs w 16 0

# Disparar el sensor frontal y medir el eco
pigs modes 17 w ; pigs modes 27 r
```
