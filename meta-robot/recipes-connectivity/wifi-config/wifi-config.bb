# WiFi preconfigurado para arranque headless.
#
# El robot no tiene pantalla ni teclado: tiene que unirse solo a la red al
# encender, porque la interfaz de control es justamente el servidor web. Esta
# receta instala las credenciales de wpa_supplicant, la configuracion de
# systemd-networkd para pedir IP por DHCP en wlan0, y habilita el servicio
# wpa_supplicant@wlan0 en el arranque.
#
# El archivo wpa_supplicant-wlan0.conf NO esta versionado (contiene la
# contrasena de la red). Debe crearse desde files/wpa_supplicant-wlan0.conf.sample
# antes del primer build.

SUMMARY = "WiFi preconfigurado para arranque headless"
LICENSE = "CLOSED"

SRC_URI = " \
    file://wpa_supplicant-wlan0.conf \
    file://25-wlan.network \
"

S = "${WORKDIR}"

do_install() {
    # Credenciales: modo 600, solo root las puede leer.
    install -d ${D}${sysconfdir}/wpa_supplicant
    install -m 600 ${WORKDIR}/wpa_supplicant-wlan0.conf \
        ${D}${sysconfdir}/wpa_supplicant/

    # DHCP en wlan0 via systemd-networkd.
    install -d ${D}${sysconfdir}/systemd/network
    install -m 644 ${WORKDIR}/25-wlan.network \
        ${D}${sysconfdir}/systemd/network/

    # Habilitar wpa_supplicant@wlan0 en el arranque. Se enlaza a mano porque la
    # unidad es plantilla (wpa_supplicant@.service) y SYSTEMD_SERVICE no admite
    # instanciarla desde otra receta.
    install -d ${D}${sysconfdir}/systemd/system/multi-user.target.wants
    ln -sf ${systemd_system_unitdir}/wpa_supplicant@.service \
        ${D}${sysconfdir}/systemd/system/multi-user.target.wants/wpa_supplicant@wlan0.service
}

FILES:${PN} += " \
    ${sysconfdir}/wpa_supplicant/wpa_supplicant-wlan0.conf \
    ${sysconfdir}/systemd/network/25-wlan.network \
    ${sysconfdir}/systemd/system/multi-user.target.wants/wpa_supplicant@wlan0.service \
"

RDEPENDS:${PN} = "wpa-supplicant"
