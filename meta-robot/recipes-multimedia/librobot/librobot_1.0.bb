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

SUMMARY = "Biblioteca dinamica de control del robot aspiradora (motores, sensores, LEDs, audio)"
DESCRIPTION = "Abstrae el hardware del robot: PWM de los motores DC, lectura de los \
sensores HC-SR04, los cuatro LEDs indicadores y la reproduccion de MP3."
SECTION = "libs"
HOMEPAGE = "https://github.com/Randall-BL/EmpotradosProyecto1"

# El proyecto se distribuye bajo MIT (ver LICENSE y NOTICE.md en la raiz).
# Si este checksum falla tras editar LICENSE, recalcularlo con: md5sum LICENSE
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=71d3accc05fafe1f2c137677ec5d1600"

# lib/ y LICENSE estan tres niveles arriba de esta receta, en la raiz del repo.
FILESEXTRAPATHS:prepend := "${THISDIR}/../../../lib:${THISDIR}/../../..:"

SRC_URI = " \
    file://lib_motors.c   \
    file://lib_motors.h   \
    file://lib_sensors.c  \
    file://lib_sensors.h  \
    file://lib_leds.c     \
    file://lib_leds.h     \
    file://lib_audio.c    \
    file://lib_audio.h    \
    file://lib_odom.c     \
    file://lib_odom.h     \
    file://lib_robot.c    \
    file://lib_robot.h    \
    file://robot_state.h  \
    file://CMakeLists.txt \
    file://LICENSE        \
"

S = "${WORKDIR}"

# Dependencias de compilacion:
#   pigpio    -> pigpiod_if2.h y libpigpiod_if2, para GPIO y PWM por hardware
#   mpg123    -> decodificacion de MP3
#   alsa-lib  -> salida de audio por el jack de 3.5 mm
DEPENDS = "mpg123 alsa-lib"
DEPENDS:append:rpi = " pigpio"

# En tiempo de ejecucion hace falta el demonio pigpiod, no solo la biblioteca:
# pigpiod_if2 es un cliente que se conecta a el por socket.
RDEPENDS:${PN}:rpi = "pigpio-bin-pigpiod"

# ── Variante para QEMU ───────────────────────────────────────────────────────
# En la maquina qemuarm64-robot no hay GPIO: la biblioteca se enlaza con el
# simulador de sim/ (ROBOT_SIM), que mueve un robot virtual por una sala de
# 4x4 m y responde los sensores sobre ese mundo. El codigo de lib/ es el mismo.
SRC_URI:append:qemuall = " \
    file://sim/pigpio_sim.c   \
    file://sim/pigpiod_if2.h  \
    file://sim/mundo.c        \
    file://sim/mundo.h        \
"
EXTRA_OECMAKE:append:qemuall = " -DROBOT_SIM=ON -DROBOT_SIM_DIR=${WORKDIR}/sim"

# El contenido del .so cambia segun la maquina (pigpiod o simulador) aunque las
# dos compartan el tune cortexa72: sin esto ambas escribirian el mismo paquete
# en deploy/rpm/cortexa72 y una imagen podria llevarse la biblioteca de la otra.
PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit cmake

# La biblioteca versionada va al paquete principal; el enlace de desarrollo y los
# headers al paquete -dev, que no se instala en la imagen final.
FILES:${PN}     = "${libdir}/librobot.so.1*"
FILES:${PN}-dev = "${libdir}/librobot.so ${includedir}/robot/*.h"
