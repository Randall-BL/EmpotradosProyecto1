# Sistema de alimentación

Dos fuentes independientes, una por dominio: un **power bank USB** para la Raspberry Pi
y todo lo que trabaja a 5 V, y **dos baterías alcalinas de 9 V en paralelo** para el
puente H y los motores. Las dos fuentes no comparten ningún conductor.

---

## Por qué dos fuentes

La Raspberry Pi 4 requiere **5 V estables** y detecta caída de tensión (*undervoltage*)
por debajo de 4.63 V: reduce la frecuencia del SoC, corrompe la microSD y termina
reiniciándose. Alimentar la lógica y los motores del mismo punto mete el transitorio de
arranque de los motores —cinco a ocho veces la corriente nominal— directamente en el
riel de la Pi.

Con una fuente para cada dominio ese transitorio no tiene por dónde llegar: las tierras
`GND_LOG` y `GND_POT` quedan separadas desde la batería, no solo desde un regulador, y
las únicas señales que cruzan lo hacen por los optoacopladores (ver
[`hardware-aislamiento.md`](hardware-aislamiento.md)).

---

## Arquitectura

```
   ┌──────────────────┐                    ┌──────────────────────┐
   │  Power bank USB  │                    │ 2 × batería de 9 V   │
   │  5 V regulados   │                    │ alcalina, en paralelo│
   └────────┬─────────┘                    └──────────┬───────────┘
            │ USB-C                                   │ ~9 V
     DOMINIO LÓGICO                            DOMINIO DE POTENCIA
     5 V · GND_LOG                             9 V · GND_POT
            │                                         │
     ┌──────┴───────┐                          ┌──────┴───────┐
     │ Raspberry Pi │                          │ L298N  (VS)  │
     │      4       │                          │ regulador de │
     └──────┬───────┘                          │ 5 V interno  │
            │ pin de 5 V                       └──┬────────┬──┘
     ┌──────┴────────────┐                        │        │
     │ Servo del radar   │                 ┌──────┴───┐ ┌──┴───────────┐
     │ HC-SR04           │                 │ Motor izq│ │ +5V_POT:     │
     │ MPU-6050 (GY-521) │                 │ Motor der│ │ VSS y pull-up│
     │ PAM8403 + parlante│                 └──────────┘ │ de los optos │
     │ LEDs ×4 (GPIO)    │                              └──────────────┘
     └───────────────────┘

           ╳  GND_LOG y GND_POT NO se unen
              (ver docs/hardware-aislamiento.md)
```

---

## Dominio lógico: power bank

El power bank entrega 5 V ya regulados por su salida USB y trae de fábrica el circuito
de protección de su celda (sobrecarga, sobredescarga y cortocircuito). Por eso la
Raspberry Pi nunca queda conectada directamente a una celda de Li-Ion, que es lo que el
enunciado prohíbe, y el grupo no tuvo que armar ni ajustar un BMS ni un regulador.

La Raspberry Pi se alimenta por el **conector USB-C**, que pasa por la protección de
entrada de la tarjeta. Del **pin de 5 V** del conector de 40 pines salen el servo del
radar, el HC-SR04, el MPU-6050 y el amplificador con su parlante.

| Consumidor | Corriente típica | Pico |
|---|---|---|
| Raspberry Pi 4 (WiFi + CPU al 100 %) | 1.2 A | **2.5 A** |
| Servo del radar (SG90/MG90S) | 100–250 mA moviéndose | ~700 mA trabado |
| Amplificador PAM8403 + parlante de 8 Ω | 80 mA | 400 mA |
| HC-SR04 del radar | 15 mA | 20 mA |
| 4 × LED a 5 mA (nunca más de 3 encendidos) | 15 mA | 15 mA |
| 4 × LED de optoacoplador | 20 mA | 20 mA |
| MPU-6050 | 4 mA | 4 mA |
| **Total** | **~1.6 A** | **~3.7 A** |

El pico suma el peor caso de todo **a la vez**: la CPU al 100 %, el servo trabado y el
amplificador a volumen máximo sobre un golpe de bajo. En la práctica no coinciden.

El **servo es el consumidor delicado** de este dominio: es un motor, y sus picos y su
ruido entran al mismo riel de 5 V que alimenta la Raspberry Pi. Lo que lo mantiene
controlado:

- el barrido usa una rampa a un tercio de la velocidad del servo, así que no arranca de
  golpe en cada paso;
- conviene un capacitor de 470 µF en los bornes del servo, lo más cerca posible;
- si con música fuerte la Raspberry Pi marca subtensión, se baja el volumen máximo.

El MPU-6050 va en un módulo **GY-521**, que trae su propio regulador de 3.3 V y acepta
5 V en `VCC`; que el bus I2C siga en 3.3 V se comprueba midiendo `SDA` y `SCL` (ver
[`hardware-sensores.md`](hardware-sensores.md)).

---

## Dominio de potencia: dos baterías de 9 V

Dos baterías alcalinas de 9 V (las cuadradas, formato PP3) conectadas **en paralelo**:
la tensión sigue siendo de 9 V y la corriente se reparte entre las dos. Alimentan
directamente la entrada `VS` del L298N.

El L298N cae unos **2 V** entre `VS` y la salida por su topología de transistores
bipolares, así que los motores reciben cerca de **7 V**. Los motores son lentos y de alto
torque (60 rpm a 12 V), con rango de 6 a 12 V, y a esa tensión mueven el robot solo si la reciben
completa: por eso van a velocidad fija, sin PWM (ver
[`hardware-aislamiento.md`](hardware-aislamiento.md#velocidad-fija)).

`VSS` (los 5 V de la lógica interna del L298N) y los pull-up de los optoacopladores
salen del **regulador interno del módulo L298N**, con su jumper puesto, válido mientras
`VS ≤ 12 V`. **Nunca de los 5 V de la Raspberry Pi**: eso uniría las dos tierras y
anularía el aislamiento.

### Limitación: la autonomía

Una alcalina de 9 V tiene poca capacidad (del orden de 500 mAh) y una resistencia
interna alta: bajo la corriente de los motores su tensión cae y se agota pronto. Dos en
paralelo reparten la corriente y alargan la duración, pero siguen siendo la parte del
robot que primero se descarga. Para la demostración conviene llevar baterías de
repuesto (issue #34).

Al ser alcalinas no llevan BMS: no se recargan, y las dos deben ser **del mismo tipo y
estar igual de nuevas**, porque una gastada en paralelo con una nueva se descarga sobre
la otra.

---

## Desacople

| Dónde | Componente | Para qué |
|---|---|---|
| `VS` del L298N | 100 µF + 100 nF | Absorbe el pico de arranque de los motores |
| Bornes del servo | 470 µF / 10 V + 100 nF | Absorbe el pico de arranque en cada paso del barrido |
| El HC-SR04 | 100 nF | Ruido del pulso de disparo |
| `VDD` del PAM8403 | 100 µF + 100 nF | El clase D consume a pulsos |

Los electrolíticos van **con la polaridad correcta y lo más cerca posible del punto que
desacoplan**.

---

## Cableado

| Tramo | Calibre | Motivo |
|---|---|---|
| Baterías de 9 V → L298N → motores | **AWG 20** | Corriente de arranque |
| Power bank → Raspberry Pi | Cable USB-C corto y de buena calidad | La caída en el cable cuenta contra el margen de 4.63 V |
| Señales de sensores y LEDs | AWG 24 / Dupont | Corriente despreciable |

Los cables de motor van **trenzados entre sí** y separados de los cables de señal. Un
par trenzado cancela buena parte del campo magnético que de otro modo se acopla a las
líneas de `ECHO`.

---

## Lista de materiales

| Cantidad | Componente | Especificación |
|---|---|---|
| 1 | Power bank USB | Salida de 5 V, con su cable USB-C |
| 2 | Batería alcalina de 9 V | Del mismo tipo y estado, en paralelo |
| 2 | Broche para batería de 9 V | Con cables rojo y negro |
| 1 | Capacitor 470 µF / 10 V | Bornes del servo |
| 1 | Capacitor 100 µF / 25 V | `VS` del L298N |
| 2 | Capacitor 100 nF cerámico | Desacople |
| — | Cable AWG 20 | Rojo y negro |

---

## Verificación

### Paso 1 — Aislamiento entre dominios, todo desenergizado

| Entre | Esperado |
|---|---|
| `GND_LOG` y `GND_POT` | **Circuito abierto** |
| Pin de 5 V de la Raspberry Pi y `VS` del L298N | **Circuito abierto** |

### Paso 2 — Dominio de potencia solo

Con las baterías de 9 V conectadas y la Raspberry Pi apagada:

| Punto | Esperado |
|---|---|
| `VS` del L298N | Alrededor de 9 V con baterías nuevas |
| `+5V_POT` (salida de 5 V del L298N) | 5 V ± 0.25 V |
| Entradas `IN1`–`IN4` con los optos en reposo | ~5 V: los motores quedan frenados |

### Paso 3 — Ya con la Raspberry Pi encendida

Con el robot navegando y reproduciendo audio a la vez, que es el peor caso de consumo,
la Raspberry Pi no debe reiniciarse ni registrar subtensión:

```bash
dmesg | grep -i voltage        # no debe imprimir nada
```

---

## Estado

- [x] Topología definida: power bank para el dominio lógico y dos baterías de 9 V en paralelo para el de potencia
- [x] Servo, HC-SR04, MPU-6050 y amplificador en los 5 V de la Raspberry Pi
- [x] `VSS` y pull-up de los optos desde el regulador interno del L298N
- [x] Tierras separadas desde la fuente
- [x] Procedimiento de verificación
- [x] Robot armado y alimentado con esta topología
