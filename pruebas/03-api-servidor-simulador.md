# Prueba 03 — API del servidor sobre el simulador

| | |
|---|---|
| **Fecha** | 11 de octubre de 2026 |
| **Commit probado** | `b1187e1` (mismo árbol que `main` en `82f6472`), más la corrección de `sim/prueba_api.sh` de este mismo PR |
| **Entorno** | WSL2, Ubuntu 24.04.3 LTS, gcc 13.3.0, curl y Python 3.12.3 |
| **Tipo** | Ejecución real del servidor completo sobre el hardware simulado, ejercitado por HTTP |
| **Resultado** | **Falla con el script original. Pasa con la espera corregida**, en 3 de 3 corridas |

## Qué verifica

Compila el servidor completo (`server/src` más `lib/`) contra el simulador, lo lanza y
recorre por HTTP la autenticación, el panel, el cambio de modo, el movimiento, el
audio y el mapa incremental.

## Cómo se ejecuta

```sh
sudo apt install libmicrohttpd-dev libmpg123-dev libasound2-dev
cd sim
./construir_servidor.sh
./prueba_api.sh; echo "salida: $?"
```

Igual que en la prueba 02, aquí las bibliotecas de desarrollo se tomaron de los
paquetes `.deb` extraídos en una carpeta aparte, sin instalarlas en el sistema.

## Resultado con el script original: falla

`./construir_servidor.sh` terminó con código 0. `./prueba_api.sh` terminó con código 1
a los 3 s, sin llegar a hablar con el servidor:

```text
== Autenticacion (#25) ==
  GET /                 -> HTTP 000
  login mal password    -> HTTP 000
  status sin sesion     -> HTTP 000
  login correcto        -> HTTP 000
== Panel / modo / movimiento (#26) ==
json.decoder.JSONDecodeError: Expecting value: line 1 column 1 (char 0)
```

**Causa.** El script lanzaba el servidor y esperaba un tiempo fijo, `sleep 3`. En esta
ejecución el servidor tardó **3,6 s** en abrir el puerto. Las peticiones salían antes
de tiempo, `curl` devolvía `000` y el primer `json.load` abortaba el script. El
defecto es la espera fija del script de prueba, no el servidor.

El tiempo se fue en `lib_audio_init()`, que lee las pistas antes de que el servidor
empiece a escuchar, y esa lectura fue lenta **por el disco de esta ejecución**: la
carpeta de trabajo estaba en el disco de Windows visto desde WSL (`/mnt/c`, sistema de
archivos 9p). El mismo binario, con las 9 pistas versionadas:

| Carpeta de trabajo | Escaneo de audio | Puerto abierto a los |
|---|---|---|
| `/mnt/c`, disco de Windows desde WSL (9p) | 2,6 s | 3,2 s |
| Disco nativo de WSL (ext4) | 0,02 s | 0,59 s |

En la Raspberry Pi, el journal de `docs/evidencias/ejecucion-target.md` registra
0,44 s para ese mismo escaneo de 9 pistas. Sobre un disco Linux el servidor escucha en
menos de 1 s y el `sleep 3` original alcanzaba, lo que coincide con las evidencias
anteriores del repositorio.

En el disco lento, el costo crece con el tamaño de las pistas:

| Contenido de `audio/canciones/`, en `/mnt/c` | Calibración del MPU y radar | Escaneo de audio | Puerto abierto a los |
|---|---|---|---|
| Vacío | 0,57 s | 0,01 s | 0,57 s |
| Las 8 pistas generadas (688 KB) | 0,56 s | 0,81 s | 1,37 s |
| Las 9 versionadas (6,1 MB) | 0,57 s | 5,84 s | 6,41 s |

La última fila se midió sobre una copia recién creada; con el archivo ya en la caché de
disco el escaneo bajó a unos 3 s.

**Corrección.** `sim/prueba_api.sh` ahora sondea `GET /` una vez por segundo hasta que
el servidor responde, con un máximo de 15 s, y si no responde termina con un mensaje
que apunta a `run/srv.log`.

## Resultado con la espera corregida: pasa

| Corrida | Código de salida | Duración | Última línea |
|---|---|---|---|
| 1 | 0 | 11 s | `OK` |
| 2 | 0 | 10 s | `OK` |
| 3 | 0 | 14 s | `OK` |

En las tres, el log del servidor registra `Servidor escuchando en puerto 8080` y el
mapa arranca de cero, así que cada corrida habló con su propio servidor.

| Verificación | Esperado | Obtenido |
|---|---|---|
| `GET /` | 200 | 200 |
| Login con contraseña incorrecta | 401 | 401 |
| `GET /api/status` sin sesión | 401 | 401 |
| Login correcto | 200 | 200 |
| Estado inicial | modo autónomo, LED de encendido y de autónomo | `autonomous`, `power` y `autonomous` en `True` |
| Radar | 7 lecturas, de 0° a 180° | 7 lecturas; frente a 177 cm |
| Velocidad y tiempo de choque | valores del MPU-6050 | 30,9 cm/s; choque en 5,6 s; `imu True` |
| Cambio a modo manual | `{"ok":true}` | `{"ok":true}` |
| Avanzar | `{"ok":true}` | `{"ok":true}` |
| Lista de audio | las pistas de `audio/canciones/` | 9 pistas |
| Volumen y detener | `{"ok":true}` | `{"ok":true}` en ambos |
| Mapa en modo autónomo | las celdas visitadas crecen | 2 → 4 → 6 → 7 en 6 s |

Salida de la corrida 1:

```text
== Autenticacion (#25) ==
  GET /                 -> HTTP 200
  login mal password    -> HTTP 401
  status sin sesion     -> HTTP 401
  login correcto        -> HTTP 200
== Panel / modo / movimiento (#26) ==
  status: autonomous | sensores {'front': 176.9, 'back': 0.0, 'left': 200.0, 'right': 200.0} | leds {'power': True, 'autonomous': True, 'manual': False, 'obstacle': False}
  radar: servo 90 | 0:200 30:60 60:80 90:177 120:210 150:231 180:200 | velocidad 30.9 cm/s | choque en 5.6 s | imu True
  mode->manual  {"ok":true}
  move forward  {"ok":true}
== Audio (#27) ==
  list    {"tracks":[{"id":1,"name":"01_Himno_a_la_alegria.mp3","duration":"0:12"},{"id":2,"name":"02_Para_Elisa.mp3","duration":"0:09"},{"id":3,"name":"03_Canon_en_Re.mp3","duration":"0:10"},{"id":4,"name":"04_Pequena_serenata_nocturna.mp3","duration":"0:10"},{"id":5,"name":"05_En_la_gruta_del_rey_de_la_montana.mp3","duration":"0:11"},{"id":6,"name":"06_Minueto_en_Sol.mp3","duration":"0:10"},{"id":7,"name":"07_Cancion_de_cuna.mp3","duration":"0:12"},{"id":8,"name":"08_Estrellita.mp3","duration":"0:09"},{"id":9,"name":"Ren - Bitter Sweet Symphony (Live).mp3","duration":"3:53"}]}
  volume  {"ok":true}
  stop    {"ok":true}
== Mapa incremental (#28/#29) ==
  celda=30 cm robot=(15,14) rumbo=0 visitadas=2 obstaculos=2
  celda=30 cm robot=(15,12) rumbo=0 visitadas=4 obstaculos=4
  celda=30 cm robot=(15,10) rumbo=0 visitadas=6 obstaculos=8
  celda=30 cm robot=(15,10) rumbo=0 visitadas=7 obstaculos=8
OK
```

## Observaciones

- **La espera fija solo falla sobre un disco lento.** Con la carpeta de trabajo en un
  disco Linux el servidor escucha a los 0,6 s y el script original pasaba. El sondeo
  hace que la prueba deje de depender de dónde está clonado el repositorio.
- **El servidor tarda 6,9 s en terminar tras `SIGTERM`.** Libera el puerto antes, pero
  el proceso sigue vivo mientras apaga el hardware. No afecta a la prueba; conviene
  tenerlo presente al medir `systemctl stop` o `restart` en el target.
- **`sim/prueba_reinicio_systemd.sh` tiene la misma espera fija** de 3 s antes de
  consultar el servidor. Este PR no la toca.
- **La prueba casi no afirma nada por sí sola.** Imprime los códigos HTTP pero no los
  compara: solo falla si el servidor no responde o si un JSON no se puede leer. La
  tabla de arriba es la comparación hecha a mano contra lo que documenta
  `docs/qemu.md`.
- Aviso `-Wmisleading-indentation` en `lib/lib_audio.c:826`, el mismo de la prueba 02.

## Límites

- Hardware simulado y binario x86_64 del host: no prueba el servicio `systemd`, la
  receta `robot-server` ni el target.
- El audio no se reproduce: el host no tiene tarjeta de sonido.
