#!/bin/sh
# Proyecto I - Robot Aspiradora Autonomo
# CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
#
# Compila el servidor de control COMPLETO para correr en la laptop sobre el
# robot simulado. Igual que construir.sh, -I. hace que el <pigpiod_if2.h> de la
# biblioteca resuelva contra el simulador; microhttpd, mpg123 y alsa son los
# reales del host (paquetes -dev). Sirve para probar el dashboard en el
# navegador y para la prueba de reinicio del servicio systemd (issue #12).

set -e
cd "$(dirname "$0")"
CC=${CC:-gcc}
LIB=../lib
SRV=../server/src

$CC -Wall -O1 -g -I. -I"$LIB" -I"$SRV" \
    "$SRV"/main.c "$SRV"/api.c "$SRV"/auth.c "$SRV"/sha256.c "$SRV"/robot_state.c \
    "$LIB"/lib_motors.c "$LIB"/lib_sensors.c "$LIB"/lib_leds.c \
    "$LIB"/lib_odom.c "$LIB"/lib_robot.c "$LIB"/lib_audio.c \
    mundo.c pigpio_sim.c \
    -o robot-server-sim \
    -lmicrohttpd -lmpg123 -lasound -lm -lpthread

echo "listo: ./robot-server-sim  (dashboard en http://localhost:8080)"
