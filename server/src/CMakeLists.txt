# Proyecto I - Robot Aspiradora Autonomo
# CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
#
# Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
# MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.

cmake_minimum_required(VERSION 3.16)
project(robot-server VERSION 1.0 LANGUAGES C)

# Buscar librerias
find_library(MHD_LIB microhttpd REQUIRED)
find_library(ROBOT_LIB robot REQUIRED)
find_library(PIGPIOD_IF2_LIB pigpiod_if2 REQUIRED)
find_library(PTHREAD_LIB pthread REQUIRED)

find_path(MHD_INCLUDE microhttpd.h REQUIRED)

# Buscar directorio donde librobot instalo sus headers
find_path(ROBOT_INCLUDE
    NAMES lib_audio.h
    PATH_SUFFIXES robot
    REQUIRED
)

add_executable(robot-server
    main.c
    api.c
    auth.c
    sha256.c
    robot_state.c
    robot_hardware.c
)

target_include_directories(robot-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}   # api.h, auth.h, robot_state.h, robot_hardware.h
    ${MHD_INCLUDE}                # microhttpd.h
    ${ROBOT_INCLUDE}              # lib_audio.h, lib_leds.h, lib_motors.h, lib_sensors.h
)

target_link_libraries(robot-server
    ${MHD_LIB}
    ${ROBOT_LIB}
    ${PIGPIOD_IF2_LIB}
    ${PTHREAD_LIB}
)

include(GNUInstallDirs)
install(TARGETS robot-server
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

