# Etapa de potencia con aislamiento galvánico

> **Lectura obligatoria antes de energizar nada.** Conectar un GPIO directamente a la
> circuitería de motores puede destruir la Raspberry Pi de forma irreversible. El equipo
> es prestado por el curso y el daño es responsabilidad del grupo.

---

## Por qué hace falta

Un motor DC es una carga inductiva. Al conmutar o al frenar genera picos de tensión
inversa de decenas de voltios sobre la línea de alimentación y sobre la referencia de
tierra. Si la tierra de los motores y la tierra de la Raspberry Pi son el mismo cobre,
ese pico aparece en el conector de 40 pines.

Tres cosas lo hacen peor en este robot:

1. **Corriente de arranque.** Un motor DC pequeño arrancando consume entre 5 y 8 veces
   su corriente nominal durante decenas de milisegundos.
2. **PWM a 1 kHz.** Se conmuta la corriente del motor mil veces por segundo. Cada flanco
   es un transitorio.
3. **Inversión de sentido.** Pasar de avance a retroceso invierte la corriente en un
   inductor cargado: es el peor caso.

El GPIO de la Raspberry Pi **no tiene diodos de protección hacia 3.3 V** y tolera
3.3 V como máximo absoluto. No hay margen.

---

## Solución adoptada: PC817 en las cinco señales

El L298N se usa con los **jumpers `ENA` y `ENB` puestos**: el puente queda siempre
habilitado y esos pines no salen a la Raspberry Pi. La velocidad se controla con **PWM
directamente sobre las entradas de sentido** `IN1`–`IN4`, así que hacia el dominio de
potencia cruzan cuatro señales, las cuatro con PWM. La quinta es la del **servo del
radar**: es un motor, se alimenta del riel de potencia y su señal cruza la barrera igual,
pero con un canal armado distinto (ver [más abajo](#el-canal-del-servo-no-invierte)).

```
     DOMINIO LÓGICO (3.3 V)          │        DOMINIO DE POTENCIA (7.4 V)
     GND_LOG                         │        GND_POT
                                     │
  GPIO ──[R 390Ω]──┐             ┌───┼──── +5V_POT
                   │             │   │        │
                 ┌─┴─┐           │   │      [R 4.7kΩ]
                 │ ▼ │ LED       │   │        │
   PC817         │   │           │   │        ├──────► entrada del L298N
                 │ ⊂ │ fototrans.│   │        │
                 └─┬─┘           └───┼────────┘ colector
                   │      emisor ────┼──── GND_POT
                GND_LOG              │
                                     │
                        ↑ AISLAMIENTO GALVÁNICO
                          sin cobre en común
```

Se repite cuatro veces, una por entrada: `IN1`, `IN2`, `IN3`, `IN4`.

### Cómo mueve los motores el L298N sin `ENA`/`ENB`

Con el puente siempre habilitado, cada par de entradas decide el estado del motor:

| `IN1` | `IN2` | Motor A |
|---|---|---|
| Alto | Bajo | Avanza |
| Bajo | Alto | Retrocede |
| Bajo | Bajo | **Frenado** (las dos salidas a tierra: el motor queda en cortocircuito) |
| Alto | Alto | **Frenado** (las dos salidas al positivo) |

Para avanzar a una velocidad dada, `IN1` lleva la PWM e `IN2` queda en bajo: el motor
alterna entre avanzar y frenar, y la velocidad sigue al ciclo de trabajo de forma casi
lineal. Para retroceder, al revés. Detenerse es dejar las dos en bajo: **frena en seco**,
no queda girando libre. Igual para `IN3`/`IN4` y el motor B.

Los pines `GPIO 12` y `13`, que antes llevaban `ENA`/`ENB`, quedan libres.

### Cálculo de la resistencia de entrada

El LED interno del PC817 tiene `Vf ≈ 1.2 V`:

```
R = (3.3 V − 1.2 V) / If
```

| `If` | R calculada | R comercial | Corriente total (×4) |
|---|---|---|---|
| 5 mA | 420 Ω | **390 Ω** | 20 mA |
| 8 mA | 262 Ω | 270 Ω | 32 mA |
| 10 mA | 210 Ω | 220 Ω | 40 mA ⚠️ |

**Se usa 390 Ω.** El límite de corriente sumada de todos los GPIO de la Raspberry Pi es
de 50 mA, y a eso hay que restarle todavía los LEDs indicadores y el canal del servo.
Con 5 mA por canal los cuatro optos suman 20 mA y queda holgura para los LEDs, de los que
nunca hay más de tres encendidos (15 mA), y el canal del servo (3.1 mA). La cuenta
completa está en [`hardware-pinout.md`](hardware-pinout.md).

El PC817 con `CTR` mínimo de 50 % da 2.5 mA de colector con `If = 5 mA`. Contra la
resistencia de 4.7 kΩ a 5 V eso satura el transistor de sobra: hacen falta apenas
1.1 mA para llevar la salida a nivel bajo.

### Resistencia de salida

`4.7 kΩ` de colector a `+5V_POT`. Con `Cpar ≈ 10 pF` de la entrada del L298N la
constante de tiempo es de unos 50 ns, despreciable frente al kilohercio del PWM. Lo que
sí se nota es el apagado del propio PC817, de unas decenas de microsegundos: deforma los
ciclos de trabajo muy chicos o muy grandes, que el robot de todas formas no usa (por
debajo de ~60/255 el motor no vence la fricción).

Bajarla a 1 kΩ acelera el flanco pero consume 5 mA por canal del riel de potencia. No
hace falta a 1 kHz.

---

## ⚠️ El optoacoplador invierte la señal

En configuración de emisor común —la del diagrama— el fototransistor **conduce** cuando
el LED está encendido, y al conducir lleva la salida a **nivel bajo**:

| GPIO | LED del opto | Fototransistor | Entrada del L298N |
|---|---|---|---|
| Alto (3.3 V) | Encendido | Conduce | **Bajo** |
| Bajo (0 V) | Apagado | Corte | **Alto** (por el pull-up) |

Con la PWM sobre las entradas, pedir un ciclo *d* en el GPIO entrega 255 − *d* en el
L298N. Sin compensarlo, la entrada que debía quedar en bajo queda siempre en alto, y
cada motor **gira al revés**: `motores_avanzar()` haría retroceder el robot y el giro a
la derecha sería a la izquierda.

### Cómo se compensa

**En software, en `lib/lib_motors.c`**, el único punto donde la biblioteca toca los
motores:

```c
#define OPTO_INVERTIDO 1

/* Deja en la ENTRADA DEL L298N un ciclo de trabajo de 0 (siempre en bajo) a
   MOTOR_PWM_MAX (siempre en alto), compensando el optoacoplador. */
static void entrada_l298n(unsigned gpio, int duty) {
#if OPTO_INVERTIDO
    duty = MOTOR_PWM_MAX - duty;
#endif
    set_PWM_dutycycle(g_pi, gpio, (unsigned)duty);
}
```

Todas las escrituras a `IN1`–`IN4` pasan por esa función. Para una prueba de banco con el
L298N conectado **sin** optoacopladores, `OPTO_INVERTIDO` va en 0; con la placa de
optos, siempre en 1. El simulador (`sim/pigpio_sim.c`) invierte la señal como la placa
real, así que si alguien quita la compensación la prueba de avance falla.

La alternativa —una segunda inversión en hardware, con un 74HC04 o un transistor por
canal— son cuatro componentes más y más puntos de falla para resolver lo que resuelve una
resta.

### El reposo es seguro

Durante el arranque, antes de que `pigpiod` configure los pines, o si el servidor se
detiene, los GPIO quedan en bajo: los LED de los optos apagados y las cuatro entradas del
L298N en alto por los pull-up. Con el puente habilitado eso es **freno**, no movimiento.

---

## El canal del servo no invierte

Para el servo, en cambio, la inversión no se puede compensar en software sin un riesgo
que los motores no tienen. `pigpiod` genera los pulsos del servo por DMA
(`set_servo_pulsewidth`), y lo que no se puede invertir es el **reposo**: durante el
arranque, antes de que `pigpiod` configure el pin, o si el servidor se detiene, el GPIO
queda en bajo. Con un canal de emisor común eso es un nivel alto **permanente** en la
entrada del servo, que muchos servos interpretan como un pulso larguísimo y los lleva
contra el tope mecánico.

Por eso el quinto canal se arma en **seguidor de emisor**: el colector va a `+5V_POT`
y la salida se toma del emisor, con una resistencia a `GND_POT`.

```
     DOMINIO LÓGICO (3.3 V)          │        DOMINIO DE POTENCIA
     GND_LOG                         │        GND_POT
                                     │
  GPIO 25 ─[R 680Ω]─┐            ┌───┼──── +5V_POT
                    │            │   │
                  ┌─┴─┐          │   │      colector
                  │ ▼ │ LED      │   │
   PC817          │   │          │   │
                  │ ⊂ │          └───┼──┐
                  └─┬─┘              │  │  emisor
                    │                │  ├─────────► señal del servo
                 GND_LOG             │  │
                                     │ [R 4.7kΩ]
                                     │  │
                                     │ GND_POT
```

| GPIO 25 | LED del opto | Fototransistor | Señal del servo |
|---|---|---|---|
| Alto (3.3 V) | Encendido | Conduce | **Alto** (~4.8 V) |
| Bajo (0 V) | Apagado | Corte | **Bajo** (por la resistencia a tierra) |

Sin inversión: el pulso que manda `pigpiod` es el que recibe el servo, y en reposo no
recibe nada.

### Cálculo

- **Salida:** la entrada del servo es de alta impedancia; la corriente la fija la
  resistencia de emisor: 4.8 V / 4.7 kΩ ≈ 1 mA.
- **Entrada:** con CTR mínimo de 50 % hacen falta 2 mA en el LED para sostener ese
  miliamperio. `R = (3.3 V − 1.2 V) / 3 mA = 700 Ω` → **680 Ω, 3.1 mA**. Circula solo
  durante el pulso (≤ 2.5 ms cada 20 ms): 0.4 mA de promedio sobre el presupuesto del
  conector.
- **Tiempos:** el PC817 tarda unas decenas de microsegundos en apagarse, así que el
  pulso llega al servo algo más largo de lo que se mandó. Es un corrimiento fijo de
  pocos grados y se absorbe en la calibración de `SERVO_PULSO_0_US` /
  `SERVO_PULSO_180_US` (ver [`odometria.md`](odometria.md)).

El servo se alimenta de su propio regulador de 5 V en el riel de potencia (ver
[`hardware-alimentacion.md`](hardware-alimentacion.md)); su tierra es `GND_POT`, la
misma del emisor.

---

## Separación de tierras

Es la mitad del asunto y la que más se equivoca.

```
     ┌──────────────── DOMINIO LÓGICO ────────────────┐
     │  Raspberry Pi 4                                │
     │  HC-SR04 del radar                             │
     │  MPU-6050                                      │
     │  LEDs indicadores                              │
     │  Amplificador de audio PAM8403 + parlante      │
     │  Lado LED de los 5 optoacopladores             │
     │                                                │
     │  Todos referidos a GND_LOG                     │
     └────────────────────────────────────────────────┘
                          ╳  SIN CONEXIÓN
     ┌─────────────── DOMINIO DE POTENCIA ────────────┐
     │  Driver L298N (VS, VSS y jumpers ENA/ENB)      │
     │  2 motores DC                                  │
     │  Servo del radar + su regulador de 5 V         │
     │  Lado fototransistor de los 5 optoacopladores  │
     │                                                │
     │  Todos referidos a GND_POT                     │
     └────────────────────────────────────────────────┘
```

**`GND_LOG` y `GND_POT` no se tocan en ningún punto.** Ni con un cable, ni por el chasis,
ni por la carcasa de un conector metálico. Si se unen, el aislamiento deja de existir y
los optoacopladores no sirven de nada.

### El error más común

El L298N tiene un pin `+5V` (`VSS`) para su propia lógica interna. **Ese pin NO se
conecta a los 5 V de la Raspberry Pi.** Si se conecta, la tierra de referencia de la
lógica del L298N pasa a ser la de la Pi y el aislamiento queda anulado, aunque los seis
optoacopladores estén perfectamente montados.

`VSS` se alimenta desde el regulador del **riel de potencia**, o desde el regulador
interno del propio módulo L298N si trae el jumper de 5 V puesto y `VS ≤ 12 V`.

---

## Alternativa: driver con aislamiento integrado

El DRV8871 acepta entrada de 3.3 V directa, pero **no aísla galvánicamente**: comparte
tierra. Sirve para simplificar el nivel lógico, no para reemplazar los optoacopladores.

Si se prefiere un componente en vez de cuatro, un **ADuM1401** (aislador digital de
cuatro canales) cubre las cuatro entradas del L298N con aislamiento real, y es lo
bastante rápido para la PWM. Es más caro y más difícil de conseguir localmente que
cuatro PC817.

**Se mantiene la solución con PC817.**

> El HC-SR04 va montado **sobre** el servo pero pertenece al dominio lógico: su
> alimentación y sus señales llegan por su propio cable desde la Raspberry Pi. El brazo
> del servo es de plástico y no une las dos tierras; el cable del sensor no debe
> compartir conector con el del servo.

---

## Lista de materiales

| Cantidad | Componente | Notas |
|---|---|---|
| 5 | Optoacoplador PC817 (o 4N25) | Cuatro para `IN1`–`IN4` y uno para el servo |
| 4 | Resistencia 390 Ω, 1/4 W | Limitación del LED, canales del L298N |
| 1 | Resistencia 680 Ω, 1/4 W | Limitación del LED, canal del servo |
| 5 | Resistencia 4.7 kΩ, 1/4 W | Cuatro pull-up de colector (L298N) y una a tierra de emisor (servo) |
| 1 | Driver L298N (módulo) | Doble puente H, **con los jumpers `ENA` y `ENB` puestos** |
| 4 | Diodo 1N5822 (Schottky) | Volante, si el módulo no los trae |
| 1 | Capacitor 100 µF electrolítico | Desacople en `VS` del L298N |
| 2 | Capacitor 100 nF cerámico | Desacople en `VSS` y en el L298N |

> Muchos módulos L298N ya traen los diodos volantes. Verificar en la placa antes de
> agregarlos.

---

## Procedimiento de verificación — **antes de conectar la Raspberry Pi**

Se hace en este orden. No saltarse pasos.

### Paso 1 — Aislamiento, con todo desenergizado

Multímetro en continuidad:

| Entre | Debe medir |
|---|---|
| `GND_LOG` y `GND_POT` | **Circuito abierto** (sin pitido, > 1 MΩ) |
| `+3.3V` y `GND_LOG` | Sin corto |
| `+5V_POT` y `GND_POT` | Sin corto |
| Ánodo del LED de cada opto y `GND_POT` | **Circuito abierto** |

Si hay continuidad entre las dos tierras, **parar aquí** y encontrar el puente antes de
seguir.

### Paso 2 — Riel de potencia solo

Energizar únicamente la batería y el lado de potencia, sin la Raspberry Pi conectada.

| Punto | Esperado |
|---|---|
| `VS` del L298N | 7.4 V nominal (6.0–8.4 V según carga de la batería) |
| `VSS` del L298N | 5 V ± 0.25 V |
| `ENA` y `ENB` del L298N | ~5 V: los jumpers están puestos |
| Entradas `IN1`–`IN4` (con optos en reposo) | ~5 V, por el pull-up — los motores quedan frenados |

### Paso 3 — Optoacopladores, con fuente de banco

Sin la Raspberry Pi todavía. Con una fuente de 3.3 V, inyectar en el ánodo de cada
opto a través de su resistencia de 390 Ω:

| Entrada del opto | Salida esperada en el L298N |
|---|---|
| 3.3 V | ~0.2 V (nivel bajo) |
| 0 V | ~5 V (nivel alto) |

Esto confirma el aislamiento y **confirma también la inversión** descrita arriba.

El canal del servo, en cambio, **no invierte**. Con el servo desconectado:

| Entrada del opto (GPIO 25) | Salida hacia el servo |
|---|---|
| 3.3 V | ~4.8 V (nivel alto) |
| 0 V | ~0 V (nivel bajo) |

### Paso 4 — Riel lógico solo

Energizar el riel lógico sin la Raspberry Pi. Medir en el conector donde iría:

| Punto | Esperado |
|---|---|
| Salida del regulador de 5 V | 5.00 V ± 0.25 V, **sin carga y con carga** |
| Rizado (osciloscopio, si hay) | < 100 mV pico a pico |

Una lectura de 4.7 V en vacío indica un regulador que no va a sostener la corriente de
arranque de la Pi. Resolverlo antes de continuar.

### Paso 5 — Conectar la Raspberry Pi

Solo si los cuatro pasos anteriores pasaron. Primero con los motores **desconectados
del L298N**, verificando por consola serie que arranca y que los GPIO responden. Los
motores se conectan de últimos.

La primera vez que se conectan, hacerlo **con el robot elevado** (llantas sin tocar el
piso), en modo manual, y avanzar a baja velocidad desde el panel web: las dos llantas
tienen que girar hacia adelante. Si giran al revés, la compensación de la inversión no
coincide con la placa: revisar `OPTO_INVERTIDO` en `lib/lib_motors.c` y el Paso 3.

---

## Estado

- [x] Solución de aislamiento definida: 5 × PC817, una por señal de control
- [x] PWM sobre `IN1`–`IN4` con el L298N siempre habilitado (jumpers `ENA`/`ENB`)
- [x] Canal del servo en seguidor de emisor, sin inversión, con su cálculo
- [x] Resistencias calculadas contra el presupuesto de corriente del GPIO
- [x] Separación de tierras especificada, con el error común documentado
- [x] Inversión del optoacoplador identificada y compensada en `lib_motors.c` (`OPTO_INVERTIDO`)
- [x] Procedimiento de verificación con multímetro, en cinco pasos
- [ ] Circuito armado en placa perforada — **pendiente: requiere los componentes**
- [ ] Verificación de los cinco pasos ejecutada y registrada
- [ ] Compensación de la inversión verificada con la placa real (Paso 3 y primer avance con el robot elevado)
