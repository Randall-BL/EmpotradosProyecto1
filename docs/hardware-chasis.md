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
| Altura total | **10–12 cm** | El sensor del radar debe quedar a 5–8 cm del piso |
| Separación al piso | **1.5–2 cm** | Salva desniveles de alfombra sin encallar |
| Masa objetivo | **< 1.5 kg** | Por encima, los motores DC pequeños pierden par para arrancar |

Dos niveles unidos por separadores hexagonales M3 de 40 mm:

```
   ── NIVEL SUPERIOR ──────────────────────────
      LEDs indicadores (rotulados)
      Parlante
      Interruptor principal
      Raspberry Pi 4 (acceso a USB-C, HDMI y microSD)

   ── NIVEL INFERIOR ──────────────────────────
      Pack 2S 18650 + BMS  (al centro, bajo)
      Reguladores buck 5 V ×2 (lógica y servo)
      Placa de optoacopladores + L298N
      Amplificador PAM8403 + filtro RC
      MPU-6050 (entre las dos llantas, atornillado)
      Motores DC ×2 + rueda loca
      Radar: servo + HC-SR04 (en el borde frontal)
```

La batería va **al centro y lo más bajo posible**: es el componente más pesado y baja el
centro de gravedad, que es lo que evita que el robot cabecee al arrancar o al frenar.

---

## Tracción

```
                  FRENTE
          180°     90°      0°
             ╲      │      ╱        ← barrido del radar, cada 30°
              ╲  ┌──┴──┐  ╱
               ╲ │HC-SR│ ╱          ← HC-SR04 sobre el brazo del servo,
                 └──┬──┘               eje 10 cm adelante del centro
                  servo
    │                         │
  ══╪═══   Motor izq  [MPU]═══╪══   ← eje de tracción, ligeramente adelante
    │      ●    ●    Motor der│        del centro; el MPU-6050 entre las llantas
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

Los motores del kit rara vez traen encoder. Sin ellos, la odometría (issue #22) combina
dos fuentes: el **MPU-6050**, cuyo giroscopio da el rumbo y cuyo acelerómetro, integrado,
da la velocidad; y el **modelo de los motores** —velocidad a cada ciclo de trabajo,
medida en la calibración—, que corrige la deriva del acelerómetro. El detalle está en
[`navegacion-radar.md`](navegacion-radar.md). El error se acumula igual, pero el rumbo,
que es lo que más deforma el mapa, deja de depender de que las dos llantas patinen lo
mismo.

Por eso el MPU va **entre las dos llantas**, sobre el eje de tracción: ahí girar sobre
el propio eje no produce aceleración centrípeta que el sensor confunda con avance.

Si el kit trae encoders de ranura, van montados en el eje **antes** de la reductora y
usan dos GPIO de los que quedaron libres (7 y 8).

### El radar en el borde frontal

El servo va atornillado al nivel inferior, con el eje a **10 cm por delante del centro**
(`RADAR_EJE_ADELANTE_CM` en `lib/lib_radar.h`) y el HC-SR04 sobre su brazo, a 5–8 cm
del piso. Al barrer de 0° a 180° el sensor no puede rozar el nivel superior, los
separadores ni los cables: conviene dejarlo en una muesca del borde, de modo que a 0° y
180° quede de costado sin salir del círculo del chasis. El cable del sensor sale por el
eje de giro, con holgura para media vuelta.

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
| 1 | Servo MG90S (o SG90) de 180° con su brazo |
| 1 | Soporte del HC-SR04 al brazo del servo |
| 1 | Soporte del servo al chasis |
| 2 | Separador de nylon M3 (MPU-6050) |
| — | Tornillería M3 |
| — | Amarras plásticas / canaleta |
| — | Conectores Dupont y JST |
| 2 | Placa perforada (etapa de potencia con los 5 optos, y filtro de audio) |

---

## Verificación

| Prueba | Criterio |
|---|---|
| Masa total | < 1.5 kg |
| Giro sobre el eje | Rota 360° sin que ninguna parte sobresalga del círculo |
| Estabilidad | No cabecea al arrancar ni al frenar de golpe |
| Separación al piso | Salva un obstáculo de 1 cm |
| Cableado | Ningún cable a menos de 1 cm de una llanta |
| Radar | El sensor barre de 0° a 180° sin rozar nada, horizontal, a 5–8 cm del piso |
| MPU-6050 | Entre las llantas, plano y firme: no se mueve al empujarlo con el dedo |
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
- [x] Ubicación del radar (servo + HC-SR04) y del MPU-6050 definida
- [ ] Motores, radar, MPU-6050, LEDs y parlante montados — **pendiente**
- [ ] Acabado estético terminado — **pendiente**
