# Calibración en campo — motores, sensores y odometría

Los módulos de control (`librobot`) están implementados y probados en el
simulador, pero tres cosas solo pueden **ajustarse con el robot armado**. Esta
es la guía para hacerlo cuando esté el kit. Todo se puede correr por SSH sobre
la imagen del robot.

## 1. Velocidades de los motores (issue #15)

El PWM es de 0–255, pero la velocidad real depende de la batería, el peso y la
fricción. Objetivo: elegir una velocidad de crucero estable y una de giro que
no haga trompos.

1. Con el robot **elevado** (llantas sin tocar el piso), probar `motores_set`
   a 120, 160, 200, 240 y anotar que ambas llantas giren parejo. Si una gira
   más, corregir en el montaje o compensar en `motores_set`.
2. En el piso, medir con cinta el avance en 3 s a cada PWM → cm/s reales.
3. Fijar `vel_crucero` (en `server/src/main.c`) al PWM que dé ~15–20 cm/s, y
   `vel_giro` al mínimo que complete un giro de 90° sin patinar.

## 2. Umbrales de distancia de los sensores (issue #16)

El HC-SR04 lee bien de ~2 cm a ~4 m, pero el umbral de "obstáculo" hay que
ajustarlo a la geometría del robot (cuánto tarda en frenar).

1. Enfrentar el sensor a una pared a distancias medidas (10, 15, 20, 30 cm) y
   comparar con `robot_distancia_frontal()`. Debe caer dentro de ±1 cm.
2. Con el robot en movimiento a `vel_crucero`, medir la distancia de frenado.
3. Fijar el umbral de obstáculo (hoy `15.0` cm en `main.c`) un poco por encima
   de esa distancia de frenado, para que no choque.

## 3. Constantes de odometría (issue #22)

`lib_odom` estima la posición integrando la velocidad ordenada. Sus constantes
(`lib/lib_odom.h`) hay que medirlas:

- **`ODOM_VEL_MAX_CM_S`** — velocidad de una llanta a PWM 255. Medir avance en
  línea recta a PWM máximo durante 5 s y dividir entre el tiempo.
- **`ODOM_ENTRE_EJES_CM`** — distancia entre el centro de las dos llantas, con
  regla. Afecta cuánto rota el robot por diferencia de velocidades.
- **`ODOM_PWM_ARRANQUE`** — el PWM mínimo con el que las llantas empiezan a
  girar (por debajo, el motor zumba pero no mueve). Subir de 40 en 40 hasta que
  el robot arranque.

**Validación:** mandar al robot a recorrer un cuadrado de 1 m de lado y comparar
la pose final que reporta `odom_get()` con la real. El error acumulado debería
quedar por debajo del ~15 % del perímetro; si es mayor, reajustar las constantes.

> Método de medición idéntico al que valida la odometría en el simulador
> (`sim/prueba_librobot`), donde el error medido es del 8 %.
