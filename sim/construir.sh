#!/bin/sh
# Proyecto I - Robot Aspiradora Autonomo
# CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
#
# Compila la biblioteca contra el simulador, sin Raspberry y sin Yocto.
# Los fuentes de lib/ NO se tocan: -I. antepone el pigpiod_if2.h de este
# directorio, y el enlace resuelve contra pigpio_sim.c.
#
# Se compila tambien robot_state.c del servidor: lib_leds.c lo usa para
# sincronizar los LEDs con el estado global, y en la Raspberry ese simbolo lo
# aporta el ejecutable del servidor al cargar el .so.

# Cualquier comando que falle detiene el script.
set -e
# Trabaja en la carpeta del script, se lo llame desde donde se lo llame.
cd "$(dirname "$0")"

# Compilador: el del host, o el que venga en la variable CC.
CC=${CC:-gcc}
# Fuentes de la biblioteca y del servidor.
LIB=../lib
SRV=../server/src

# Todos los modulos de la biblioteca: motores, sensor, servo, radar, MPU,
# odometria, LEDs, audio y la fachada.
# -I. va antes que -I$LIB: el pigpiod_if2.h de esta carpeta gana. Enlaza con
# mpg123, ALSA, la biblioteca matematica y pthreads.
$CC -Wall -Wextra -O1 -g -I. -I"$LIB" \
    "$LIB"/lib_*.c \
    "$SRV"/robot_state.c \
    mundo.c \
    pigpio_sim.c \
    prueba_librobot.c \
    -o prueba_librobot -lmpg123 -lasound -lm -lpthread

# Aviso de que el binario quedo listo.
echo "listo: ./prueba_librobot"
