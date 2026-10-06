# API de `librobot` — biblioteca dinámica de control

`librobot` (`librobot.so.1`) es la **única vía de acceso al hardware** del robot.
El servidor web y cualquier otro programa la usan para mover motores, leer el
radar y el MPU-6050, encender LEDs, reproducir audio y consultar la odometría;
nadie toca GPIO directamente (lo exige el enunciado).

- **Enlace:** `-lrobot` · **Headers:** `#include <robot/lib_robot.h>` (y los demás bajo `robot/`)
- **Versionado:** `SONAME librobot.so.1` (API estable en la serie 1.x)
- **Dependencias de runtime:** el demonio `pigpiod` (GPIO, pulsos del servo e I2C), `mpg123` y `alsa-lib` (audio)
- **Hilos propios:** `robot_init()` arranca dos — la odometría a 50 Hz y el barrido del radar — además del de reproducción de audio.
- **Concurrencia:** la odometría, el radar, el MPU-6050 y el audio son seguros para llamarse desde varios hilos; motores y LEDs asumen un solo hilo de control (el lazo de navegación).

Toda función que devuelve `int` usa **0 = éxito, −1 = error** salvo que se indique
otra cosa; las lecturas de distancia devuelven `−1.0` ante timeout.

---

## 1. Fachada del hardware — `lib_robot.h`

Puerta de entrada única. Abre la sesión con `pigpiod` e inicializa todo de una
sola llamada, en este orden: motores detenidos, LEDs, MPU-6050 (con su
calibración de medio segundo, **con el robot quieto**), odometría y radar.

| Función | Descripción | Retorno |
|---|---|---|
| `int robot_init(void)` | Abre `pigpiod` e inicializa todo el hardware; lanza los hilos de la odometría y del radar. Si el MPU-6050 no contesta, sigue sin él y lo avisa por `stderr`. | `0` ok · `−1` si no conecta a `pigpiod` |
| `void robot_shutdown(void)` | Detiene el radar (suelta el servo) y la odometría, frena los motores, libera el I2C, apaga LEDs y cierra la sesión. Idempotente y segura sin `init` previo. | — |
| `int robot_activo(void)` | ¿Hay sesión abierta? | `1` sí · `0` no |
| `double robot_distancia_frontal(void)` | Última lectura del radar a 90° (al frente), en cm. | cm · `−1.0` sin eco o sin medir |
| `double robot_distancia_izquierda(void)` | Última lectura del radar a 180°, en cm. | cm · `−1.0` sin eco o sin medir |
| `double robot_distancia_derecha(void)` | Última lectura del radar a 0°, en cm. | cm · `−1.0` sin eco o sin medir |

Las tres distancias se refrescan una vez por barrido (~1.2 s). El barrido completo, con
la pose de cada lectura, y el tiempo antes de chocar están en `lib_radar.h`.

**Manejo de errores y liberación de recursos:** `robot_init` falla limpio si
`pigpiod` no está (devuelve −1 sin dejar estado a medias); `robot_shutdown`
detiene los hilos, libera el GPIO y el I2C, y puede llamarse siempre. Patrón de uso:

```c
if (robot_init() != 0) return 1;          // pigpiod no disponible
double d = robot_distancia_frontal();
if (d < 0) { /* sin eco: tratar como camino libre */ }
robot_shutdown();                          // al terminar, siempre
```

---

## 2. Motores — `lib_motors.h`

Tracción diferencial sobre un puente L298N con los jumpers `ENA`/`ENB` puestos, que se
controla solo con las entradas `IN1`–`IN4`. Para cada motor, el signo elige cuál de sus
dos entradas se activa y la otra queda en bajo; con velocidad 0 las dos quedan en bajo y
el L298N **frena**. La inversión de los optoacopladores está compensada adentro
(`OPTO_INVERTIDO`). `MOTOR_PWM_MAX` (255) es el tope de velocidad.

> **La velocidad es fija, sin PWM** (`MOTOR_VELOCIDAD_VARIABLE` en 0, en
> `lib_motors.h`): cualquier velocidad distinta de cero lleva el motor al máximo,
> `motores_get()` informa ±255 y `motores_curva()` gira sobre el eje, con una llanta
> hacia adelante y la otra hacia atrás. Es una desviación del enunciado acordada con el
> profesor; el motivo está en
> [`hardware-aislamiento.md`](hardware-aislamiento.md#velocidad-fija). Con 1 la
> biblioteca usa PWM sobre `IN1`–`IN4`, un modo que solo se probó en el simulador.

| Función | Descripción |
|---|---|
| `void motores_set(int izq, int der)` | **Primitiva.** Velocidad de cada motor, −255..255; el signo da el sentido; la magnitud es el ciclo de PWM solo en el modo de velocidad variable. Se satura. |
| `void motores_get(int *izq, int *der)` | Velocidad que recibe cada motor: la ordenada ya saturada, o ±255 con velocidad fija (entrada de la odometría). Punteros NULL permitidos. |
| `void motores_detener(void)` | Frena ambos en seco (equivale a `motores_set(0,0)`). |
| `void motores_avanzar(int v)` | `motores_set(v, v)`. |
| `void motores_retroceder(int v)` | `motores_set(-v, -v)`. |
| `void motores_girar_izquierda(int v)` | Gira sobre su eje a la izquierda: `motores_set(-v, v)`. |
| `void motores_girar_derecha(int v)` | Gira sobre su eje a la derecha: `motores_set(v, -v)`. |
| `void motores_curva(int v, int giro)` | Avanza en curva sin detenerse. `giro` −100..100 (0 = recto); reduce el motor interior. Con velocidad fija, gira sobre el eje hacia ese lado. |

```c
motores_set(200, 120);      // curva suave a la derecha
motores_curva(200, 100);    // gira apoyándose en la llanta interior detenida
motores_detener();
```

---

## 3. Sensor ultrasónico — `lib_sensors.h`

Lectura de un HC-SR04 por eco. La usa el radar; la API de bajo nivel queda expuesta
para pruebas.

| Tipo / Función | Descripción |
|---|---|
| `struct SensorUltrasonico { int pinTrigger, pinEcho; }` | Pines BCM de un sensor. |
| `void sensor_init(int pi, SensorUltrasonico *s, int trig, int echo)` | Configura los pines del sensor. |
| `double sensor_leer_distancia(SensorUltrasonico *s)` | Distancia en cm, o `−1.0` si el eco no vuelve dentro del tiempo de espera. |

---

## 4. Servo del radar — `lib_servo.h`

Servo de 180° en GPIO 25. Ángulos: **0 = derecha, 90 = frente, 180 = izquierda**. Los
pulsos los genera `pigpiod` por DMA a 50 Hz. Mientras el radar barre, el servo es suyo:
para moverlo a mano, pausar antes el radar.

| Función | Descripción | Retorno |
|---|---|---|
| `void servo_init(int pi)` | Configura el pin. No mueve el servo. | — |
| `int servo_mover(int grados)` | Lleva el servo a `grados` (0–180, se satura) en rampa, a 1/3 de su velocidad máxima. Bloquea mientras dura la rampa. | ms que faltan para que llegue y se asiente · `−1` error |
| `int servo_angulo(void)` | Último ángulo ordenado. | grados · `−1` desconocido |
| `void servo_liberar(void)` | Deja de mandar pulsos: el servo queda suelto. | — |

Constantes a calibrar: `SERVO_PULSO_0_US`, `SERVO_PULSO_180_US` (µs de los extremos) y
`SERVO_MS_POR_GRADO` (velocidad, para saber cuánto esperar). `SERVO_DIVISOR_VELOCIDAD`
(3) fija cuánto más lento que su máximo se mueve el servo.

---

## 5. Radar — `lib_radar.h`

Un hilo propio barre el servo de 0° a 180° y de vuelta, en pasos de 30°
(`RADAR_N_ANGULOS` = 7), y dispara el HC-SR04 en cada parada: ~5 lecturas por segundo,
la frontal cada ~1.2 s. De cada ángulo guarda la última lectura con la **pose del robot
al medir**. Cada lectura frontal con eco actualiza el **tiempo antes de chocar**.

```c
typedef struct {
    int      angulo;        /* 0..180 */
    double   distancia_cm;  /* -1 sin eco */
    double   edad_s;        /* segundos desde la medicion; -1 nunca medido */
    uint32_t seq;           /* numero de lectura; 0 = nunca medido */
    double   x_cm, y_cm, rumbo_grados;   /* pose del robot al medir */
} RadarLectura;

typedef struct {
    int    hay_obstaculo;   /* la ultima lectura frontal vio algo */
    double distancia_cm;    /* distancia de esa lectura */
    double velocidad_cm_s;  /* velocidad del robot al medirla */
    double edad_s;
    double ttc_s;           /* estimacion vigente; -1 = sin riesgo */
} RadarChoque;
```

| Función | Descripción | Retorno |
|---|---|---|
| `int radar_iniciar(int pi)` | Configura sensor y servo y lanza el hilo. Necesita la odometría corriendo (lo hace `robot_init`). | `0`/`−1` |
| `void radar_detener(void)` | Detiene el barrido, suelta el servo, deja `TRIG` en bajo. | — |
| `void radar_pausar(int p)` | Pausa (`1`) o reanuda (`0`) el barrido tras la medición en curso. | — |
| `int radar_angulo_actual(void)` | Adónde apunta el servo. | grados |
| `int radar_lecturas(RadarLectura *out, int max)` | Copia la última lectura de cada ángulo, de 0° a 180°. | cuántas copió |
| `double radar_distancia(int angulo)` | Última distancia en un ángulo múltiplo de 30. | cm · `−1` |
| `uint32_t radar_seq(void)` | Número de la última lectura: marca para `radar_barrido_completo`. | — |
| `int radar_barrido_completo(uint32_t desde)` | ¿Todos los ángulos tienen una lectura posterior a `desde`? | `1`/`0` |
| `int radar_esperar_barrido(int timeout_ms)` | Bloquea hasta que todos los ángulos se midan de nuevo. | `0` · `−1` timeout |
| `double radar_tiempo_choque(void)` | Segundos antes de chocar con lo que hay al frente. | s · `−1` sin riesgo |
| `void radar_estado_choque(RadarChoque *out)` | El detalle: la lectura frontal que lo originó y la estimación. | — |
| `void radar_rayo(const RadarLectura *l, double *ox, double *oy, double *rumbo)` | Origen (el sensor, `RADAR_EJE_ADELANTE_CM` delante del centro) y rumbo de brújula del rayo. El eco está en `origen + d·(sin, cos)`. | — |

**Tiempo antes de chocar:** `(d − avance desde la lectura) / velocidad actual`, con la
velocidad del MPU-6050. Vale `−1` si la última lectura frontal no tuvo eco, si el robot
va a menos de 2 cm/s o retrocede, si giró más de 20° desde la lectura o si la lectura
tiene más de 2 s. Explicado en [`navegacion-radar.md`](navegacion-radar.md).

```c
RadarLectura l[RADAR_N_ANGULOS];
int n = radar_lecturas(l, RADAR_N_ANGULOS);
for (int i = 0; i < n; i++) {
    double ox, oy, rumbo;
    radar_rayo(&l[i], &ox, &oy, &rumbo);
    if (l[i].distancia_cm > 0)
        marcar(ox + l[i].distancia_cm * sin(rumbo * M_PI / 180),
               oy + l[i].distancia_cm * cos(rumbo * M_PI / 180));
}
if (radar_tiempo_choque() >= 0 && radar_tiempo_choque() < 1.2) evadir();
```

---

## 6. MPU-6050 — `lib_imu.h`

Acelerómetro y giroscopio por I2C (`/dev/i2c-1`, dirección `0x68`), a través de
`pigpiod`. Solo lee y quita el sesgo; la integración vive en `lib_odom`.

```c
typedef struct {
    double avance_cm_s2;   /* aceleracion hacia adelante, sin sesgo */
    double lateral_cm_s2;  /* hacia la izquierda */
    double vertical_g;     /* ~1.0 apoyado en el piso */
    double giro_dps;       /* grados/s, positivo en sentido horario (como el rumbo) */
    double temperatura_c;
} ImuLectura;
```

| Función | Descripción | Retorno |
|---|---|---|
| `int imu_init(int pi)` | Abre el sensor, lo despierta y lo configura (±2 g, ±250 °/s, pasabajos de 10 Hz). Acepta clones con otro `WHO_AM_I`, con aviso. | `0` · `−1` si nadie contesta |
| `int imu_calibrar(int muestras)` | Promedia `muestras` lecturas **con el robot quieto** y las toma como el cero. | `0`/`−1` |
| `int imu_leer(ImuLectura *out)` | Una lectura en unidades físicas y sin sesgo. | `0`/`−1` |
| `int imu_disponible(void)` | ¿Se encontró el sensor? | `1`/`0` |
| `void imu_cerrar(void)` | Libera el bus. | — |

Orientación: `IMU_SIGNO_AVANCE` e `IMU_SIGNO_GIRO` suponen X hacia el frente y Z hacia
arriba.

---

## 7. LEDs indicadores — `lib_leds.h`

Los cuatro LEDs obligatorios. IDs en el enum `LedId`:
`LED_POWER`, `LED_AUTONOMOUS`, `LED_MANUAL`, `LED_OBSTACLE`.

| Función | Descripción | Retorno |
|---|---|---|
| `int lib_leds_init(int pi)` | Configura los pines de los LEDs. | `0`/`−1` |
| `void lib_leds_set(LedId led, int state)` | Enciende (`1`) o apaga (`0`) un LED. | — |
| `int lib_leds_get(LedId led)` | Estado actual del LED. | `1`/`0` |
| `void lib_leds_sync_from_state(void)` | Refleja en los LEDs físicos el estado global del robot. | — |
| `void lib_leds_destroy(void)` | Apaga todos y libera los pines. | — |

Pines BCM: POWER 16, AUTONOMOUS 20, MANUAL 21, OBSTACLE 26.

---

## 8. Odometría — `lib_odom.h`

Estima posición, rumbo y velocidad por **navegación a la estima** (el chasis no tiene
encoders). Con MPU-6050, la velocidad es la aceleración integrada —corregida por el
modelo de los motores con un filtro complementario y forzada a cero con los motores
detenidos— y el rumbo es el giroscopio integrado. Sin MPU, todo sale del modelo de
tracción diferencial. Corre en su propio hilo a 50 Hz. Sirve para el mapa y para el
tiempo de choque, no para posición absoluta. Seguro para varios hilos.

| Función | Descripción |
|---|---|
| `void odom_init(void)` | Arranca en el origen. Idempotente. |
| `int odom_arrancar(void)` / `void odom_parar(void)` | Lanza / detiene el hilo de 50 Hz. `robot_init` y `robot_shutdown` lo hacen. |
| `void odom_reset(void)` | Reinicia en el origen, quieto y mirando al norte. |
| `void odom_update(void)` | Integra el movimiento desde la llamada anterior (paso = tiempo real, `CLOCK_MONOTONIC`). La llama el hilo; llamarla además no hace daño. |
| `void odom_get(double *x_cm, double *y_cm, double *rumbo_grados)` | Posición estimada. X este+, Y norte+, rumbo brújula (0 N, 90 E, 180 S, 270 O). NULL permitido. |
| `double odom_velocidad_cm_s(void)` | **Velocidad hacia adelante** en cm/s (negativa al retroceder): la del MPU. |
| `double odom_avance_cm(void)` | Avance neto acumulado, con signo: retroceder resta. |
| `double odom_distancia_recorrida(void)` | Camino total en cm. |
| `void odom_get_velocidades(double *izq, double *der)` | Velocidad del modelo de cada llanta en cm/s. |
| `int odom_usa_imu(void)` | ¿La última integración usó el MPU? |

**Constantes a calibrar en campo:** `ODOM_VEL_MAX_CM_S` (cm/s a velocidad máxima),
`ODOM_ENTRE_EJES_CM` (separación de llantas), `ODOM_PWM_ARRANQUE` (PWM mínimo
que vence la fricción; no interviene con velocidad fija) y `ODOM_TAU_FUSION_S` (cuánto se confía en el MPU).

---

## 9. Audio — `lib_audio.h`

Reproduce MP3 locales (mpg123 + ALSA) por la salida PWM analógica de la RPi4, que el
overlay `audremap` saca por GPIO 18 hacia el amplificador PAM8403, en un hilo propio,
concurrente con la navegación. Con `LIB_AUDIO_MONO` cada pista se mezcla a mono: el
robot tiene un solo parlante. Volumen 0–100.

| Función | Descripción |
|---|---|
| `int lib_audio_init(const char *dir)` | Inicializa el subsistema y escanea `dir` (NULL = `./audio`). Lanza el hilo de reproducción. |
| `void lib_audio_destroy(void)` | Detiene y libera. |
| `int lib_audio_scan(void)` | Reescanea el directorio y sus subcarpetas directas (sin `notify_*`), ordena por nombre; devuelve el número de pistas. |
| `int lib_audio_get_tracks(LibAudioTrack *out, int max)` | Copia hasta `max` pistas; devuelve cuántas. |
| `int lib_audio_play(int track_id)` | Reproduce la pista **en bucle** hasta `stop` u otro `play`. |
| `void lib_audio_pause/resume/stop(void)` | Control de reproducción. |
| `int lib_audio_seek(float segundos)` | Salta dentro de la pista actual, sonando o en pausa (en pausa sigue en pausa). Se recorta a medio segundo antes del final; −1 si no hay pista cargada. |
| `void lib_audio_set_volume(int v)` / `int lib_audio_get_volume(void)` | Volumen 0–100. |
| `LibAudioStatus lib_audio_get_status(void)` | `STOPPED` / `PLAYING` / `PAUSED`. |
| `int lib_audio_get_current_id(void)` | Pista actual, o −1. |
| `float lib_audio_get_position(void)` | Posición en segundos. |
| `void lib_audio_notify(NotificationEvent e)` | Sonido de evento (`NOTIFY_STARTUP/AUTONOMOUS/OBSTACLE/MANUAL/CYCLE_END`). **Pausa la música, reproduce el aviso y la reanuda** — no la corta. Un `stop` pendiente no se pisa: la música no vuelve. |
| `int lib_audio_playlist_get(int *ids, int max)` | Ids de la playlist persistente, en orden; devuelve cuántos copió. |
| `int lib_audio_playlist_set(const int *ids, int n)` | Reemplaza la playlist y la guarda en `canciones/playlist.txt` (temporal + `rename`). −1 si un id no existe o no se pudo escribir. |
| `int lib_audio_play_playlist(int pos)` | Recorre la playlist desde `pos`, en orden y en bucle. |
| `int lib_audio_playlist_pos(void)` | Posición de la playlist que suena, o −1 si suena una pista suelta. |

---

## 10. Desnivel — `lib_caida.h`

Dos sensores infrarrojos al piso (TCRT5000 o FC-51) en las esquinas delanteras, en
GPIO 4 (izquierda) y 8 (derecha). Requerimiento opcional; ver
[`opcionales.md`](opcionales.md).

| Función | Descripción |
|---|---|
| `int caida_init(int pi)` | Configura los dos GPIO como entradas con pull-down: un módulo desconectado se lee como "hay piso". La llama `robot_init()`. |
| `int caida_leer(void)` | Bits `CAIDA_IZQ` y `CAIDA_DER` de los sensores que no ven piso; 0 si hay piso bajo los dos. Cada sensor se lee dos veces, a 200 µs, para filtrar ruido. |

---

## Ejemplo mínimo de integración

```c
#include <robot/lib_robot.h>
#include <robot/lib_motors.h>
#include <robot/lib_odom.h>
#include <robot/lib_radar.h>

int main(void) {
    if (robot_init() != 0) return 1;       // el robot quieto: calibra el MPU
    while (trabajando) {
        double f   = robot_distancia_frontal();
        double ttc = radar_tiempo_choque();
        if ((f > 0 && f < 20.0) || (ttc >= 0 && ttc < 1.2)) {
            motores_detener();              /* evadir */
        } else {
            motores_avanzar(200);
        }
        printf("v = %.1f cm/s\n", odom_velocidad_cm_s());   // la del MPU
        usleep(100000);                    // la odometría y el radar corren solos
    }
    robot_shutdown();
    return 0;
}
```

> Esta referencia se enlaza desde el README. La prueba `sim/prueba_librobot`
> ejercita cada módulo de esta API contra un robot simulado (47/47).
