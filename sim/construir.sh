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

set -e
cd "$(dirname "$0")"

CC=${CC:-gcc}
LIB=../lib
SRV=../server/src

$CC -Wall -Wextra -O1 -g -I. -I"$LIB" \
    "$LIB"/lib_motors.c \
    "$LIB"/lib_sensors.c \
    "$LIB"/lib_leds.c \
    "$LIB"/lib_odom.c \
    "$LIB"/lib_audio.c \
    "$LIB"/lib_robot.c \
    "$SRV"/robot_state.c \
    mundo.c \
    pigpio_sim.c \
    prueba_librobot.c \
    -o prueba_librobot -lmpg123 -lasound -lm -lpthread

echo "listo: ./prueba_librobot"
