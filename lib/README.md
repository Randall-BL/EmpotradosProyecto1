# `lib/` — Biblioteca dinámica `librobot.so`

Única vía de acceso al hardware del robot. El servidor web **no** toca GPIO
directamente: todo pasa por esta biblioteca.

| Módulo | Responsabilidad |
|---|---|
| `lib_motors.{c,h}`  | Motores DC vía PWM (avance, retroceso, giros, velocidad por motor) |
| `lib_sensors.{c,h}` | Sensores de proximidad HC-SR04 por GPIO |
| `lib_leds.{c,h}`    | Los 4 LEDs indicadores |
| `lib_audio.{c,h}`   | Reproducción MP3 y sonidos de evento |
| `robot_state.h`     | Estado compartido entre hilos |

Se compila de forma cruzada para ARM con CMake, tanto desde la receta
`librobot_1.0.bb` como manualmente con el Toolchain-SDK.
Ver [`docs/sdk.md`](../docs/sdk.md).
