# Proyecto I - Robot Aspiradora Autonomo
# CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
#
# Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
# MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.

cmake_minimum_required(VERSION 3.16)
project(robot VERSION 1.0 LANGUAGES C)

# Buscar dependencias
find_library(PIGPIOD_IF2_LIB pigpiod_if2 REQUIRED)
find_library(ASOUND_LIB asound REQUIRED)
find_library(MPG123_LIB mpg123 REQUIRED)

find_path(PIGPIOD_INCLUDE pigpiod_if2.h)
find_path(ALSA_INCLUDE alsa/asoundlib.h)
find_path(MPG123_INCLUDE mpg123.h)

# Crear biblioteca dinamica
add_library(robot SHARED
    lib_audio.c
    lib_leds.c
    lib_motors.c
    lib_sensors.c
)

# Headers propios
target_include_directories(robot PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${PIGPIOD_INCLUDE}
    ${ALSA_INCLUDE}
    ${MPG123_INCLUDE}
)

# Linking
target_link_libraries(robot
    ${PIGPIOD_IF2_LIB}
    ${ASOUND_LIB}
    ${MPG123_LIB}
    pthread
)

# Version del .so
set_target_properties(robot PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    PUBLIC_HEADER "lib_audio.h;lib_leds.h;lib_motors.h;lib_sensors.h"
)

# Instalacion
include(GNUInstallDirs)
install(TARGETS robot
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/robot
)
