# WiFi preconfigurado para arranque headless.
#
# El robot no tiene pantalla ni teclado: tiene que unirse solo a la red al
# encender, porque la interfaz de control es justamente el servidor web. Esta
# receta instala las credenciales de wpa_supplicant, la configuracion de
# systemd-networkd para pedir IP por DHCP en wlan0, y habilita el servicio
# wpa_supplicant@wlan0 en el arranque.

SUMMARY = "WiFi preconfigurado para arranque headless"
LICENSE = "CLOSED"

# El archivo real de credenciales no se versiona: lleva la contrasena de la red.
# Si existe se usa; si no, se cae a la plantilla. Asi un clon limpio construye la
# imagen sin pasos manuales previos, que es el requisito de reproducibilidad del
# enunciado: la imagen arranca igual, solo que sin conectarse, y el aviso de
# bitbake lo deja claro en el log.
def robot_wifi_conf(d):
    import os
    real = os.path.join(d.getVar('THISDIR'), 'files', 'wpa_supplicant-wlan0.conf')
    if os.path.exists(real):
        return 'wpa_supplicant-wlan0.conf'
    bb.warn("wifi-config: no se encontro wpa_supplicant-wlan0.conf, se usa la "
            "plantilla. EL ROBOT NO SE CONECTARA AL WIFI. Copie el .sample y "
            "ponga la red real; ver meta-robot/README.md")
    return 'wpa_supplicant-wlan0.conf.sample'

WIFI_CONF = "${@robot_wifi_conf(d)}"

SRC_URI = " \
    file://${WIFI_CONF} \
    file://25-wlan.network \
"

S = "${WORKDIR}"

do_install() {
    # Credenciales: modo 600, solo root las puede leer.
    install -d ${D}${sysconfdir}/wpa_supplicant
    install -m 600 ${WORKDIR}/${WIFI_CONF} \
        ${D}${sysconfdir}/wpa_supplicant/wpa_supplicant-wlan0.conf

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

# El contenido depende de un archivo que puede o no existir en el arbol de
# fuentes, asi que la receta no es apta para compartirse por sstate entre
# maquinas: cada host la reconstruye con sus propias credenciales.
BB_DONT_CACHE = "1"
