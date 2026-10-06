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
2. **Arranques y frenados.** Cada vez que el robot arranca, frena o evade un obstáculo
   la corriente del motor se conmuta de golpe. Cada flanco es un transitorio.
3. **Inversión de sentido.** Pasar de avance a retroceso invierte la corriente en un
   inductor cargado: es el peor caso.

El GPIO de la Raspberry Pi **no tiene diodos de protección hacia 3.3 V** y tolera
3.3 V como máximo absoluto. No hay margen.

---

## Solución adoptada: PC817 en las cuatro señales de los motores

El L298N se usa con los **jumpers `ENA` y `ENB` puestos**: el puente queda siempre
habilitado y esos pines no salen a la Raspberry Pi. Los motores se controlan con
**niveles fijos sobre las entradas de sentido** `IN1`–`IN4` (ver
[Velocidad fija](#velocidad-fija)), así que hacia el dominio de potencia cruzan cuatro
señales, y las cuatro lo hacen por un optoacoplador. El **servo del radar** no cruza
la barrera: se alimenta de los 5 V de la Raspberry Pi y su señal sale directo del GPIO
(ver [más abajo](#el-servo-va-en-el-dominio-lógico)).

```
     DOMINIO LÓGICO (3.3 V)          │        DOMINIO DE POTENCIA (9 V)
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

Para avanzar, `IN1` en alto e `IN2` en bajo; para retroceder, al revés. Detenerse es
dejar las dos en bajo: **frena en seco**, no queda girando libre. Igual para `IN3`/`IN4`
y el motor B. Para girar, un motor hacia adelante y el otro hacia atrás: el robot rota
sobre su eje.

Los pines `GPIO 12` y `13`, que antes llevaban `ENA`/`ENB`, quedan libres.

### Velocidad fija

Los motores trabajan **sin PWM**: la entrada activa lleva un nivel fijo y cualquier
velocidad distinta de cero es la máxima (`MOTOR_VELOCIDAD_VARIABLE` en 0, en
`lib/lib_motors.h`). El robot avanza, retrocede, gira sobre su eje hacia los dos lados y
frena, que es el control diferencial que pide el enunciado.

> **Desviación acordada con el profesor.** El enunciado pide que la velocidad de cada
> motor se controle por PWM. Se le consultó al profesor y se acordó que en este robot la
> PWM no es necesaria, porque con ella el robot no se mueve.

La razón está en los motores. Son motorreductores lentos y de alto torque (30 rpm a 6 V
y 60 rpm a 12 V; 110 y 200 oz-in a rotor bloqueado) y trabajan a unos 9 V, que es lo que
da la batería. El L298N cae unos 2 V, así que a fondo les llegan unos 7 V: con ruedas de
30 mm, el robot avanza a unos 5 cm/s. Recortar esa tensión con PWM los deja por debajo
de lo que necesitan para mover el robot.

Se comprobó en la placa: con PWM de 1 kHz sobre la entrada activa **los motores no se
movieron**. A la caída del L298N se suma que el PC817 con 4.7 kΩ de pull-up tarda decenas
de microsegundos en apagarse, y en cada ciclo se come parte del tiempo en que la entrada
debía estar en alto.

El modo con PWM sigue en la biblioteca (`MOTOR_VELOCIDAD_VARIABLE` en 1) y está probado
en el simulador, pero el robot no lo usa.

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
de 50 mA, y a eso hay que restarle todavía los LEDs indicadores.
Con 5 mA por canal los cuatro optos suman 20 mA y queda holgura para los LEDs, de los que
nunca hay más de tres encendidos (15 mA). La cuenta
completa está en [`hardware-pinout.md`](hardware-pinout.md).

El PC817 con `CTR` mínimo de 50 % da 2.5 mA de colector con `If = 5 mA`. Contra la
resistencia de 4.7 kΩ a 5 V eso satura el transistor de sobra: hacen falta apenas
1.1 mA para llevar la salida a nivel bajo.

### Resistencia de salida

`4.7 kΩ` de colector a `+5V_POT`. Con `Cpar ≈ 10 pF` de la entrada del L298N la
constante de tiempo es de unos 50 ns, despreciable. Lo que manda es el apagado del
propio PC817: con la carga de 4.7 kΩ y el transistor saturado tarda decenas de
microsegundos. Con niveles fijos, que es como trabaja el robot, no importa; con PWM de
1 kHz fue parte de por qué los motores no se movieron (ver [arriba](#velocidad-fija)).

Bajarla a 1 kΩ acelera el apagado pero consume 5 mA por canal del riel de potencia.

---

## ⚠️ El optoacoplador invierte la señal

En configuración de emisor común —la del diagrama— el fototransistor **conduce** cuando
el LED está encendido, y al conducir lleva la salida a **nivel bajo**:

| GPIO | LED del opto | Fototransistor | Entrada del L298N |
|---|---|---|---|
| Alto (3.3 V) | Encendido | Conduce | **Bajo** |
| Bajo (0 V) | Apagado | Corte | **Alto** (por el pull-up) |

Sin compensarlo, la entrada que debía quedar en bajo queda en alto y la que debía
quedar en alto, en bajo: cada motor **gira al revés**: `motores_avanzar()` haría retroceder el robot y el giro a
la derecha sería a la izquierda.

### Cómo se compensa

**En software, en `lib/lib_motors.c`**, el único punto donde la biblioteca toca los
motores:

```c
#define OPTO_INVERTIDO 1

static void entrada_l298n(unsigned gpio, int duty) {
#if MOTOR_VELOCIDAD_VARIABLE
  #if OPTO_INVERTIDO
    duty = MOTOR_PWM_MAX - duty;          /* PWM: se complementa el ciclo */
  #endif
    set_PWM_dutycycle(g_pi, gpio, (unsigned)duty);
#else
    int nivel = duty > 0;
  #if OPTO_INVERTIDO
    nivel = !nivel;                       /* nivel fijo: se invierte */
  #endif
    gpio_write(g_pi, gpio, (unsigned)nivel);
#endif
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

## El servo va en el dominio lógico

El servo del radar se alimenta del **pin de 5 V de la Raspberry Pi** y su señal sale
**directo del GPIO 25**, sin optoacoplador. Un servo de este tamaño acepta el pulso de
3.3 V como nivel alto, y al compartir fuente y tierra con la Pi no hay barrera que
cruzar.

Los pulsos los genera `pigpiod` por DMA (`set_servo_pulsewidth`). En reposo el GPIO
queda en bajo y el servo no recibe pulsos: se queda donde estaba.

El costo de esta decisión es que los picos de corriente del servo entran al riel de 5 V
de la Pi; cómo se mantienen controlados está en
[`hardware-alimentacion.md`](hardware-alimentacion.md).

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
     │  Servo del radar                               │
     │  Lado LED de los 4 optoacopladores             │
     │                                                │
     │  Todos referidos a GND_LOG                     │
     └────────────────────────────────────────────────┘
                          ╳  SIN CONEXIÓN
     ┌─────────────── DOMINIO DE POTENCIA ────────────┐
     │  Driver L298N (VS, VSS y jumpers ENA/ENB)      │
     │  2 motores DC                                  │
     │  2 baterías de 9 V en paralelo                 │
     │  Lado fototransistor de los 4 optoacopladores  │
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
lógica del L298N pasa a ser la de la Pi y el aislamiento queda anulado, aunque los cuatro
optoacopladores estén perfectamente montados.

`VSS` se alimenta desde el regulador interno del propio módulo L298N, con el jumper de
5 V puesto: es válido mientras `VS ≤ 12 V`, y las baterías dan 9 V. De esa misma salida
(`+5V_POT`) cuelgan los pull-up de los optoacopladores.

---

## Alternativa: driver con aislamiento integrado

El DRV8871 acepta entrada de 3.3 V directa, pero **no aísla galvánicamente**: comparte
tierra. Sirve para simplificar el nivel lógico, no para reemplazar los optoacopladores.

Si se prefiere un componente en vez de cuatro, un **ADuM1401** (aislador digital de
cuatro canales) cubre las cuatro entradas del L298N con aislamiento real, y es lo
bastante rápido para la PWM. Es más caro y más difícil de conseguir localmente que
cuatro PC817.

**Se mantiene la solución con PC817.**

> El servo y el HC-SR04 que lleva encima pertenecen los dos al dominio lógico: su
> alimentación y sus señales llegan desde la Raspberry Pi. Ninguno de sus cables debe
> tocar el lado de potencia.

---

## Lista de materiales

| Cantidad | Componente | Notas |
|---|---|---|
| 4 | Optoacoplador PC817 (o 4N25) | Uno por entrada: `IN1`–`IN4` |
| 4 | Resistencia 390 Ω, 1/4 W | Limitación del LED, canales del L298N |
| 4 | Resistencia 4.7 kΩ, 1/4 W | Pull-up de colector hacia `+5V_POT` |
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

Conectar únicamente las baterías de 9 V, sin la Raspberry Pi encendida.

| Punto | Esperado |
|---|---|
| `VS` del L298N | Alrededor de 9 V con baterías nuevas |
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

### Paso 4 — Dominio lógico solo

Conectar el power bank a la Raspberry Pi por USB-C, con las baterías de 9 V
desconectadas. La Pi debe arrancar y el servo, el sensor y el MPU-6050 deben responder
(ver [`hardware-sensores.md`](hardware-sensores.md)).

### Paso 5 — Conectar la Raspberry Pi

Solo si los cuatro pasos anteriores pasaron. Primero con los motores **desconectados
del L298N**, verificando por consola serie que arranca y que los GPIO responden. Los
motores se conectan de últimos.

La primera vez que se conectan, hacerlo **con el robot elevado** (llantas sin tocar el
piso), en modo manual, y avanzar desde el panel web: las dos llantas
tienen que girar hacia adelante. Si giran al revés, la compensación de la inversión no
coincide con la placa: revisar `OPTO_INVERTIDO` en `lib/lib_motors.c` y el Paso 3.

---

## Estado

- [x] Solución de aislamiento definida: 4 × PC817, uno por entrada del L298N
- [x] L298N siempre habilitado (jumpers `ENA`/`ENB`), control por `IN1`–`IN4`
- [x] Velocidad fija, con niveles fijos en `IN1`–`IN4` y sin PWM: desviación acordada con el profesor
- [x] Servo en el dominio lógico: 5 V de la Raspberry Pi y señal directa del GPIO 25
- [x] Resistencias calculadas contra el presupuesto de corriente del GPIO
- [x] Separación de tierras especificada, con el error común documentado
- [x] Inversión del optoacoplador identificada y compensada en `lib_motors.c` (`OPTO_INVERTIDO`)
- [x] Procedimiento de verificación con multímetro, en cinco pasos
- [x] Circuito armado
- [x] Verificación ejecutada: todos los pasos pasaron
- [ ] Compensación de la inversión verificada con la placa real (Paso 3 y primer avance con el robot elevado)
