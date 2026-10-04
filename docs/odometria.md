# Calibración en campo — motores, radar, MPU-6050 y odometría

Los módulos de control (`librobot`) están implementados y probados en el
simulador, pero varias cosas solo pueden **ajustarse con el robot armado**. Esta
es la guía para hacerlo cuando esté el kit. Todo se puede correr por SSH sobre
la imagen del robot. Qué hace cada constante está explicado en
[`navegacion-radar.md`](navegacion-radar.md).

## 1. Velocidades de los motores (issue #15)

> **Por ahora los motores van a velocidad fija** (`MOTOR_VELOCIDAD_VARIABLE` en 0,
> en `lib/lib_motors.h`): cualquier velocidad es la máxima y los pasos 1 y 3 no
> aplican. Lo que sí hay que medir es la velocidad a fondo, que es la
> `ODOM_VEL_MAX_CM_S` de la sección 5. Si se recupera la PWM, antes de estos
> pasos probar que el robot arranque con ciclos medios (ver
> [`hardware-aislamiento.md`](hardware-aislamiento.md#por-ahora-velocidad-fija)).

El PWM es de 0–255, pero la velocidad real depende de la batería, el peso y la
fricción. Objetivo: elegir una velocidad de crucero estable y una de giro que
no haga trompos.

1. Con el robot **elevado** (llantas sin tocar el piso), probar `motores_set`
   a 120, 160, 200, 240 y anotar que ambas llantas giren parejo. Si una gira
   más, corregir en el montaje o compensar en `motores_set`.
2. En el piso, medir con cinta el avance en 3 s a cada PWM → cm/s reales.
3. Fijar `VEL_CRUCERO` (en `server/src/main.c`) al PWM que dé ~20–25 cm/s, y
   `VEL_GIRO` al mínimo que complete un giro de 90° sin patinar.

## 2. Servo del radar

Los pulsos que llevan el servo a 0° y 180° varían entre unidades, y el
optoacoplador los alarga unas decenas de microsegundos. Se calibran moviendo el
servo a mano con `pigs` (que en desarrollo se agrega a la imagen, ver
[`hardware-pinout.md`](hardware-pinout.md)), **con el servidor detenido** para
que el barrido no pelee por el servo:

```bash
systemctl stop robot-server
pigs s 25 1500        # centro: el sensor tiene que mirar al frente
pigs s 25 500         # un extremo: 90° a la derecha
pigs s 25 2500        # el otro: 90° a la izquierda
pigs s 25 0           # soltar
systemctl start robot-server
```

1. **Centro.** Buscar, de 10 en 10 µs, el pulso que deja el sensor mirando
   **exactamente al frente**. Si no es 1500, correr `SERVO_PULSO_0_US` y
   `SERVO_PULSO_180_US` (en `lib/lib_servo.h`) en la misma cantidad; mover el
   brazo un diente del estriado solo si el error supera ~10°.
2. **Extremos.** En cada extremo el sensor debe quedar a 90° del frente **sin
   que el servo zumbe contra su tope**. Si zumba, acercar ese extremo al
   centro 50 µs por vez.
3. **Comprobación.** Con el servidor corriendo y el robot quieto frente a una
   pared, en el radar del panel web la lectura a 90° tiene que ser la más corta
   y las de 60° y 120°, parecidas entre sí. Si una diagonal es
   sistemáticamente más corta, el 90° no está centrado.

`SERVO_MS_POR_GRADO` fija cuánto se espera antes de medir. Si las lecturas de
un mismo ángulo saltan de una pasada a otra, subirlo: el sensor está midiendo
antes de que el servo termine de llegar.

## 3. Montaje del MPU-6050

Los signos de `lib/lib_imu.h` suponen el eje X hacia el frente y Z hacia
arriba:

1. **Montaje:** la serigrafía del GY-521 dibuja los ejes. La flecha **X hacia el
   frente** del robot y los componentes **hacia arriba**. Si por espacio se
   monta con X hacia atrás, `IMU_SIGNO_AVANCE` pasa a −1.
2. **Arranque:** encender el robot apoyado en el piso y sin tocarlo durante el
   primer segundo, y revisar la calibración:
   ```bash
   journalctl -u robot-server | grep "imu\] calibrado"
   #   ... sesgo ax +0.0123 g, ay -0.0040 g, gz +0.85 grados/s; vertical +1.00 g
   ```
   `vertical` en **+1 g** confirma que Z quedó hacia arriba; en −1 g el módulo
   está dado vuelta (el giro cambia de signo). Un sesgo de giro de más de unos
   pocos grados/s indica que el robot se movió durante la calibración.
3. **Giro:** con el servidor corriendo, girar el robot a mano hacia la derecha
   (sentido horario visto desde arriba). La flecha del robot en el mapa del
   panel tiene que girar igual. Si gira al revés, cambiar `IMU_SIGNO_GIRO`
   a +1.

La velocidad de avance no se puede comprobar empujando el robot a mano: con los
motores detenidos la odometría la fuerza a cero (ZUPT). Si el montaje cumple el
punto 1, el signo es el correcto.

## 4. Umbrales de obstáculo (issue #16)

El HC-SR04 lee bien de ~2 cm a ~4 m, pero los umbrales hay que ajustarlos a la
geometría del robot y a cuánto tarda en frenar.

1. Con el robot quieto, enfrentarlo a una pared a distancias medidas (10, 15,
   20, 30 cm) y comparar con la lectura "Frente" del radar en el panel web
   (`robot_distancia_frontal()`, que se refresca cada ~1.2 s). Debe caer
   dentro de ±1 cm.
2. Con el robot en movimiento a `VEL_CRUCERO`, medir la distancia de frenado.
3. Ajustar en `server/src/main.c`:
   - `TTC_OBSTACULO_S` (hoy 1.2 s): el robot tiene que detenerse con margen
     aun cuando la lectura frontal llega justo antes de disparar. Si frena
     demasiado cerca, subirlo.
   - `DIST_OBSTACULO_CM` (hoy 20 cm): un poco por encima de la distancia de
     frenado a baja velocidad.

## 5. Constantes de odometría (issue #22)

`lib_odom` estima la posición con el MPU-6050 y el modelo de los motores. Las
constantes del modelo (`lib/lib_odom.h`) hay que medirlas, porque en crucero la
velocidad converge a la del modelo:

- **`ODOM_VEL_MAX_CM_S`** — velocidad de una llanta a PWM 255. Medir avance en
  línea recta a PWM máximo durante 5 s y dividir entre el tiempo.
- **`ODOM_ENTRE_EJES_CM`** — distancia entre el centro de las dos llantas, con
  regla. Con MPU el rumbo sale del giroscopio, pero sin él se usa esta.
- **`ODOM_PWM_ARRANQUE`** — el PWM mínimo con el que las llantas empiezan a
  girar (por debajo, el motor zumba pero no mueve). Subir de 40 en 40 hasta que
  el robot arranque.
- **`ODOM_TAU_FUSION_S`** — rara vez hace falta tocarla. Si la velocidad del
  panel deriva con el robot en crucero, bajarla; si no sigue las frenadas,
  subirla.

**Validación:** mandar al robot a recorrer un cuadrado de 1 m de lado y comparar
la pose final que reporta `odom_get()` con la real. El error acumulado debería
quedar por debajo del ~15 % del perímetro; si es mayor, reajustar las constantes.

> Método de medición idéntico al que valida la odometría en el simulador
> (`sim/prueba_librobot`), donde el error medido es de 1–5 % en posición y de
> uno o dos grados en el rumbo.

## 6. Mapa

Cada celda mide `MAP_CELDA_CM` = 30 cm (en `lib/robot_state.h` y
`server/src/robot_state.h`, que deben ser idénticos). Para validar: recorrer en
manual una línea recta de 3 m medidos con cinta; en el panel el robot tiene que
avanzar 10 celdas. Si avanza de más o de menos, el error está en
`ODOM_VEL_MAX_CM_S`, no en el mapa.
