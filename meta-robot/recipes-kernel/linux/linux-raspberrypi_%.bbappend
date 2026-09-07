# Fragmento de configuracion del kernel para el WiFi Broadcom de la RPi4.
#
# La configuracion por defecto del BSP no siempre deja brcmfmac y su transporte
# SDIO habilitados. wifi.cfg los activa explicitamente, junto con cfg80211 y
# rfkill. Se aplica como fragmento (.cfg) en vez de un defconfig completo para no
# quedar atado a una version concreta del kernel.

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "file://wifi.cfg"

KERNEL_CONFIG_FRAGMENTS += "${WORKDIR}/wifi.cfg"
