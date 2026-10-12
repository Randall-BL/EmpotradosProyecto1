# Proyecto I - Robot Aspiradora Autonomo
# CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
#


# Version minima de CMake que entiende este archivo.
cmake_minimum_required(VERSION 3.16)
# Proyecto en C, version 1.0.
project(robot VERSION 1.0 LANGUAGES C)

# ROBOT_SIM enlaza el simulador de sim/ en lugar de pigpiod: el mismo codigo de
# la biblioteca mueve un robot virtual. Lo usa la imagen de QEMU
# (MACHINE=qemuarm64-robot), donde no hay GPIO real. Ver docs/qemu.md.
option(ROBOT_SIM "Enlazar el simulador de hardware en vez de pigpiod" OFF)
# Carpeta del simulador; por defecto, sim/ junto a lib/. La receta de la
# maquina de QEMU la cambia a la copia que desempaqueta dentro de lib/.
set(ROBOT_SIM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sim" CACHE PATH
    "Carpeta con pigpio_sim.c, mundo.c y el pigpiod_if2.h de reemplazo")

# Buscar dependencias
if(NOT ROBOT_SIM)
    # Con hardware real hace falta el cliente de pigpiod; si no esta, la
    # configuracion falla.
    find_library(PIGPIOD_IF2_LIB pigpiod_if2 REQUIRED)
    find_path(PIGPIOD_INCLUDE pigpiod_if2.h)
endif()
# ALSA y mpg123 hacen falta siempre.
find_library(ASOUND_LIB asound REQUIRED)
find_library(MPG123_LIB mpg123 REQUIRED)

# Carpetas de sus cabeceras.
find_path(ALSA_INCLUDE alsa/asoundlib.h)
find_path(MPG123_INCLUDE mpg123.h)

# Crear biblioteca dinamica
# Un .c por modulo.
add_library(robot SHARED
    lib_audio.c
    lib_caida.c
    lib_imu.c
    lib_leds.c
    lib_motors.c
    lib_odom.c
    lib_radar.c
    lib_robot.c
    lib_sensors.c
    lib_servo.c
)

if(ROBOT_SIM)
    # El simulador se compila dentro de la misma biblioteca.
    target_sources(robot PRIVATE
        ${ROBOT_SIM_DIR}/pigpio_sim.c
        ${ROBOT_SIM_DIR}/mundo.c
    )
    # BEFORE: el <pigpiod_if2.h> de los fuentes debe resolver contra el del
    # simulador, igual que en sim/construir.sh.
    target_include_directories(robot BEFORE PRIVATE ${ROBOT_SIM_DIR})
endif()

# Headers propios
target_include_directories(robot PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${PIGPIOD_INCLUDE}
    ${ALSA_INCLUDE}
    ${MPG123_INCLUDE}
)

# Linking
# Bibliotecas contra las que se enlaza; sin pigpiod_if2 si es el simulador.
target_link_libraries(robot
    ${PIGPIOD_IF2_LIB}
    m
    ${ASOUND_LIB}
    ${MPG123_LIB}
    pthread
)

# Version del .so
# Genera librobot.so.1.0.0 con SONAME librobot.so.1, y marca los headers
# publicos.
set_target_properties(robot PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    PUBLIC_HEADER "lib_audio.h;lib_caida.h;lib_imu.h;lib_leds.h;lib_motors.h;lib_odom.h;lib_radar.h;lib_robot.h;lib_sensors.h;lib_servo.h"
)

# Instalacion
# Rutas de instalacion estandar: lib/ e include/.
include(GNUInstallDirs)
# La biblioteca va a lib/ y los headers a include/robot/.
install(TARGETS robot
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/robot
)
