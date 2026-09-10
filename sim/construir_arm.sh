#!/bin/sh
# Proyecto I - Robot Aspiradora Autonomo — CE-1113, TEC
#
# Compila la prueba de la biblioteca para ARM con el Toolchain-SDK y la ejecuta
# bajo qemu-aarch64 (modo usuario, incluido en el propio SDK). Prueba que los
# binarios cross-compilados EJECUTAN correctamente, sin la Raspberry.
#
# Requiere el SDK instalado (por defecto en ~/robot-sdk). Ver docs/sdk.md.
set -e
SIM="$(cd "$(dirname "$0")" && pwd)"
SDK="${SDK:-$HOME/robot-sdk}"
ENV="$SDK/environment-setup-cortexa72-poky-linux"
QEMU="$SDK/sysroots/x86_64-pokysdk-linux/usr/bin/qemu-aarch64"

[ -f "$ENV" ] || { echo "No encuentro el SDK en $SDK. Instalalo (docs/sdk.md) o exporta SDK=<ruta>."; exit 1; }

unset LD_LIBRARY_PATH
. "$ENV"

$CC -Wall -O1 -I"$SIM" -I"$SIM/../lib" -I"$SIM/../server/src" \
    "$SIM"/../lib/lib_motors.c "$SIM"/../lib/lib_sensors.c "$SIM"/../lib/lib_leds.c \
    "$SIM"/../lib/lib_odom.c "$SIM"/../lib/lib_robot.c "$SIM"/../server/src/robot_state.c \
    "$SIM"/mundo.c "$SIM"/pigpio_sim.c "$SIM"/prueba_librobot.c \
    -o "$SIM/prueba_librobot_arm" -lm -lpthread

echo "binario: $(file "$SIM/prueba_librobot_arm" | cut -d, -f1-2)"
echo "ejecutando bajo qemu-aarch64 ..."
"$QEMU" -L "$SDKTARGETSYSROOT" "$SIM/prueba_librobot_arm"
