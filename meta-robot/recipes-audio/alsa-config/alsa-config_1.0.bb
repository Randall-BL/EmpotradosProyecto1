# Configuracion de ALSA para el robot.
#
# La Raspberry Pi 4 expone dos tarjetas de sonido: la card 0 es la salida por HDMI
# y la card 1 es el jack analogico de 3.5 mm. Sin esta configuracion ALSA toma la
# card 0 por defecto y el audio se va al HDMI, que en el robot no esta conectado.
# asound.conf fuerza la card 1 como dispositivo por defecto, de modo que mpg123 y
# la biblioteca de audio salgan por el amplificador y el altavoz.

SUMMARY = "Configuracion de ALSA: salida por el jack de 3.5 mm de la RPi4"
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
