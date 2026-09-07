# Sistema de alimentación

Dos rieles regulados por separado desde una batería con protección. La Raspberry Pi
nunca se alimenta directamente de la celda.

---

## Por qué no se conecta la batería directo

Una celda Li-Ion entrega **4.2 V cargada y 3.0 V descargada**. La Raspberry Pi 4
requiere **5 V estables** y detecta caída de tensión (*undervoltage*) por debajo de
4.63 V: reduce la frecuencia del SoC, corrompe la microSD y termina reiniciándose.

Y aunque la tensión fuera correcta, alimentar la lógica y los motores del mismo punto
mete el transitorio de arranque de los motores —cinco a ocho veces la corriente
nominal— directamente en el riel de la Pi.

---

## Arquitectura

```
   ┌─────────────┐
   │  2S Li-Ion  │  2 × 18650 en serie
   │   18650     │  7.4 V nominal · 6.0–8.4 V
   └──────┬──────┘
          │
   ┌──────┴──────┐
   │  BMS 2S     │  sobredescarga · sobrecarga · cortocircuito · balanceo
   └──────┬──────┘
          │
          ├────────────────────────────────┐
          │                                │
   ┌──────┴───────┐                 ┌──────┴───────┐
   │ Buck 5V / 3A │                 │   Directo    │
   │ MP2307       │                 │   7.4 V      │
   │ o LM2596     │                 │              │
   └──────┬───────┘                 └──────┬───────┘
          │                                │
    RIEL LÓGICO                       RIEL POTENCIA
    5 V · GND_LOG                     7.4 V · GND_POT
          │                                │
    ┌─────┴─────┐                    ┌─────┴─────┐
    │ RPi 4     │                    │ L298N VS  │
    │ HC-SR04×3 │                    │ Motor izq │
    │ LEDs ×4   │                    │ Motor der │
    │ LM386     │                    └───────────┘
    └───────────┘

           ╳  GND_LOG y GND_POT NO se unen
              (ver docs/hardware-aislamiento.md)
```

---

## Batería: 2S en vez de 1S

| | 1S (3.7 V) | **2S (7.4 V)** |
|---|---|---|
| Regulador para la Pi | Boost o buck-boost | **Buck** — más simple y eficiente |
| Corriente de entrada a 15 W | ~4.5 A | **~2.2 A** |
| Alimentación de motores | Necesita elevador aparte | **Directa** |
| Pérdidas en cables | Altas | Bajas |

Con 1S hace falta un elevador de 3.7 V a 5 V que sostenga 3 A: son 4.5 A de entrada, y
en el extremo descargado (3.0 V) más de 5 A. Los módulos MT3608 y XL6009 típicos no lo
dan de forma sostenida, aunque su hoja de datos lo insinúe.

Con 2S, un reductor a 5 V trabaja con holgura en todo el rango de descarga: incluso a
6.0 V hay 1 V de margen sobre la salida, suficiente para un LM2596 y de sobra para un
MP2307.

**Especificación:** 2 × 18650 de al menos 2500 mAh y 10 A de descarga continua, en serie.

> Celdas del mismo lote y con la misma carga inicial. Celdas dispares en serie se
> desbalancean y el BMS termina cortando antes de tiempo.

---

## BMS — no es opcional

Un pack 2S sin BMS es un riesgo de incendio, no una simplificación de diseño.

| Protección | Qué evita |
|---|---|
| Sobredescarga (corte a ~2.5 V/celda) | Daño permanente de la celda por descarga profunda |
| Sobrecarga (corte a ~4.25 V/celda) | Fuga térmica durante la carga |
| Cortocircuito | Corriente sin límite ante una falla de cableado |
| Balanceo | Que una celda se descargue mucho más que la otra |

**Especificación:** BMS 2S de 10 A o más, con balanceo.

Muchos packs comerciales de 18650 ya lo traen integrado. Verificarlo antes de comprarlo
aparte: se reconoce por la placa pequeña soldada entre las celdas y los terminales de
salida.

---

## Riel lógico: 5 V, 3 A

| Consumidor | Corriente típica | Pico |
|---|---|---|
| Raspberry Pi 4 (sin periféricos) | 600–900 mA | 1.2 A |
| Raspberry Pi 4 (WiFi + CPU al 100 %) | 1.2 A | **2.5 A** |
| 3 × HC-SR04 | 45 mA | 60 mA |
| 4 × LED a 5 mA | 20 mA | 20 mA |
| Amplificador LM386 + altavoz | 50 mA | 300 mA |
| 6 × LED de optoacoplador a 5 mA | 30 mA | 30 mA |
| **Total** | **~2.0 A** | **~2.9 A** |

**Especificación:** regulador reductor de 5 V con **3 A de salida continua**.

| Módulo | Tipo | Corriente | Nota |
|---|---|---|---|
| **MP2307** | Conmutado síncrono | 3 A | Recomendado. ~93 % de eficiencia |
| LM2596 | Conmutado | 3 A | Funciona. Más caliente, ~80 %, necesita disipador |
| AMS1117 | Lineal | 1 A | **No sirve.** Disiparía 2.4 W como calor y no da la corriente |

En el módulo hay que **ajustar el potenciómetro a 5.0 V con el multímetro antes de
conectar la Pi**. Vienen ajustados de fábrica en cualquier valor, a veces 12 V.

### Autonomía estimada

```
Energía útil del pack:  2500 mAh × 7.4 V × 0.85 (BMS + margen)  ≈ 15.7 Wh
Consumo del riel lógico: 2.0 A × 5 V / 0.93 (eficiencia)        ≈ 10.8 W
Consumo de motores:      ~0.5 A × 7.4 V en navegación           ≈  3.7 W
                                                        Total  ≈ 14.5 W

Autonomía ≈ 15.7 Wh / 14.5 W ≈ 65 minutos
```

Suficiente para la demostración con margen. Aun así, **el plan B incluye un pack
cargado de repuesto** (issue #34).

---

## Riel de potencia: 7.4 V directo

Los motores DC del kit trabajan típicamente entre 6 y 12 V. El L298N cae unos **2 V**
entre `VS` y la salida por su topología de transistores bipolares, así que con `VS` a
7.4 V los motores reciben cerca de **5.4 V**. Es adecuado para motores de 6 V nominales.

Si el kit trae motores de 12 V, hay dos caminos: subir a 3S (11.1 V) —revisando que el
buck de 5 V lo tolere— o aceptar que girarán más lento. **Decidir cuando el kit esté en
mano.**

`VSS` (los 5 V de la lógica interna del L298N) sale del regulador interno del módulo con
el jumper puesto, válido mientras `VS ≤ 12 V`. **Nunca de los 5 V de la Raspberry Pi**:
eso anula el aislamiento (ver [`hardware-aislamiento.md`](hardware-aislamiento.md)).

---

## Desacople

| Dónde | Componente | Para qué |
|---|---|---|
| Entrada del buck de 5 V | 470 µF electrolítico | Absorbe el transitorio de la batería |
| Salida del buck de 5 V | 220 µF + 100 nF | Sostiene el pico de arranque de la Pi |
| `VS` del L298N | 100 µF + 100 nF | Absorbe el pico de arranque de los motores |
| Cada HC-SR04 | 100 nF | Ruido del pulso de disparo |

Los electrolíticos van **con la polaridad correcta y lo más cerca posible del punto que
desacoplan**. Un capacitor a diez centímetros por un cable largo no desacopla nada.

---

## Cableado

| Tramo | Calibre | Motivo |
|---|---|---|
| Batería → BMS → reguladores | **AWG 18** | Hasta 3 A continuos |
| Riel de potencia → L298N → motores | **AWG 20** | Corriente de arranque |
| Regulador 5 V → Raspberry Pi | **AWG 20**, lo más corto posible | La caída en el cable cuenta contra el margen de 4.63 V |
| Señales de sensores y LEDs | AWG 24 / Dupont | Corriente despreciable |

Alimentar la Pi por el **conector USB-C**, no por los pines 2 y 4 del header: el USB-C
pasa por la protección de entrada de la tarjeta, y los pines no.

Los cables de motor van **trenzados entre sí** y separados de los cables de señal. Un
par trenzado cancela buena parte del campo magnético que de otro modo se acopla a las
líneas de `ECHO`.

Un **interruptor principal** en la salida del BMS, accesible sin desarmar el chasis.

---

## Lista de materiales

| Cantidad | Componente | Especificación |
|---|---|---|
| 2 | Celda 18650 | ≥ 2500 mAh, ≥ 10 A de descarga |
| 1 | Portapilas 2S 18650 | Con terminales soldables |
| 1 | Módulo BMS 2S | ≥ 10 A, con balanceo |
| 1 | Módulo buck 5 V | MP2307, 3 A |
| 1 | Interruptor SPST | ≥ 5 A |
| 1 | Portafusible + fusible 5 A | En serie con el positivo del pack |
| 1 | Conector de carga | Según el cargador del pack |
| 1 | Capacitor 470 µF / 25 V | Entrada del buck |
| 1 | Capacitor 220 µF / 10 V | Salida del buck |
| 2 | Capacitor 100 nF cerámico | Desacople |
| — | Cable AWG 18 y AWG 20 | Rojo y negro |

---

## Verificación — **antes de conectar la Raspberry Pi**

### Paso 1 — Pack y BMS, sin carga

| Punto | Esperado |
|---|---|
| Salida del pack (cargado) | 8.2–8.4 V |
| Tensión de cada celda | Diferencia < 0.05 V entre ambas |
| Salida del BMS | Igual a la del pack |

Una diferencia mayor a 0.1 V entre celdas indica desbalance: cargarlas por separado
antes de armar el pack.

### Paso 2 — Reguladores en vacío

| Punto | Esperado |
|---|---|
| Salida del buck de 5 V | **5.00 V ± 0.10 V** — ajustar con el potenciómetro |
| Riel de potencia | 7.4 V nominal |

### Paso 3 — Reguladores con carga

Con una carga resistiva equivalente (por ejemplo 2.5 Ω / 10 W para simular 2 A):

| Punto | Esperado |
|---|---|
| Salida del buck de 5 V bajo 2 A | **≥ 4.90 V** |
| Temperatura del regulador tras 5 min | < 60 °C al tacto |

Si cae por debajo de 4.9 V con carga, el módulo no da la corriente. Cambiarlo antes de
conectar la Pi: la Raspberry Pi 4 corrompe la microSD cuando entra en *undervoltage*.

### Paso 4 — Aislamiento entre rieles

| Entre | Esperado |
|---|---|
| `GND_LOG` y `GND_POT` | **Circuito abierto** |
| `+5V_LOG` y riel de potencia | **Circuito abierto** |

### Paso 5 — Ya con la Pi conectada

```bash
vcgencmd get_throttled
```

`throttled=0x0` significa alimentación correcta. Cualquier bit encendido indica
subtensión, presente o pasada:

| Bit | Significado |
|---|---|
| 0 | Subtensión **ahora** |
| 16 | Hubo subtensión desde el arranque |
| 1 / 17 | Límite de frecuencia por subtensión |

Conviene medirlo con el robot navegando y reproduciendo audio a la vez, que es el peor
caso de consumo.

---

## Estado

- [x] Topología definida: 2S + BMS + dos rieles regulados por separado
- [x] Elección de 2S sobre 1S justificada con números
- [x] Presupuesto de corriente del riel lógico calculado (~2.0 A típico, ~2.9 A pico)
- [x] Regulador especificado (MP2307, 3 A) con las alternativas descartadas y por qué
- [x] Autonomía estimada: ~65 minutos
- [x] Lista de materiales y calibres de cable
- [x] Procedimiento de verificación con multímetro, en cinco pasos
- [ ] Componentes conseguidos y pack armado — **pendiente**
- [ ] Verificación de los cinco pasos ejecutada y registrada
- [ ] `vcgencmd get_throttled` en operación real
