# Mapa de pines GPIO

Referencia única del cableado. Cualquier cambio aquí **tiene que** reflejarse en el
código, y viceversa: los pines están escritos como `#define` en la biblioteca, y el
simulador (`sim/pigpio_sim.c`) los repite a propósito para que un cambio en un solo
lado se note.

Numeración **BCM** (la que usa pigpio), con el pin físico del conector de 40 pines.

---

## Tabla completa

| Función | BCM | Pin físico | Dirección | Definido en |
|---|---|---|---|---|
| **Motor izquierdo (A)** — vía optoacoplador; `ENA` con jumper en el L298N ||||
| `IN1` — PWM de avance | 5 | 29 | Salida | `lib/lib_motors.c` |
| `IN2` — PWM de retroceso | 6 | 31 | Salida | `lib/lib_motors.c` |
| **Motor derecho (B)** — vía optoacoplador; `ENB` con jumper en el L298N ||||
| `IN3` — PWM de avance | 23 | 16 | Salida | `lib/lib_motors.c` |
| `IN4` — PWM de retroceso | 24 | 18 | Salida | `lib/lib_motors.c` |
| **Radar: servo de 180°** — vía optoacoplador ||||
| Señal del servo (pulsos de 50 Hz) | 25 | 22 | Salida | `lib/lib_servo.h` |
| **Radar: HC-SR04 sobre el servo** ||||
| `TRIG` | 17 | 11 | Salida | `lib/lib_radar.h` |
| `ECHO` (con divisor 1k/2k) | 27 | 13 | Entrada | `lib/lib_radar.h` |
| **MPU-6050 (I2C-1)** ||||
| `SDA` | 2 | 3 | Bidireccional | `lib/lib_imu.h` (bus 1) |
| `SCL` | 3 | 5 | Salida | `lib/lib_imu.h` (bus 1) |
| `VCC` — **3.3 V**, no 5 V | 3V3 | 1 | — | — |
| **LEDs indicadores** ||||
| Sistema encendido | 16 | 36 | Salida | `lib/lib_leds.h` |
| Modo autónomo | 20 | 38 | Salida | `lib/lib_leds.h` |
| Modo manual | 21 | 40 | Salida | `lib/lib_leds.h` |
| Obstáculo detectado | 26 | 37 | Salida | `lib/lib_leds.h` |
| **Audio** ||||
| PWM de audio → filtro RC → PAM8403 | 18 | 12 | Salida | `dtoverlay=audremap,pins_18_19` (`rpi-config_%.bbappend`) |
| Segundo canal PWM (misma señal, sin conectar) | 19 | 35 | Salida | ídem |

---

## Diagrama del conector

Solo los pines usados. `·` = pin libre.

```
      MPU VCC ─ 3V3  ( 1) ( 2)  5V
      MPU SDA ─(GPIO2)( 3) ( 4)  5V
      MPU SCL ─(GPIO3)( 5) ( 6)  GND ────── GND lógica
            ·  GPIO4 ( 7) ( 8)  GPIO14  (UART TX, consola)
      MPU GND ─  GND ( 9) (10)  GPIO15  (UART RX, consola)
  TRIG radar ─(GPIO17)(11) (12)(GPIO18)─ audio PWM → PAM8403
  ECHO radar ─(GPIO27)(13) (14)  GND
            · GPIO22 (15) (16)(GPIO23)─ IN3 motor der.
                 3V3 (17) (18)(GPIO24)─ IN4 motor der.
            · GPIO10 (19) (20)  GND
            ·  GPIO9 (21) (22)(GPIO25)─ servo del radar
            · GPIO11 (23) (24)  GPIO8   ·
                 GND (25) (26)  GPIO7   ·
               ID_SD (27) (28)  ID_SC
 IN1 motor iz─(GPIO5)(29) (30)  GND
 IN2 motor iz─(GPIO6)(31) (32)  GPIO12  ·
            · GPIO13 (33) (34)  GND
2º canal audio (libre)(GPIO19)(35) (36)(GPIO16)─ LED encendido
LED obstáculo─(GPIO26)(37) (38)(GPIO20)─ LED autónomo
                 GND (39) (40)(GPIO21)─ LED manual
```

---

## Por qué estos pines

**La PWM de los motores va sobre `IN1`–`IN4` (5, 6, 23 y 24).** El L298N lleva los
jumpers `ENA`/`ENB` puestos —siempre habilitado— y la velocidad se da con PWM
directamente sobre las entradas de sentido (ver
[`hardware-aislamiento.md`](hardware-aislamiento.md)). La genera `pigpiod` por software
temporizado con DMA con `set_PWM_dutycycle()`, que funciona en cualquier GPIO: por eso
las entradas no necesitan estar en los pines de PWM por hardware, y son los mismos pines
de sentido de antes. `GPIO 12` y `13`, que llevaban `ENA`/`ENB`, quedan libres.

**GPIO 18 para el audio.** La salida PWM analógica que normalmente va al jack de 3.5 mm
se saca al conector con el overlay `audremap`, que acepta los pares 12/13 o 18/19. Se
eligió 18/19 cuando 12/13 eran el PWM de los motores, y se mantiene: ya está en la imagen
y en el cableado, y cambiarlo no aporta nada. Solo se cablea GPIO 18: la biblioteca mezcla
la música a mono y los dos canales llevan la misma señal (ver
[`hardware-sensores.md`](hardware-sensores.md)).

**GPIO 2 y 3 para el MPU-6050.** Son el bus I2C-1 del SoC (`/dev/i2c-1`) y traen
resistencias pull-up de 1.8 kΩ a 3.3 V en la propia placa. Estaban reservados
justamente "por si se agrega un sensor adicional".

**GPIO 25 para el servo.** Queda en el pin 22, al lado de `IN3`/`IN4` (16 y 18): las
señales que cruzan al dominio de potencia quedan cerca y van juntas a la placa de
optoacopladores.

**GPIO 17 y 27 para el HC-SR04 del radar.** Son los del antiguo sensor frontal: el
cable al borde delantero del chasis no cambia. Los pines de los dos sensores laterales
que ya no existen (22, 10, 9 y 11) quedan libres.

**LEDs en 16, 20, 21 y 26.** Bloque contiguo al final del conector, lejos de las señales
de motor. Reduce el acople de ruido de conmutación del PWM hacia las señales lógicas.

---

## Restricciones eléctricas

| Límite | Valor | Consecuencia |
|---|---|---|
| Corriente por GPIO | 16 mA | Nunca conectar un LED sin resistencia limitadora |
| Corriente total de todos los GPIO | 50 mA | Ver el presupuesto de abajo |
| Tensión de entrada de un GPIO | **3.3 V máximo** | El `ECHO` del HC-SR04 saca 5 V: **va con divisor obligatorio**. El MPU-6050 se alimenta a 3.3 V para que sus pull-up no lleven 5 V a SDA/SCL — ver [`hardware-sensores.md`](hardware-sensores.md) |
| Tensión de salida de un GPIO | 3.3 V | Los optoacopladores y el filtro de audio se dimensionan para esa tensión |

### Presupuesto de corriente del conector

Lo que los GPIO entregan desde su propio riel de 3.3 V, en el peor caso **simultáneo**:

| Consumidor | Peor caso | Por qué |
|---|---|---|
| 4 optoacopladores de motor a 5 mA | 20 mA | Con la inversión del opto compensada, el robot **detenido** (las cuatro entradas del L298N en bajo) es el caso con los cuatro LED internos encendidos |
| Optoacoplador del servo a 3.1 mA | 3.1 mA | Solo durante el pulso: ≤ 2.5 ms cada 20 ms, 0.4 mA de promedio |
| LEDs indicadores a 5 mA | 15 mA | Como mucho **tres** encendidos: autónomo y manual son excluyentes |
| Filtro RC del audio | 0.6 mA | 3.3 V sobre 4.7 kΩ + 1 kΩ |
| `TRIG` del HC-SR04 | ≈ 0 | Entrada de alta impedancia del sensor |
| SDA/SCL | 0 | Colector abierto: el GPIO solo drena la corriente del pull-up |
| **Total** | **≈ 39 mA** | Dentro de los 50 mA, con 11 mA de margen |

Sin `ENA`/`ENB` hay dos optoacopladores menos que antes. Y autónomo y manual nunca están
encendidos juntos, así que los LEDs suman tres, no cuatro.
El `VCC` del MPU-6050 (unos 4 mA) sale del pin de 3.3 V, no de un GPIO, y no entra en
esta cuenta.

> Meter 5 V a un GPIO destruye el pin, y a veces el SoC completo. No hay protección
> interna. Es el error que más Raspberry Pi mata en este tipo de proyecto.

---

## Pines deliberadamente libres

| Pin | Motivo |
|---|---|
| GPIO 14, 15 (UART) | Consola serie de depuración. `ENABLE_UART = "1"` en `local.conf` |
| GPIO 12, 13 | Liberados al dejar `ENA`/`ENB` con jumper. Son un par de PWM por hardware: sirven para un segundo servo o para pasar el audio a `pins_12_13` |
| GPIO 19 | Toma la función PWM de audio con `audremap`; no se conecta, pero tampoco se reutiliza |
| GPIO 22, 10, 9, 11 | Liberados por los sensores laterales. Si el profesor exige dos sensores físicos, el segundo HC-SR04 va en 22 (`TRIG`) y 10 (`ECHO`) |
| GPIO 4, 7, 8 | Margen para los requerimientos opcionales (sensores de desnivel) |

---

## Verificación del cableado

Antes de energizar, con la Raspberry Pi **apagada y desconectada**, verificar con el
multímetro en modo continuidad que cada pin del conector llega a donde dice la tabla.
Un cable cruzado entre una señal de motor y una entrada de sensor puede meter 5 V a un
GPIO configurado como salida.

Con el sistema encendido, el propio servidor confirma lo que encontró:

```bash
journalctl -u robot-server | grep -E "imu|radar|leds"
#   [imu] MPU-6050 listo en /dev/i2c-1, direccion 0x68
#   [imu] calibrado con 100 muestras: sesgo ...
#   [radar] barriendo de 0 a 180 grados en pasos de 30
ls /dev/i2c-1                    # el bus existe: dtparam=i2c_arm=on + i2c-dev
```

Para probar un pin a mano hace falta `pigs`, que **no** va en la imagen de entrega. En
desarrollo se agrega con `IMAGE_INSTALL:append = " pigpio-bin-pigs"` en `local.conf`:

```bash
pigs modes 16 w ; pigs w 16 1 ; sleep 1 ; pigs w 16 0   # un LED
pigs s 25 1500 ; sleep 1 ; pigs s 25 0                   # servo al frente y suelto
pigs i2co 1 0x68 0                                       # abre el MPU: devuelve un handle h
pigs i2crb 0 0x75                                        # WHO_AM_I con h = 0: 104 (0x68)
pigs i2cc 0                                              # cierra el handle
```
