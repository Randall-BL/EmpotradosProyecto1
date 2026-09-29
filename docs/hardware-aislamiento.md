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

## Solución adoptada: PC817 en las siete señales

Se aíslan las seis señales de control del L298N, no solo las de PWM. Aislar únicamente
`ENA`/`ENB` deja `IN1`–`IN4` como camino de retorno para el ruido, que es justo lo que
se quiere cortar. La séptima es la señal del **servo del radar**: es un motor, se
alimenta del riel de potencia y su señal cruza la barrera igual que las del L298N, pero
con un canal armado distinto (ver [más abajo](#el-canal-del-servo-no-invierte)).

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

Se repite seis veces, una por señal: `ENA`, `IN1`, `IN2`, `ENB`, `IN3`, `IN4`.

### Cálculo de la resistencia de entrada

El LED interno del PC817 tiene `Vf ≈ 1.2 V`:

```
R = (3.3 V − 1.2 V) / If
```

| `If` | R calculada | R comercial | Corriente total (×6) |
|---|---|---|---|
| 5 mA | 420 Ω | **390 Ω** | 30 mA |
| 8 mA | 262 Ω | 270 Ω | 48 mA |
| 10 mA | 210 Ω | 220 Ω | 60 mA ⚠️ |

**Se usa 390 Ω.** El límite de corriente sumada de todos los GPIO de la Raspberry Pi es
de 50 mA, y a eso hay que restarle todavía los LEDs indicadores y el canal del servo.
Con 8 mA por canal (48 mA solo en los optos) el presupuesto ya está roto. Con 5 mA
quedan 20 mA para lo demás: los LEDs, de los que nunca hay más de tres encendidos
(15 mA), y el canal del servo (3.1 mA). La cuenta completa está en
[`hardware-pinout.md`](hardware-pinout.md).

El PC817 con `CTR` mínimo de 50 % da 2.5 mA de colector con `If = 5 mA`. Contra la
resistencia de 4.7 kΩ a 5 V eso satura el transistor de sobra: hacen falta apenas
1.1 mA para llevar la salida a nivel bajo.

### Resistencia de salida

`4.7 kΩ` de colector a `+5V_POT`. Con `Cpar ≈ 10 pF` de la entrada del L298N la
constante de tiempo es de unos 50 ns, despreciable frente al kilohercio del PWM.

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

Consecuencias directas sobre el código actual:

- En `IN1`–`IN4` el sentido de giro queda **al revés**: `motores_avanzar()` haría
  retroceder el robot.
- En `ENA`/`ENB` el ciclo de trabajo se **complementa**: pedir 30 % entrega 70 %. Y
  `motores_detener()`, que escribe ciclo 0, dejaría los motores a **velocidad máxima**.

Esto último es peligroso: el robot arrancaría a fondo justo cuando el software cree que
lo está deteniendo.

### Cómo corregirlo

**Opción A — invertir en software (recomendada).** Cero componentes extra. En
`lib/lib_motors.c`:

```c
/* El PC817 en emisor comun invierte la senal: se compensa aqui, en el unico
 * punto donde la biblioteca toca el hardware. Ver docs/hardware-aislamiento.md */
#define OPTO_INVERTIDO 1

#if OPTO_INVERTIDO
  #define GPIO_NIVEL(v)  (!(v))
  #define PWM_DUTY(d)    (255 - (d))
#else
  #define GPIO_NIVEL(v)  (v)
  #define PWM_DUTY(d)    (d)
#endif
```

y aplicar `GPIO_NIVEL()` a cada `gpio_write()` de `IN1`–`IN4` y `PWM_DUTY()` a cada
`set_PWM_dutycycle()` de `ENA`/`ENB`.

**Opción B — segunda inversión en hardware.** Un 74HC04 o un transistor por canal en el
lado de potencia. Seis componentes más, más puntos de falla, y no aporta nada que la
opción A no resuelva.

**Se adopta la opción A.** Queda como tarea de la biblioteca (issue #15) y **debe estar
implementada y verificada antes de la primera prueba con motores montados.**

---

## El canal del servo no invierte

Para el servo, la inversión no se puede compensar en software sin un riesgo que los
motores no tienen. `pigpiod` genera los pulsos del servo por DMA
(`set_servo_pulsewidth`), y lo que no se puede invertir es el **reposo**: durante el
arranque, antes de que `pigpiod` configure el pin, o si el servidor se detiene, el GPIO
queda en bajo. Con un canal de emisor común eso es un nivel alto **permanente** en la
entrada del servo, que muchos servos interpretan como un pulso larguísimo y los lleva
contra el tope mecánico.

Por eso el séptimo canal se arma en **seguidor de emisor**: el colector va a `+5V_POT`
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
     │  Lado LED de los 7 optoacopladores             │
     │                                                │
     │  Todos referidos a GND_LOG                     │
     └────────────────────────────────────────────────┘
                          ╳  SIN CONEXIÓN
     ┌─────────────── DOMINIO DE POTENCIA ────────────┐
     │  Driver L298N (VS y VSS)                       │
     │  2 motores DC                                  │
     │  Servo del radar + su regulador de 5 V         │
     │  Lado fototransistor de los 7 optoacopladores  │
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

Si se prefiere un componente en vez de seis, un **ADuM1401** (aislador digital de cuatro
canales) cubre cuatro señales con aislamiento real; harían falta dos para las seis. Es
más caro y más difícil de conseguir localmente que seis PC817.

**Se mantiene la solución con PC817.**

> El HC-SR04 va montado **sobre** el servo pero pertenece al dominio lógico: su
> alimentación y sus señales llegan por su propio cable desde la Raspberry Pi. El brazo
> del servo es de plástico y no une las dos tierras; el cable del sensor no debe
> compartir conector con el del servo.

---

## Lista de materiales

| Cantidad | Componente | Notas |
|---|---|---|
| 7 | Optoacoplador PC817 (o 4N25) | Seis para el L298N y uno para el servo |
| 6 | Resistencia 390 Ω, 1/4 W | Limitación del LED, canales del L298N |
| 1 | Resistencia 680 Ω, 1/4 W | Limitación del LED, canal del servo |
| 7 | Resistencia 4.7 kΩ, 1/4 W | Seis pull-up de colector (L298N) y una a tierra de emisor (servo) |
| 1 | Driver L298N (módulo) | Doble puente H |
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
| Entradas del L298N (con optos en reposo) | ~5 V, por el pull-up |

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

---

## Estado

- [x] Solución de aislamiento definida: 7 × PC817, una por señal de control
- [x] Canal del servo en seguidor de emisor, sin inversión, con su cálculo
- [x] Resistencias calculadas contra el presupuesto de corriente del GPIO
- [x] Separación de tierras especificada, con el error común documentado
- [x] Inversión del optoacoplador identificada y con corrección definida en software
- [x] Procedimiento de verificación con multímetro, en cinco pasos
- [ ] Circuito armado en placa perforada — **pendiente: requiere los componentes**
- [ ] Verificación de los cinco pasos ejecutada y registrada
- [ ] Corrección de la inversión implementada en `lib_motors.c` (issue #15)
