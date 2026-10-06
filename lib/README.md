# `lib/` — Biblioteca dinámica `librobot.so`

Única vía de acceso al hardware del robot. El servidor web **no** toca GPIO
directamente: todo pasa por esta biblioteca.

| Módulo | Responsabilidad |
|---|---|
| `lib_robot.{c,h}`   | Fachada: abre `pigpiod` y arranca todo lo demás en orden |
| `lib_motors.{c,h}`  | Motores DC: avance, retroceso, giros y detención. Velocidad fija en el robot; PWM opcional al compilar |
| `lib_sensors.{c,h}` | Lectura del HC-SR04 por GPIO (eco ultrasónico) |
| `lib_servo.{c,h}`   | Servo de 180° que orienta el HC-SR04 |
| `lib_radar.{c,h}`   | Radar: barrido del servo + HC-SR04 en un hilo propio, y tiempo antes de chocar |
| `lib_imu.{c,h}`     | MPU-6050 por I2C: aceleración de avance y velocidad de giro |
| `lib_odom.{c,h}`    | Odometría: posición, rumbo y velocidad (MPU fusionado con el modelo de los motores) |
| `lib_leds.{c,h}`    | Los 4 LEDs indicadores |
| `lib_audio.{c,h}`   | Reproducción MP3 y sonidos de evento, mezclados a mono para un parlante |
| `robot_state.h`     | Estado compartido entre hilos (copia idéntica de la del servidor) |

Se compila de forma cruzada para ARM con CMake, tanto desde la receta
`librobot_1.0.bb` como manualmente con el Toolchain-SDK.
Ver [`docs/sdk.md`](../docs/sdk.md).
