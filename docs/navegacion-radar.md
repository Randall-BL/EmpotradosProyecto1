# Navegación con radar, velocidad del MPU-6050 y tiempo antes de chocar

Cómo usa el software el radar (un HC-SR04 sobre un servo de 180°) y el MPU-6050 para
navegar, evitar obstáculos y construir el mapa. El hardware está en
[`hardware-sensores.md`](hardware-sensores.md); la API, en
[`api-librobot.md`](api-librobot.md); la calibración en campo, en
[`odometria.md`](odometria.md).

---

## Quién hace qué

```
  librobot (lib/)                                   robot-server (server/src/main.c)
  ───────────────                                   ───────────────────────────────
  hilo del radar ── servo + HC-SR04 ──┐
     7 ángulos, ~5 lecturas/s         │  radar_lecturas()      hilo de navegación, 10 Hz
     guarda cada lectura con la pose  ├───────────────────►   · ¿obstáculo al frente?
     tiempo de choque al mirar al     │  radar_estado_choque()  · evadir / avanzar
     frente                           │                         · proyectar lecturas en el mapa
                                      │                         · estado para /api/status
  hilo de la odometría, 50 Hz ────────┘  odom_get()
     MPU-6050 + modelo de motores        odom_velocidad_cm_s()
     → posición, rumbo, velocidad
```

Los dos hilos de medición viven en la biblioteca y arrancan con `robot_init()`. El
servidor no espera al radar: lee la última lectura de cada ángulo cuando la necesita.

---

## 1. El barrido

El hilo del radar mueve el servo en vaivén —0°, 30°, …, 180° y de vuelta— y en cada
parada espera a que el servo llegue y deje de vibrar antes de disparar el sensor.
`servo_mover()` no deja ir al servo a su velocidad máxima: lo lleva en rampa, un pulso
intermedio cada 20 ms, a un tercio de ella (`SERVO_DIVISOR_VELOCIDAD`: 1.7 × 3 = 5.1 ms
por grado). Al terminar la rampa devuelve lo que falta para que llegue y se asiente (el
último escalón más 30 ms). El primer movimiento, desde una posición desconocida, no
tiene rampa posible: va de un salto y espera media vuelta completa.

Cada lectura se guarda con:

- el ángulo del servo y la distancia (−1 si no volvió eco);
- la **pose del robot en el instante de medir** (`odom_get()`): el mapa proyecta el eco
  donde estaba el robot al medirlo, no donde está cuando el servidor lo procesa;
- un número de secuencia, para que el servidor mapee cada lectura una sola vez.

| Magnitud | Valor |
|---|---|
| Lecturas por segundo | ~5 |
| Pasada completa (0 → 180) | ~1.2 s |
| Lectura frontal | cada ~1.2 s |

Es la **frecuencia de muestreo** del sistema (issue #20). La deducción está en
[`hardware-sensores.md`](hardware-sensores.md#barrido-y-tasa-de-muestreo).

---

## 2. La velocidad con el MPU-6050

La odometría (`lib/lib_odom.c`) corre en su propio hilo a 50 Hz. En cada paso lee el
MPU-6050 y los comandos de los motores:

```
a          = aceleración de avance del MPU (sin sesgo)
v_modelo   = velocidad que corresponde al PWM ordenado a las dos llantas

v  ←  v + a·dt                          (1) integrar el acelerómetro
v  ←  v + (dt/τ)·(v_modelo − v)         (2) corregir la deriva con el modelo, τ = 1 s
si los dos motores llevan 0.3 s detenidos:  v ← 0      (3) ZUPT

rumbo ← rumbo + giro_MPU·dt              el giroscopo, en lugar de la diferencia de llantas
```

**Por qué no basta con (1).** Un acelerómetro no mide velocidad, mide cambios de
velocidad. Después de calibrar le queda un sesgo de unos pocos cm/s²; integrado, ese
sesgo se vuelve un error de velocidad que crece sin límite (3 cm/s² son 3 cm/s más de
error por cada segundo). (2) es un **filtro complementario**: deja pasar lo rápido del
acelerómetro —arranques, frenadas, patinadas, choques— y toma del modelo de los motores
solo la tendencia lenta. Con τ = 1 s, un sesgo residual *b* deja como mucho *b*·τ de
error en la velocidad. (3), la *zero-velocity update*, es la corrección más fuerte que
existe: con los motores detenidos el robot está quieto, sin discusión.

**Qué se gana y qué no.** Lo que el MPU mejora es la dinámica: durante un arranque o
una frenada, o cuando el robot se traba contra algo con las llantas girando, la
velocidad del modelo es falsa y la del MPU no. En crucero, en cambio, la aceleración es
cero y el acelerómetro no tiene nada que aportar: la velocidad converge a la del modelo,
con su error de calibración. Por eso `ODOM_VEL_MAX_CM_S` se sigue midiendo en campo.

Sin MPU —si no contesta en el bus—, la odometría usa solo el modelo, como antes, y el
panel lo indica.

---

## 3. El tiempo antes de chocar

**Cada vez que el sensor apunta al frente (90°) y detecta un obstáculo**, el radar
congela tres cosas: la distancia medida *d*, el avance acumulado de la odometría en ese
instante y el rumbo. La estimación vigente es:

```
                 d − (avance_ahora − avance_al_medir)
 t_choque  =  ─────────────────────────────────────────
                          v_MPU ahora
```

- El **numerador** proyecta la distancia con lo que el robot avanzó desde la lectura.
  Entre dos lecturas frontales —que llegan cada ~1.2 s— la cuenta regresiva sigue
  bajando sola, en lugar de quedar congelada hasta la próxima vez que el sensor mire al
  frente.
- El **denominador** es la velocidad **de ahora**: si el robot frena, el riesgo
  desaparece en el acto.

La estimación se anula (`−1`, "sin riesgo") si:

| Condición | Por qué |
|---|---|
| La última lectura frontal no tuvo eco | No hay nada al frente con qué chocar |
| El robot va a menos de 2 cm/s, o retrocede | No se está acercando |
| El robot giró más de 20° desde la lectura | Ese obstáculo ya no está al frente |
| La lectura tiene más de 2 s | El radar dejó de barrer: no hay con qué sostenerla |

Si la distancia proyectada llega a cero, el tiempo es 0.

---

## 4. Obstáculo y evasión

El hilo de navegación del servidor considera que hay un obstáculo al frente si pasa
cualquiera de dos cosas:

| Vía | Umbral | Cuándo es la que dispara |
|---|---|---|
| **Tiempo** | tiempo de choque < `TTC_OBSTACULO_S` (1.2 s) | En movimiento: a 30 cm/s —la velocidad fija de ahora— salta a ~36 cm de la pared, antes que la distancia |
| **Distancia** | alguna lectura del cono frontal (60°, 90°, 120°) < `DIST_OBSTACULO_CM` (20 cm) | Robot quieto o muy lento, donde no hay tiempo de choque, y obstáculos que el sensor ve en diagonal |

Del cono frontal solo cuentan lecturas de menos de 2 s **tomadas mirando hacia donde
el robot mira ahora** (menos de 20° de diferencia de rumbo): tras un giro, lo que había
"al frente" es otra cosa.

Al **aparecer** el obstáculo —en cualquier modo— el robot se detiene, suena el aviso y se
enciende el LED rojo. En modo manual eso es un freno de seguridad. En modo autónomo
sigue la evasión:

1. **Retroceder** medio segundo.
2. **Mirar alrededor**: esperar un barrido completo del radar tomado ya quieto (~1 s).
3. **Elegir el rumbo** con más espacio entre los seis ángulos que no son el frente.
   Los que están a menos de 25 cm del mejor compiten **al azar**: es la parte aleatoria
   del algoritmo de rebote, que evita repetir siempre la misma trayectoria. Si ningún
   ángulo tiene 40 cm libres, media vuelta.
4. **Girar en lazo cerrado** sobre el rumbo de la odometría —el giroscopo— hasta cubrir
   el ángulo elegido, no durante un tiempo fijo. El giro es sobre el eje: una llanta
   hacia adelante y la otra hacia atrás.

Durante toda la maniobra el servidor sigue actualizando sensores, mapa y LED: el panel no
se congela mientras el robot retrocede o gira, y pasar a manual aborta la maniobra.

---

## 5. El mapa

Grilla de 31 × 31 celdas de **30 cm** (9.3 m de lado), con el robot empezando en el
centro. 30 cm es del orden del diámetro del robot: "celda visitada" es "el robot pasó
por acá".

| Estado | Cuándo |
|---|---|
| **Visitada** | El centro del robot estuvo en la celda |
| **Obstáculo** | Un eco del radar cayó en la celda |
| **Desconocida** | Todo lo demás |

Cada lectura nueva del radar se proyecta **una sola vez**, desde el sensor —el eje del
servo, 10 cm por delante del centro— con la pose que tenía el robot al medirla:

- el **eco** marca su celda como obstáculo, si está a menos de 150 cm (más lejos, el
  cono del HC-SR04 ya abarca varias celdas) y no cae en la celda del propio robot;
- las celdas que el **rayo cruza antes del eco** están libres: si alguna había quedado
  como obstáculo —un eco falso, algo que se movió— vuelve a desconocida;
- una lectura **sin eco** no despeja nada: puede ser una pared oblicua que desvió el
  pulso.

---

## 6. Evidencia en el simulador

Todo lo anterior corre sin la Raspberry sobre el simulador de `sim/`, que modela el
servo (600°/s), el HC-SR04 midiendo hacia donde apunta el servo en el instante del
disparo, el MPU-6050 con sesgo de fábrica y la inercia de las llantas.

`sim/prueba_librobot` — **47/47**, compilado para ARM y ejecutado con `qemu-aarch64`.
Estos números son con PWM (motores a 200/255); con la velocidad fija de ahora la prueba
también pasa 47/47, con el robot a 28 cm/s:

| Prueba | Resultado |
|---|---|
| Barrido completo contra el mundo | los 7 ángulos a menos de 2 cm de la distancia real |
| Eco frontal proyectado con `radar_rayo()` | cae sobre la pared (y = 200.0 cm) |
| Calibración del MPU | quita 0.02 g de sesgo del acelerómetro y 1.5 °/s del giroscopo |
| Velocidad al arrancar (100 ms) | real 12.4 · **MPU 13.8** · modelo de motores 23.5 cm/s |
| Velocidad en crucero | real 22.0 · MPU 23.8 cm/s (el resto es el error de calibración del modelo) |
| Detenido | 0.00 cm/s (ZUPT) |
| Tiempo de choque contra una pared a 137 cm | 5.61 s estimado, 6.21 s real; medio segundo después, 5.21 s |
| Odometría en L | 5 % de error de posición y **0.4° de rumbo** (antes: 8 % y 12°) |

El servidor completo, compilado para ARM y corriendo sobre el simulador:

| Prueba | Resultado |
|---|---|
| 2 minutos en modo autónomo, con PWM a 210/255 | **14 evasiones y ningún choque**; 59 celdas visitadas |
| 2 minutos en modo autónomo, a velocidad fija (máxima) | **18 evasiones y ningún choque**; 68 celdas visitadas |
| Modo manual, avance directo contra una pared | el tiempo de choque cae bajo 1.2 s y el robot frena solo a 26 cm |

---

## 7. Constantes a ajustar en campo

| Constante | Archivo | Valor | Qué fija |
|---|---|---|---|
| `SERVO_PULSO_0_US`, `SERVO_PULSO_180_US` | `lib/lib_servo.h` | 500, 2500 | Pulsos de los extremos; de ellos sale el de 90° |
| `SERVO_MS_POR_GRADO` | `lib/lib_servo.h` | 1.7 | Espera antes de medir |
| `SERVO_DIVISOR_VELOCIDAD` | `lib/lib_servo.h` | 3 | El servo barre a 1/3 de su velocidad máxima |
| `RADAR_EJE_ADELANTE_CM` | `lib/lib_radar.h` | 10 | Dónde está el sensor respecto del centro |
| `IMU_SIGNO_AVANCE`, `IMU_SIGNO_GIRO` | `lib/lib_imu.h` | +1, −1 | Orientación del MPU en el chasis |
| `ODOM_TAU_FUSION_S` | `lib/lib_odom.h` | 1.0 s | Cuánto se confía en el MPU frente al modelo |
| `ODOM_VEL_MAX_CM_S`, `ODOM_ENTRE_EJES_CM` | `lib/lib_odom.h` | 30, 15 | Modelo de los motores |
| `TTC_OBSTACULO_S` | `server/src/main.c` | 1.2 s | Anticipación de la evasión |
| `DIST_OBSTACULO_CM` | `server/src/main.c` | 20 cm | Obstáculo cercano con el robot lento |
| `MAP_CELDA_CM` | `lib/robot_state.h` y `server/src/robot_state.h` | 30 cm | Lado de la celda del mapa |

El procedimiento está en [`odometria.md`](odometria.md).
