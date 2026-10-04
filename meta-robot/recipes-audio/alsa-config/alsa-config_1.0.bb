# Configuracion de ALSA para el robot.
#
# La Raspberry Pi 4 expone dos tarjetas de sonido: la card 0 es la salida por HDMI
# y la card 1 es la salida PWM analogica ("Headphones"), la que normalmente va al
# jack de 3.5 mm. En el robot el overlay audremap la saca por GPIO 18 hacia el
# amplificador PAM8403 (ver rpi-config_%.bbappend), pero para ALSA sigue siendo
# la card 1. Sin esta configuracion ALSA toma la card 0 por defecto y el audio se
# va al HDMI, que en el robot no esta conectado. asound.conf fuerza la card 1,
# de modo que mpg123, aplay y speaker-test salgan por el parlante.

SUMMARY = "Configuracion de ALSA: salida PWM analogica de la RPi4 (GPIO 18 -> PAM8403)"
LICENSE = "CLOSED"

SRC_URI = "file://asound.conf"

S = "${WORKDIR}"

do_install() {
    install -d ${D}${sysconfdir}
    install -m 0644 ${WORKDIR}/asound.conf ${D}${sysconfdir}/asound.conf
}

FILES:${PN} = "${sysconfdir}/asound.conf"

# Sin alsa-lib en el target esta configuracion no la lee nadie.
RDEPENDS:${PN} = "alsa-lib"
