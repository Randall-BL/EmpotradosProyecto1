#!/bin/sh
# Proyecto I - Robot Aspiradora Autonomo — CE-1113, TEC
#
# Compila la prueba de la biblioteca para ARM con el Toolchain-SDK y la ejecuta
# bajo qemu-aarch64 (modo usuario, incluido en el propio SDK). Prueba que los
# binarios cross-compilados EJECUTAN correctamente, sin la Raspberry.
#
# Requiere el SDK instalado (por defecto en ~/robot-sdk). Ver docs/sdk.md.
# Cualquier comando que falle detiene el script.
set -e
# Carpeta de este script, en ruta absoluta.
SIM="$(cd "$(dirname "$0")" && pwd)"
# Donde esta instalado el SDK; se puede cambiar con la variable SDK.
SDK="${SDK:-$HOME/robot-sdk}"
# Script del SDK que exporta el compilador cruzado.
ENV="$SDK/environment-setup-cortexa72-poky-linux"
# Emulador de usuario que trae el propio SDK.
QEMU="$SDK/sysroots/x86_64-pokysdk-linux/usr/bin/qemu-aarch64"

# Sin SDK instalado no se puede seguir.
[ -f "$ENV" ] || { echo "No encuentro el SDK en $SDK. Instalalo (docs/sdk.md) o exporta SDK=<ruta>."; exit 1; }

# El script del SDK exige que LD_LIBRARY_PATH este vacia.
unset LD_LIBRARY_PATH
# Carga el entorno: CC pasa a ser el compilador cruzado.
. "$ENV"

# Compila la prueba para ARM, con las mismas fuentes que construir.sh.
$CC -Wall -O1 -I"$SIM" -I"$SIM/../lib" -I"$SIM/../server/src" \
    "$SIM"/../lib/lib_*.c "$SIM"/../server/src/robot_state.c \
    "$SIM"/mundo.c "$SIM"/pigpio_sim.c "$SIM"/prueba_librobot.c \
    -o "$SIM/prueba_librobot_arm" -lmpg123 -lasound -lm -lpthread

# Muestra el tipo del binario: tiene que decir ARM aarch64.
echo "binario: $(file "$SIM/prueba_librobot_arm" | cut -d, -f1-2)"
echo "ejecutando bajo qemu-aarch64 ..."
# Lo ejecuta bajo emulacion, con las bibliotecas del sysroot del SDK.
"$QEMU" -L "$SDKTARGETSYSROOT" "$SIM/prueba_librobot_arm"
