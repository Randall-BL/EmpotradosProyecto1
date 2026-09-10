# ─────────────────────────────────────────────────────────────────────────────
#  robot-server — Servidor web embebido de control
#
#  Servidor HTTP en C sobre libmicrohttpd. Sirve la interfaz web, expone la API
#  REST de control y corre el hilo de navegacion autonoma. Accede al hardware
#  exclusivamente a traves de librobot.
#
#  Instala tres cosas en el target:
#    /usr/bin/robot-server   el binario
#    /opt/robot/www/         la interfaz web estatica
#    /opt/robot/audio/       los sonidos de evento y la musica
# ─────────────────────────────────────────────────────────────────────────────

SUMMARY = "Servidor web embebido de control del robot aspiradora"
DESCRIPTION = "Servidor HTTP en C sobre libmicrohttpd: autenticacion, API REST de \
control, telemetria de sensores, mapa de recorrido y control de audio."
SECTION = "net"
HOMEPAGE = "https://github.com/Randall-BL/EmpotradosProyecto1"

LICENSE = "MIT"

# La ruta de LIC_FILES_CHKSUM se resuelve relativa a S, y aqui S es
# ${WORKDIR}/src porque el CMakeLists del servidor vive en src/. El LICENSE que
# trae el SRC_URI, en cambio, se desempaqueta en ${WORKDIR}. Sin la ruta
# absoluta, do_populate_lic falla con "LIC_FILES_CHKSUM points to an invalid
# file". librobot no tiene el problema porque alli S es ${WORKDIR}.
LIC_FILES_CHKSUM = "file://${WORKDIR}/LICENSE;md5=71d3accc05fafe1f2c137677ec5d1600"

# server/, audio/ y LICENSE estan en la raiz del repositorio, tres niveles arriba.
FILESEXTRAPATHS:prepend := "${THISDIR}/../../../server:${THISDIR}/../../..:"

SRC_URI = " \
    file://src/main.c            \
    file://src/api.c             \
    file://src/api.h             \
    file://src/auth.c            \
    file://src/auth.h            \
    file://src/sha256.c          \
    file://src/sha256.h          \
    file://src/robot_state.c     \
    file://src/robot_state.h     \
    file://src/CMakeLists.txt    \
    file://www                   \
    file://audio                 \
    file://robot-server.service  \
    file://LICENSE               \
"

# El CMakeLists del servidor esta en src/, no en la raiz del WORKDIR.
S = "${WORKDIR}/src"

# libmicrohttpd -> servidor HTTP; librobot -> acceso al hardware;
# pigpio -> pigpiod_if2, del que librobot arrastra simbolos al enlazar.
DEPENDS = "libmicrohttpd librobot pigpio"

RDEPENDS:${PN} = " \
    librobot \
    libmicrohttpd \
    mpg123 \
    alsa-utils \
    pigpio-bin-pigpiod \
"

inherit cmake systemd

# Arranque automatico al energizar el sistema, habilitado desde la receta y no a
# mano en el target: el enunciado exige que la imagen se reproduzca sin pasos
# manuales posteriores.
SYSTEMD_SERVICE:${PN} = "robot-server.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install:append() {
    # Interfaz web estatica.
    install -d ${D}/opt/robot/www
    cp -r ${WORKDIR}/www/* ${D}/opt/robot/www/

    # Recursos de audio. audio/music/ puede venir vacio del repositorio: la
    # imagen se construye igual, solo que sin playlist inicial.
    # Se copian solo los .mp3: el README y el .gitkeep del repositorio no
    # tienen nada que hacer en el rootfs.
    install -d ${D}/opt/robot/audio
    find ${WORKDIR}/audio -name '*.mp3' -exec install -m 0644 {} ${D}/opt/robot/audio/ \;

    # Unidad systemd. La clase systemd se encarga de habilitarla en el arranque.
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/robot-server.service \
                    ${D}${systemd_system_unitdir}/robot-server.service
}

FILES:${PN} += "/opt/robot"
