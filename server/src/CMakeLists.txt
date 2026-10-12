# Proyecto I - Robot Aspiradora Autonomo
# CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
#


# Version minima de CMake que entiende este archivo.
cmake_minimum_required(VERSION 3.16)
# Proyecto en C, version 1.0.
project(robot-server VERSION 1.0 LANGUAGES C)

# Buscar librerias
# El servidor NO enlaza pigpio: todo el acceso al hardware pasa por librobot,
# tal como exige el enunciado. Si alguna vez vuelve a aparecer pigpio aqui, es
# senal de que alguien metio GPIO en el servidor.
# Bibliotecas necesarias; si falta alguna, la configuracion falla.
find_library(MHD_LIB microhttpd REQUIRED)
find_library(ROBOT_LIB robot REQUIRED)
find_library(PTHREAD_LIB pthread REQUIRED)

# Carpeta de la cabecera de libmicrohttpd.
find_path(MHD_INCLUDE microhttpd.h REQUIRED)

# Buscar directorio donde librobot instalo sus headers
find_path(ROBOT_INCLUDE
    NAMES lib_audio.h
    PATH_SUFFIXES robot
    REQUIRED
)

# El ejecutable y sus fuentes.
add_executable(robot-server
    main.c
    api.c
    auth.c
    sha256.c
    robot_state.c
)

target_include_directories(robot-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}   # api.h, auth.h, robot_state.h
    ${MHD_INCLUDE}                # microhttpd.h
    ${ROBOT_INCLUDE}              # lib_robot.h, lib_audio.h, lib_leds.h, lib_motors.h, lib_odom.h
)

# Bibliotecas contra las que se enlaza.
target_link_libraries(robot-server
    ${MHD_LIB}
    ${ROBOT_LIB}
    ${PTHREAD_LIB}
    m            # sin/cos/lround, para proyectar los sensores sobre el mapa
)

# Rutas de instalacion estandar.
include(GNUInstallDirs)
# El binario va a bin/.
install(TARGETS robot-server
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

