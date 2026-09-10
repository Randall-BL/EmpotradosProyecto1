# Evidencia — Servidor web de control (issues #25–#29)

El servidor `robot-server` se probó **corriendo sobre el simulador** en la
laptop (sin la Raspberry): `sim/construir_servidor.sh` compila el servidor real
enlazado contra el simulador de GPIO, y se ejercitó cada endpoint por HTTP con
`curl`. Reproducible con `sim/prueba_api.sh`.

Credenciales de prueba: **`user1` / `password1`**.

## Autenticación (#25)

```
GET  /                           -> HTTP 200 (página de login)
POST /api/login  (mal password)  -> HTTP 401   ← rechaza credenciales inválidas
GET  /api/status (sin sesión)    -> HTTP 401   ← endpoints protegidos
POST /api/login  (user1 correcto)-> HTTP 200   {"ok":true,"username":"user1"}
```

Login con usuario registrado, hash SHA-256 + sesión por cookie, y todo endpoint
de control exige sesión. El servidor usa **exclusivamente** `librobot` para el
hardware (ver `docs/evidencias/` y `readelf`: no enlaza pigpio).

## Panel de control (#26) — `GET /api/status`

```json
{
 "mode": "autonomous",
 "uptime": 82,
 "sensors": { "front": 68.7, "back": 0.0, "left": 156.0, "right": 65.9 },
 "leds":    { "power": true, "autonomous": true, "manual": false, "obstacle": false },
 "audio":   { "status": 0, "current_track_id": -1, "position": 0.0, "volume": 55 },
 "map":     { "robot_x": 2, "robot_y": 11, "robot_heading": 252,
              "visited": 79, "obstacles": 86, "grid": "…31x31…" }
}
```

Un solo endpoint entrega modo, sensores en tiempo real, estado de los 4 LEDs,
estado de audio y el mapa. Conmutación de modo y control manual:

```
POST /api/mode {manual}   -> {"ok":true}
POST /api/move {forward}  -> {"ok":true}
POST /api/move {left}     -> {"ok":true}
```

## Control de audio desde la interfaz (#27)

```
GET  /api/audio/list           -> {"tracks":[...]}
POST /api/audio/volume {55}    -> {"ok":true}
POST /api/audio/control {stop} -> {"ok":true}
```

Listar, seleccionar, reproducir/pausar/detener y volumen, todo desde la web.
(La salida **audible** necesita la tarjeta de sonido del robot; ver #23.)

## Mapa 2D incremental en tiempo real (#28, #29)

Con el robot en modo autónomo, consultando `/api/status` cada 2 s:

```
  t+0s  robot=( 2,11) rumbo=252  visitadas=79 obstaculos=86
  t+2s  robot=( 0,12) rumbo=238  visitadas=82 obstaculos=86
  t+4s  robot=( 0,12) rumbo=130  visitadas=82 obstaculos=86   ← trabado: gira para evadir
  t+6s  robot=( 2,14) rumbo=130  visitadas=85 obstaculos=90
```

La grilla (3 estados: desconocida/visitada/obstáculo) **crece en tiempo real**
conforme el robot avanza, y el rumbo cambia cuando encuentra un obstáculo — se
ve de paso la evasión reactiva (#20/#21). El dashboard (`server/www/dashboard.html`)
renderiza esta grilla en un `<canvas>` y la refresca sondeando `/api/status`.

## Qué queda para el hardware

- El servidor accesible **por WiFi desde el celular** depende de la red de la
  Pi (aquí se probó por HTTP en la laptop).
- La **prueba de campo** de la navegación (#20/#21) y el audio **audible** (#23)
  necesitan el robot armado.
