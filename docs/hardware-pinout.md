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
| **Motor derecho (salida A del L298N)** — vía optoacoplador; `ENA` con jumper ||||
| `IN1` — avance | 5 | 29 | Salida | `lib/lib_motors.c` |
| `IN2` — retroceso | 6 | 31 | Salida | `lib/lib_motors.c` |
| **Motor izquierdo (salida B del L298N)** — vía optoacoplador; `ENB` con jumper ||||
| `IN3` — avance | 23 | 16 | Salida | `lib/lib_motors.c` |
| `IN4` — retroceso | 24 | 18 | Salida | `lib/lib_motors.c` |
| **Radar: servo de 180°** — señal directa, alimentado de los 5 V de la Pi ||||
| Señal del servo (pulsos de 50 Hz) | 25 | 22 | Salida | `lib/lib_servo.h` |
| **Radar: HC-SR04 sobre el servo** ||||
| `TRIG` | 17 | 11 | Salida | `lib/lib_radar.h` |
| `ECHO` (con divisor 1k/2k) | 27 | 13 | Entrada | `lib/lib_radar.h` |
| **MPU-6050 (I2C-1)** ||||
| `SDA` | 2 | 3 | Bidireccional | `lib/lib_imu.h` (bus 1) |
| `SCL` | 3 | 5 | Salida | `lib/lib_imu.h` (bus 1) |
| `VCC` — 5 V (el GY-521 trae regulador de 3.3 V) | 5V | 2 | — | — |
| **Sensores IR de desnivel (opcional)** — módulos a 3.3 V, entradas con pull-down ||||
| Esquina delantera izquierda | 4 | 7 | Entrada | `lib/lib_caida.h` |
| Esquina delantera derecha | 8 | 24 | Entrada | `lib/lib_caida.h` |
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
                3V3  ( 1) ( 2)  5V ─── MPU VCC, servo, HC-SR04, PAM8403
      MPU SDA ─(GPIO2)( 3) ( 4)  5V
      MPU SCL ─(GPIO3)( 5) ( 6)  GND ────── GND lógica
     IR izq. ─(GPIO4)( 7) ( 8)  GPIO14  (UART TX, consola)
      MPU GND ─  GND ( 9) (10)  GPIO15  (UART RX, consola)
  TRIG radar ─(GPIO17)(11) (12)(GPIO18)─ audio PWM → PAM8403
  ECHO radar ─(GPIO27)(13) (14)  GND
            · GPIO22 (15) (16)(GPIO23)─ IN3 motor izq.
                 3V3 (17) (18)(GPIO24)─ IN4 motor izq.
            · GPIO10 (19) (20)  GND
            ·  GPIO9 (21) (22)(GPIO25)─ servo del radar
            · GPIO11 (23) (24)(GPIO8)─ IR der.
                 GND (25) (26)  GPIO7   ·
               ID_SD (27) (28)  ID_SC
 IN1 motor de─(GPIO5)(29) (30)  GND
 IN2 motor de─(GPIO6)(31) (32)  GPIO12  ·
            · GPIO13 (33) (34)  GND
2º canal audio (libre)(GPIO19)(35) (36)(GPIO16)─ LED encendido
LED obstáculo─(GPIO26)(37) (38)(GPIO20)─ LED autónomo
                 GND (39) (40)(GPIO21)─ LED manual
```

---

## Por qué estos pines

**Los motores se controlan solo con `IN1`–`IN4` (5, 6, 23 y 24).** El L298N lleva los
jumpers `ENA`/`ENB` puestos —siempre habilitado— y las entradas de sentido llevan
niveles fijos: los motores van siempre al máximo, sin PWM, por una desviación acordada
con el profesor (ver [`hardware-aislamiento.md`](hardware-aislamiento.md#velocidad-fija)).
El modo con PWM de la biblioteca usa esas mismas entradas, sin recablear. `GPIO 12` y
`13`, que llevaban `ENA`/`ENB`, quedan libres.

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
señales de control quedan cerca. La del servo va directo al servo; las de los motores,
a la placa de optoacopladores.

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
| Tensión de entrada de un GPIO | **3.3 V máximo** | El `ECHO` del HC-SR04 saca 5 V: **va con divisor obligatorio**. El MPU-6050 se alimenta a 5 V por el regulador de su módulo; SDA/SCL deben medirse en 3.3 V — ver [`hardware-sensores.md`](hardware-sensores.md) |
| Tensión de salida de un GPIO | 3.3 V | Los optoacopladores y el filtro de audio se dimensionan para esa tensión |

### Presupuesto de corriente del conector

Lo que los GPIO entregan desde su propio riel de 3.3 V, en el peor caso **simultáneo**:

| Consumidor | Peor caso | Por qué |
|---|---|---|
| 4 optoacopladores de motor a 5 mA | 20 mA | Con la inversión del opto compensada, el robot **detenido** (las cuatro entradas del L298N en bajo) es el caso con los cuatro LED internos encendidos |
| Señal del servo | < 1 mA | Entrada de alta impedancia; solo durante el pulso |
| LEDs indicadores a 5 mA | 15 mA | Como mucho **tres** encendidos: autónomo y manual son excluyentes |
| Filtro RC del audio | 0.6 mA | 3.3 V sobre 4.7 kΩ + 1 kΩ |
| `TRIG` del HC-SR04 | ≈ 0 | Entrada de alta impedancia del sensor |
| SDA/SCL | 0 | Colector abierto: el GPIO solo drena la corriente del pull-up |
| **Total** | **≈ 37 mA** | Dentro de los 50 mA, con 13 mA de margen |

Sin `ENA`/`ENB` hay dos optoacopladores menos que antes. Y autónomo y manual nunca están
encendidos juntos, así que los LEDs suman tres, no cuatro.
El `VCC` del MPU-6050, del servo, del HC-SR04 y del amplificador sale del pin de 5 V, no
de un GPIO, y no entra en esta cuenta.

> Meter 5 V a un GPIO destruye el pin, y a veces el SoC completo. No hay protección
> interna. Es el error que más Raspberry Pi mata en este tipo de proyecto.

---

## Pines deliberadamente libres

| Pin | Motivo |
|---|---|
| GPIO 14, 15 (UART) | Consola serie de depuración. `ENABLE_UART = "1"` en `local.conf` |
| GPIO 12, 13 | Liberados al dejar `ENA`/`ENB` con jumper. Son un par de PWM por hardware: sirven para un segundo servo o para pasar el audio a `pins_12_13` |
| GPIO 19 | Toma la función PWM de audio con `audremap`; no se conecta, pero tampoco se reutiliza |
| GPIO 22, 10, 9, 11 | Liberados por los sensores laterales. El profesor aceptó el radar junto con el MPU-6050, así que no se montó un segundo HC-SR04; habría ido en 22 (`TRIG`) y 10 (`ECHO`) |
| GPIO 7 | Margen; 4 y 8 llevan los sensores de desnivel. SPI no está habilitado, así que 7 y 8 son GPIO comunes |

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
