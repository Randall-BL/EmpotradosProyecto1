# Chasis y modelo físico

El modelo físico tiene rubro propio: se evalúa **funcionalidad y estética**. Este
documento fija el diseño; el armado queda pendiente de tener el kit.

---

## Forma: circular

| | **Circular** | Rectangular |
|---|---|---|
| Girar en un espacio cerrado | Gira sobre su eje sin tocar nada | Las esquinas chocan al girar |
| Salir de un rincón | Rota y sale | Se puede trabar |
| Construcción | Corte circular, más trabajoso | Corte recto, trivial |
| Aprovechamiento del área interna | Menor | Mayor |
| Referencia visual | Es lo que la gente espera de una aspiradora | Parece un prototipo |

**Se elige circular.** La razón decisiva es la primera: con tracción diferencial y chasis
circular, el robot gira sobre su propio centro sin barrer área adicional, así que
cualquier rincón del que pueda entrar es un rincón del que puede salir. Un chasis
rectangular se traba en esquinas, y en una demostración en vivo eso es un fallo visible.

La estética se lleva de paso: es la silueta que se asocia a una aspiradora robot.

---

## Dimensiones

| Parámetro | Valor | Motivo |
|---|---|---|
| Diámetro | **22–25 cm** | Espacio para la Pi (8.5 × 5.6 cm), el pack 2S y las dos placas perforadas |
| Altura total | **10–12 cm** | Los sensores deben quedar a 5–8 cm del piso |
| Separación al piso | **1.5–2 cm** | Salva desniveles de alfombra sin encallar |
| Masa objetivo | **< 1.5 kg** | Por encima, los motores DC pequeños pierden par para arrancar |

Dos niveles unidos por separadores hexagonales M3 de 40 mm:

```
   ── NIVEL SUPERIOR ──────────────────────────
      LEDs indicadores (rotulados)
      Altavoz
      Interruptor principal
      Raspberry Pi 4 (acceso a USB-C, HDMI y microSD)

   ── NIVEL INFERIOR ──────────────────────────
      Pack 2S 18650 + BMS  (al centro, bajo)
      Regulador buck 5 V
      Placa de optoacopladores + L298N
      Placa del amplificador LM386
      Motores DC ×2 + rueda loca
      Sensores HC-SR04 ×3 (en el borde)
```

La batería va **al centro y lo más bajo posible**: es el componente más pesado y baja el
centro de gravedad, que es lo que evita que el robot cabecee al arrancar o al frenar.

---

## Tracción

```
              FRENTE
         ┌──────────────┐
         │   HC-SR04    │  ← sensor frontal, 0°
     ╱   │   frontal    │   ╲
   HC    └──────────────┘    HC     ← laterales a 45°
  SR04                      SR04
    │                         │
  ══╪═══   Motor izq   ═══════╪══   ← eje de tracción,
    │      ●    ●    Motor der│        ligeramente adelante del centro
    │                         │
         ┌──────────────┐
         │  rueda loca  │        ← apoyo trasero
         └──────────────┘
             ATRÁS
```

**Dos motores DC con reductora y una rueda loca de apoyo.** El eje de tracción va
ligeramente por delante del centro geométrico, de modo que el peso descanse
mayoritariamente sobre las ruedas motrices y no sobre la rueda loca: mejora la
tracción y evita que patine al arrancar.

| Componente | Especificación |
|---|---|
| Motores | DC con reductora, 6 V, 100–200 RPM a la salida |
| Llantas | 6.5 cm de diámetro, con banda de goma |
| Rueda loca | Esférica, 2.5–3 cm, metálica o plástica |

Las dos llantas deben ser **del mismo lote**: una diferencia de un milímetro en el
diámetro hace que el robot describa un arco cuando el software le pide ir recto. Se
corrige en la calibración de velocidades (issue #20), pero es mejor no tener que hacerlo.

### Odometría

Los motores del kit rara vez traen encoder. Si no lo traen, la odometría (issue #22) se
estima por tiempo y ciclo de trabajo: se calibra midiendo la distancia real recorrida a
cada ciclo de trabajo durante un tiempo conocido, y se construye una tabla. Es menos
exacta —el error se acumula y la deriva es peor sobre alfombra que sobre piso duro— pero
alcanza para el mapa de grilla que pide el enunciado.

Si el kit trae encoders de ranura, van montados en el eje **antes** de la reductora y
usan dos GPIO de los que quedaron libres (7 y 8).

---

## Materiales del chasis

| Opción | Ventaja | Desventaja |
|---|---|---|
| **Acrílico 3 mm cortado a láser** | Acabado limpio, se ve profesional | Requiere acceso al cortador |
| MDF 3 mm | Barato, se corta a mano | Se ve tosco si no se lija y pinta |
| PLA impreso en 3D | Soportes integrados | Tiempo de impresión, requiere impresora |

**Acrílico negro de 3 mm** si hay acceso al cortador láser del TEC; si no, MDF de 3 mm
lijado y pintado de negro mate. El rubro de estética se gana con acabado prolijo, no con
material caro: bordes lijados, sin rebabas, tornillería pareja y cableado ordenado.

---

## Ruteo del cableado

Es parte del rubro de estética y también de funcionalidad: un cable suelto puede
enredarse en una llanta y detener la demostración.

- **Canaletas o amarras cada 5 cm** a lo largo del chasis. Nada colgando.
- **Cables de motor separados de los de señal.** Los de motor van trenzados entre sí.
- **Conectores desconectables** (Dupont o JST) en motores, sensores y batería: permiten
  desarmar sin desoldar.
- **Cable de motor con holgura**, para poder levantar el nivel superior sin desconectar
  nada.
- **Rotular cada conector** con cinta y marcador. En medio de la demostración nadie
  recuerda cuál cable va a `IN3`.
- Los cables de audio, lo más lejos posible de los de motor.

---

## Accesibilidad para la demostración

Cosas que en el momento de presentar se agradecen:

| Elemento | Por qué debe quedar accesible |
|---|---|
| Interruptor principal | Encender y apagar sin desarmar |
| Ranura de microSD | Reflashear la imagen sin desmontar la Pi |
| Puerto USB-C | Alimentar desde la pared si la batería falla |
| Conector de carga | Cargar sin abrir el chasis |
| LEDs | Visibles desde un metro, rotulados |
| Tornillos del nivel superior | Cabeza Phillips, no Allen de tamaño raro |

---

## Lista de materiales

| Cantidad | Componente |
|---|---|
| 2 | Disco de acrílico o MDF 3 mm, Ø 22–25 cm |
| 6 | Separador hexagonal M3 × 40 mm + tornillos |
| 2 | Motor DC con reductora, 6 V |
| 2 | Llanta con banda de goma, Ø 6.5 cm |
| 2 | Soporte de motor |
| 1 | Rueda loca esférica |
| 3 | Soporte de HC-SR04 |
| — | Tornillería M3 |
| — | Amarras plásticas / canaleta |
| — | Conectores Dupont y JST |
| 2 | Placa perforada (etapa de potencia y amplificador) |

---

## Verificación

| Prueba | Criterio |
|---|---|
| Masa total | < 1.5 kg |
| Giro sobre el eje | Rota 360° sin que ninguna parte sobresalga del círculo |
| Estabilidad | No cabecea al arrancar ni al frenar de golpe |
| Separación al piso | Salva un obstáculo de 1 cm |
| Cableado | Ningún cable a menos de 1 cm de una llanta |
| Sensores | Los tres en el mismo plano horizontal, a 5–8 cm del piso |
| Autonomía | Navega 30 minutos sin recalentamiento ni caída de tensión |

---

## Estado

- [x] Forma decidida (circular) y justificada contra la alternativa
- [x] Dimensiones, altura, separación al piso y masa objetivo especificadas
- [x] Distribución en dos niveles, con la batería baja y al centro
- [x] Configuración de tracción diferencial + rueda loca definida
- [x] Estrategia de odometría definida para motores sin encoder
- [x] Criterios de ruteo de cableado y accesibilidad para la demostración
- [x] Lista de materiales y criterios de verificación
- [ ] Chasis cortado y armado — **pendiente: requiere el kit**
- [ ] Motores, sensores, LEDs y altavoz montados — **pendiente**
- [ ] Acabado estético terminado — **pendiente**
