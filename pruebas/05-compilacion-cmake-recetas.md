# Prueba 05 — Compilación e instalación por CMake como en las recetas

| | |
|---|---|
| **Fecha** | 11 de octubre de 2026 |
| **Commit probado** | `b1187e1` (mismo árbol que `main` en `82f6472`) |
| **Entorno** | WSL2, Ubuntu 24.04.3 LTS, gcc 13.3.0, CMake 3.28.3, Python 3.12.3 |
| **Tipo** | Ejecución real de CMake con el gcc del host, sobre un desempaquetado que emula `do_unpack`. **No ejecuta BitBake.** |
| **Resultado** | **Pasa**: 6 de 6 comprobaciones, código de salida 0, 28 s |

## Qué verifica

Las pruebas 02 a 04 compilan con los scripts de `sim/`, que usan `gcc` directamente.
Las recetas no: usan los `CMakeLists.txt` de `lib/` y `server/src/`. Esta prueba
recorre ese otro camino, con la variante de la máquina `qemuarm64-robot`:

1. Desempaqueta el `SRC_URI` de cada receta, con su `:append:qemuall`, en un
   `WORKDIR` propio y respetando `;subdir=`.
2. Configura `librobot` desde `${S}` con el `EXTRA_OECMAKE` de la receta
   (`-DROBOT_SIM=ON -DROBOT_SIM_DIR=${S}/sim`), la compila y la instala en un sysroot.
3. Configura `robot-server` contra ese sysroot, igual que BitBake con
   `DEPENDS = "librobot"`, lo compila y lo instala.
4. Comprueba que lo instalado es lo que declaran las variables `FILES`.
5. Arranca el `robot-server` instalado y le pide `GET /` y `GET /api/status`.

Si a un `SRC_URI` le faltara un fuente, si un `CMakeLists.txt` estuviera roto o si
el servidor usara un header que `librobot` no instala, la prueba fallaría.

## Cómo se ejecuta

```sh
sudo apt install cmake libmicrohttpd-dev libmpg123-dev libasound2-dev
python3 pruebas/herramientas/compilar_como_receta.py . /tmp/build-receta
```

El puerto 8080 debe estar libre. Devuelve 0 si todo pasa.

En esta ejecución CMake y las bibliotecas de desarrollo se tomaron de carpetas
propias, sin instalarlas en el sistema, mediante las variables `CMAKE` y
`CMAKE_EXTRA` que acepta el script.

## Resultado

```text
== do_unpack ==
  librobot: 27 entradas de SRC_URI desempaquetadas en librobot/
  robot-server: 14 entradas de SRC_URI desempaquetadas en robot-server/

== librobot: configurar, compilar e instalar ==
  EXTRA_OECMAKE: -DROBOT_SIM=ON -DROBOT_SIM_DIR=${TRABAJO}/librobot/lib/sim
  [OK ] FILES:${PN}: usr/lib/librobot.so.1.0.0 y su enlace librobot.so.1
  [OK ] FILES:${PN}-dev: usr/lib/librobot.so
  [OK ] FILES:${PN}-dev: 10 headers en usr/include/robot/
         lib_audio.h lib_caida.h lib_imu.h lib_leds.h lib_motors.h lib_odom.h lib_radar.h lib_robot.h lib_sensors.h lib_servo.h
  avisos del compilador: 1
    librobot/lib/lib_audio.c:826:5: warning: this ‘if’ clause does not guard... [-Wmisleading-indentation]

== robot-server: configurar contra el sysroot, compilar e instalar ==
  [OK ] usr/bin/robot-server instalado
  avisos del compilador: 0

== arranque del servidor instalado ==
  el servidor respondio a los 1.0 s
  [OK ] GET / devuelve 200 (obtenido 200)
  [OK ] GET /api/status sin sesion devuelve 401 (obtenido 401)

RESULTADO: OK (0 problemas)
```

Contenido del sysroot tras las dos instalaciones:

```text
usr/bin/robot-server
usr/include/robot/lib_audio.h
usr/include/robot/lib_caida.h
usr/include/robot/lib_imu.h
usr/include/robot/lib_leds.h
usr/include/robot/lib_motors.h
usr/include/robot/lib_odom.h
usr/include/robot/lib_radar.h
usr/include/robot/lib_robot.h
usr/include/robot/lib_sensors.h
usr/include/robot/lib_servo.h
usr/lib/librobot.so
usr/lib/librobot.so.1
usr/lib/librobot.so.1.0.0
```

Coincide con lo que empaquetan las recetas:

| Variable | Patrón | Instalado |
|---|---|---|
| `FILES:${PN}` de `librobot` | `${libdir}/librobot.so.1*` | `librobot.so.1`, `librobot.so.1.0.0` |
| `FILES:${PN}-dev` de `librobot` | `${libdir}/librobot.so`, `${includedir}/robot/*.h` | `librobot.so` y 10 headers |
| `robot-server` | `${bindir}/robot-server` | `usr/bin/robot-server` |

Tras la prueba se comprobó que no quedó ningún servidor vivo.

## Observaciones

- **El servidor respondió al primer sondeo, a 1,0 s**, contra los 3,6 s de la prueba
  03. Las dos corrieron sobre el mismo disco lento; la diferencia es que aquí el
  directorio de trabajo se arma con los sonidos de evento y sin canciones, así que
  casi no hay nada que leer al arrancar.
- **Hay dos copias de `robot_state.h`**, en `lib/` y en `server/src/`. Hoy solo
  difieren en una línea en blanco. `lib_leds.c` compila con la de `lib/` y el
  servidor con la suya; si una cambia y la otra no, la estructura `RobotState`
  dejaría de coincidir entre la biblioteca y el servidor sin que el compilador avise.
- Un aviso del compilador, el mismo `-Wmisleading-indentation` de
  `lib/lib_audio.c:826`. El servidor compila sin avisos con `-Wall -Wextra`.

## Límites

- **Compilador del host, no el cruzado.** No prueba el toolchain de Yocto, las
  banderas de `cmake.bbclass`, `do_package` ni `do_package_qa`.
- **Solo la variante `qemuall`.** La de la Raspberry Pi enlaza `pigpiod_if2`, que no
  existe en el host, así que `find_library(PIGPIOD_IF2_LIB ... REQUIRED)` no se puede
  ejercitar aquí.
- El `do_install:append` de `robot-server` (la interfaz web, los audios y la unidad
  `systemd`) no se ejecuta: el script solo copia `www/` y los `notify_*.mp3` para
  poder arrancar el binario.
