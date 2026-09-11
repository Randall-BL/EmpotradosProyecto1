# API de `librobot` — biblioteca dinámica de control

`librobot` (`librobot.so.1`) es la **única vía de acceso al hardware** del robot.
El servidor web y cualquier otro programa la usan para mover motores, leer
sensores, encender LEDs, reproducir audio y consultar la odometría; nadie toca
GPIO directamente (lo exige el enunciado).

- **Enlace:** `-lrobot` · **Headers:** `#include <robot/lib_robot.h>` (y los demás bajo `robot/`)
- **Versionado:** `SONAME librobot.so.1` (API estable en la serie 1.x)
- **Dependencias de runtime:** el demonio `pigpiod` (GPIO), `mpg123` y `alsa-lib` (audio)
- **Concurrencia:** la odometría y el audio son seguros para llamarse desde varios hilos; los módulos de motores/sensores/LEDs asumen un solo hilo de control (el lazo de navegación).

Toda función que devuelve `int` usa **0 = éxito, −1 = error** salvo que se indique
otra cosa; las lecturas de distancia devuelven `−1.0` ante timeout.

---

## 1. Fachada del hardware — `lib_robot.h`

Puerta de entrada única. Abre la sesión con `pigpiod` e inicializa motores,
sensores, LEDs y odometría de una sola llamada.

| Función | Descripción | Retorno |
|---|---|---|
| `int robot_init(void)` | Abre `pigpiod` e inicializa todo el hardware. Deja motores detenidos, sensores listos, LEDs apagados y odometría en el origen. | `0` ok · `−1` si no conecta a `pigpiod` |
| `void robot_shutdown(void)` | Detiene motores, apaga LEDs y cierra la sesión. Idempotente y segura sin `init` previo. | — |
| `int robot_activo(void)` | ¿Hay sesión abierta? | `1` sí · `0` no |
| `double robot_distancia_frontal(void)` | Distancia del sensor frontal, en cm. | cm · `−1.0` timeout |
| `double robot_distancia_izquierda(void)` | Distancia del sensor lateral izquierdo, en cm. | cm · `−1.0` timeout |
| `double robot_distancia_derecha(void)` | Distancia del sensor lateral derecho, en cm. | cm · `−1.0` timeout |

**Manejo de errores y liberación de recursos:** `robot_init` falla limpio si
`pigpiod` no está (devuelve −1 sin dejar estado a medias); `robot_shutdown`
libera el GPIO y puede llamarse siempre. Patrón de uso:

```c
if (robot_init() != 0) return 1;          // pigpiod no disponible
double d = robot_distancia_frontal();
if (d < 0) { /* sin eco: tratar como camino libre */ }
robot_shutdown();                          // al terminar, siempre
```

---

## 2. Motores — `lib_motors.h`

Tracción diferencial sobre un puente L298N. `MOTOR_PWM_MAX` (255) es el tope de
duty cycle.

| Función | Descripción |
|---|---|
| `void motores_set(int izq, int der)` | **Primitiva.** Velocidad de cada motor, −255..255; el signo da el sentido, la magnitud el PWM. Se satura. |
| `void motores_get(int *izq, int *der)` | Última velocidad ordenada a cada motor (entrada de la odometría). Punteros NULL permitidos. |
| `void motores_detener(void)` | Frena ambos (equivale a `motores_set(0,0)`). |
| `void motores_avanzar(int v)` | `motores_set(v, v)`. |
| `void motores_retroceder(int v)` | `motores_set(-v, -v)`. |
| `void motores_girar_izquierda(int v)` | Gira sobre su eje a la izquierda: `motores_set(-v, v)`. |
| `void motores_girar_derecha(int v)` | Gira sobre su eje a la derecha: `motores_set(v, -v)`. |
| `void motores_curva(int v, int giro)` | Avanza en curva sin detenerse. `giro` −100..100 (0 = recto); reduce el motor interior. |

```c
motores_set(200, 120);      // curva suave a la derecha
motores_curva(200, 100);    // gira apoyándose en la llanta interior detenida
motores_detener();
```

---

## 3. Sensores de proximidad — `lib_sensors.h`

HC-SR04 por eco ultrasónico. Normalmente se usan a través de la fachada
(`robot_distancia_*`); la API de bajo nivel queda expuesta para pruebas.

| Tipo / Función | Descripción |
|---|---|
| `struct SensorUltrasonico { int pinTrigger, pinEcho; }` | Pines BCM de un sensor. |
| `void sensor_init(int pi, SensorUltrasonico *s, int trig, int echo)` | Configura los pines del sensor. |
| `double sensor_leer_distancia(SensorUltrasonico *s)` | Distancia en cm, o `−1.0` si el eco no vuelve dentro del tiempo de espera. |

---

## 4. LEDs indicadores — `lib_leds.h`

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

## 5. Odometría — `lib_odom.h`

Estima posición y rumbo por **navegación a la estima** (el chasis no tiene
encoders): integra la velocidad ordenada a los motores con el modelo de
tracción diferencial. Sirve para el mapa de recorrido, no para posición
absoluta. Seguro para varios hilos.

| Función | Descripción |
|---|---|
| `void odom_init(void)` | Arranca en el origen. Idempotente. |
| `void odom_reset(void)` | Reinicia en el origen mirando al norte. |
| `void odom_update(void)` | Integra el movimiento desde la llamada anterior (paso = tiempo real, `CLOCK_MONOTONIC`). Se llama en el lazo de navegación. |
| `void odom_get(double *x_cm, double *y_cm, double *rumbo_grados)` | Posición estimada. X este+, Y norte+, rumbo brújula (0 N, 90 E, 180 S, 270 O). NULL permitido. |
| `double odom_distancia_recorrida(void)` | Camino total en cm. |
| `void odom_get_velocidades(double *izq, double *der)` | Velocidad instantánea de cada llanta en cm/s. |

**Constantes a calibrar en campo:** `ODOM_VEL_MAX_CM_S` (cm/s a PWM máximo),
`ODOM_ENTRE_EJES_CM` (separación de llantas), `ODOM_PWM_ARRANQUE` (PWM mínimo
que vence la fricción).

---

## 6. Audio — `lib_audio.h`

Reproduce MP3 locales por el jack de 3.5 mm (mpg123 + ALSA), en un hilo propio,
concurrente con la navegación. Volumen 0–100.

| Función | Descripción |
|---|---|
| `int lib_audio_init(const char *dir)` | Inicializa el subsistema y escanea `dir` (NULL = `./audio`). Lanza el hilo de reproducción. |
| `void lib_audio_destroy(void)` | Detiene y libera. |
| `int lib_audio_scan(void)` | Reescanea el directorio; devuelve el número de pistas. |
| `int lib_audio_get_tracks(LibAudioTrack *out, int max)` | Copia hasta `max` pistas; devuelve cuántas. |
| `int lib_audio_play(int track_id)` | Reproduce la pista. |
| `void lib_audio_pause/resume/stop(void)` | Control de reproducción. |
| `void lib_audio_set_volume(int v)` / `int lib_audio_get_volume(void)` | Volumen 0–100. |
| `LibAudioStatus lib_audio_get_status(void)` | `STOPPED` / `PLAYING` / `PAUSED`. |
| `int lib_audio_get_current_id(void)` | Pista actual, o −1. |
| `float lib_audio_get_position(void)` | Posición en segundos. |
| `void lib_audio_notify(NotificationEvent e)` | Sonido de evento (`NOTIFY_STARTUP/AUTONOMOUS/OBSTACLE/MANUAL`). **Pausa la música, reproduce el aviso y la reanuda** — no la corta. |

---

## Ejemplo mínimo de integración

```c
#include <robot/lib_robot.h>
#include <robot/lib_motors.h>
#include <robot/lib_odom.h>

int main(void) {
    if (robot_init() != 0) return 1;
    while (trabajando) {
        double f = robot_distancia_frontal();
        if (f > 0 && f < 15.0) { motores_detener(); /* evadir */ }
        else                    motores_avanzar(200);
        odom_update();                 // mantener la odometría al día
    }
    robot_shutdown();
    return 0;
}
```

> Esta referencia se enlaza desde el README. La prueba `sim/prueba_librobot`
> ejercita cada función de esta API contra un robot simulado (18/18).
