# ─────────────────────────────────────────────────────────────────────────────
#  librobot — Biblioteca dinamica de control del robot
#
#  Unica via de acceso al hardware. El servidor web no toca GPIO directamente:
#  todo pasa por este .so, tal como exige el enunciado.
#
#  Las fuentes viven en lib/, en la raiz del repositorio, y no dentro de la capa:
#  asi hay una sola copia del codigo, que se edita y se compila igual desde el
#  Toolchain-SDK que desde BitBake. FILESEXTRAPATHS apunta el fetcher hacia alla.
# ─────────────────────────────────────────────────────────────────────────────

SUMMARY = "Biblioteca dinamica de control del robot aspiradora (motores, radar, MPU-6050, LEDs, audio)"
DESCRIPTION = "Abstrae el hardware del robot: PWM de los motores DC, el radar \
ultrasonico (HC-SR04 sobre un servo de 180 grados), el MPU-6050 por I2C, la \
odometria, los cuatro LEDs indicadores y la reproduccion de MP3."
SECTION = "libs"
HOMEPAGE = "https://github.com/Randall-BL/EmpotradosProyecto1"

# El proyecto se distribuye bajo MIT (ver LICENSE y NOTICE.md en la raiz).
# Si este checksum falla tras editar LICENSE, recalcularlo con: md5sum LICENSE
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=71d3accc05fafe1f2c137677ec5d1600"

# lib/ y LICENSE estan tres niveles arriba de esta receta, en la raiz del repo.
FILESEXTRAPATHS:prepend := "${THISDIR}/../../../lib:${THISDIR}/../../..:"

# Las fuentes se desempaquetan en ${WORKDIR}/lib y no sueltas en ${WORKDIR}:
# con S = ${WORKDIR}, do_unpack sobrescribia en el lugar archivos que
# do_package ya habia enlazado (hardlink) en package/usr/src/debug, y al cambiar
# cualquier fuente el siguiente do_install abortaba con "abort()ing pseudo
# client ... path mismatch". Con S en un subdirectorio, do_unpack lo vacia
# antes de desempaquetar y los archivos nuevos son inodos nuevos.
SRC_URI = " \
    file://lib_motors.c;subdir=lib   \
    file://lib_motors.h;subdir=lib   \
    file://lib_sensors.c;subdir=lib  \
    file://lib_sensors.h;subdir=lib  \
    file://lib_servo.c;subdir=lib    \
    file://lib_servo.h;subdir=lib    \
    file://lib_radar.c;subdir=lib    \
    file://lib_radar.h;subdir=lib    \
    file://lib_imu.c;subdir=lib      \
    file://lib_imu.h;subdir=lib      \
    file://lib_leds.c;subdir=lib     \
    file://lib_leds.h;subdir=lib     \
    file://lib_audio.c;subdir=lib    \
    file://lib_audio.h;subdir=lib    \
    file://lib_odom.c;subdir=lib     \
    file://lib_odom.h;subdir=lib     \
    file://lib_robot.c;subdir=lib    \
    file://lib_robot.h;subdir=lib    \
    file://robot_state.h;subdir=lib  \
    file://CMakeLists.txt;subdir=lib \
    file://LICENSE;subdir=lib        \
"

S = "${WORKDIR}/lib"

# Dependencias de compilacion:
#   pigpio    -> pigpiod_if2.h y libpigpiod_if2: GPIO, PWM, pulsos del servo e I2C
#   mpg123    -> decodificacion de MP3
#   alsa-lib  -> salida de audio PWM (GPIO 18 -> amplificador PAM8403)
DEPENDS = "mpg123 alsa-lib"
DEPENDS:append:rpi = " pigpio"

# En tiempo de ejecucion hace falta el demonio pigpiod, no solo la biblioteca:
# pigpiod_if2 es un cliente que se conecta a el por socket.
RDEPENDS:${PN}:rpi = "pigpio-bin-pigpiod"

# ── Variante para QEMU ───────────────────────────────────────────────────────
# En la maquina qemuarm64-robot no hay GPIO: la biblioteca se enlaza con el
# simulador de sim/ (ROBOT_SIM), que mueve un robot virtual por una sala de
# 4x4 m y responde el radar y el MPU-6050 sobre ese mundo. El codigo de lib/ es
# el mismo.
# Dentro de S (${S}/sim) y no al lado: fuera de S el compilador no reescribe
# la ruta en la informacion de depuracion y do_package_qa avisa [buildpaths].
SRC_URI:append:qemuall = " \
    file://sim/pigpio_sim.c;subdir=lib   \
    file://sim/pigpiod_if2.h;subdir=lib  \
    file://sim/mundo.c;subdir=lib        \
    file://sim/mundo.h;subdir=lib        \
"
EXTRA_OECMAKE:append:qemuall = " -DROBOT_SIM=ON -DROBOT_SIM_DIR=${S}/sim"

# El contenido del .so cambia segun la maquina (pigpiod o simulador) aunque las
# dos compartan el tune cortexa72: sin esto ambas escribirian el mismo paquete
# en deploy/rpm/cortexa72 y una imagen podria llevarse la biblioteca de la otra.
PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit cmake

# La biblioteca versionada va al paquete principal; el enlace de desarrollo y los
# headers al paquete -dev, que no se instala en la imagen final.
FILES:${PN}     = "${libdir}/librobot.so.1*"
FILES:${PN}-dev = "${libdir}/librobot.so ${includedir}/robot/*.h"
