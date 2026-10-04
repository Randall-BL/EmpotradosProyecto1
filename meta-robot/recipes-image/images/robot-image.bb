# ─────────────────────────────────────────────────────────────────────────────
#  robot-image — Imagen final del robot aspiradora
#
#  Parte de core-image-minimal, no de core-image-base ni core-image-full-cmdline:
#  minimal es el punto de partida mas pequeno de poky y obliga a agregar
#  explicitamente todo lo demas, que es justo lo que exige el enunciado.
#
#  Cada paquete de IMAGE_INSTALL esta justificado en docs/paquetes.md. No se
#  agrega nada "por si acaso": el presupuesto de rootfs es de 200 MB.
# ─────────────────────────────────────────────────────────────────────────────

require recipes-core/images/core-image-minimal.bb

SUMMARY = "Imagen minima de Linux para el robot aspiradora autonomo (Raspberry Pi 4)"
DESCRIPTION = "Sistema operativo minimo con systemd, WiFi, audio PWM por GPIO hacia un \
amplificador PAM8403, I2C para el MPU-6050, la biblioteca dinamica de control y el \
servidor web de control remoto."

IMAGE_INSTALL:append = " \
    \
    librobot                            \
    robot-server                        \
    \
    libmicrohttpd                       \
    mpg123                              \
    alsa-utils                          \
    alsa-config                         \
"

# dhcpcd solo en QEMU: alli no hay archivo .network para eth0 y es quien pide la
# IP. En la Raspberry la red la lleva systemd-networkd (25-wlan.network), y
# tener los dos clientes DHCP sobre wlan0 hacia que se pelearan por la direccion
# IPv6: networkd marcaba la interfaz como "failed", systemd-networkd-wait-online
# agotaba sus 120 s y robot-server no arrancaba hasta los 2 min 10 s, con la
# interfaz cargando dos IPs a la vez. Ver docs/arranque-automatico.md.
IMAGE_INSTALL:append:qemuall = " dhcpcd"

# Lo que solo existe o solo sirve en la Raspberry Pi: GPIO real, WiFi Broadcom
# y los modulos del kernel de la RPi (i2c-dev e i2c-bcm2835: el bus del
# MPU-6050). La override "rpi" la define
# meta-raspberrypi para todas sus maquinas; la imagen de QEMU
# (qemuarm64-robot, ver docs/qemu.md) no lleva nada de esto.
IMAGE_INSTALL:append:rpi = " \
    pigpio                              \
    libpigpio                           \
    libpigpio_if2                       \
    pigpio-bin-pigpiod                  \
    \
    wpa-supplicant                      \
    wifi-config                         \
    wireless-regdb-static               \
    linux-firmware-rpidistro-bcm43455   \
    iw                                  \
    rfkill                              \
    \
    kernel-module-brcmfmac-6.6.63-v8       \
    kernel-module-brcmfmac-wcc-6.6.63-v8   \
    kernel-module-brcmutil-6.6.63-v8       \
    kernel-module-snd-6.6.63-v8            \
    kernel-module-snd-pcm-6.6.63-v8        \
    kernel-module-snd-bcm2835-6.6.63-v8    \
    kernel-module-i2c-dev-6.6.63-v8        \
    kernel-module-i2c-bcm2835-6.6.63-v8    \
"

# SSH: unica via de acceso al sistema para depurar y para recoger las metricas,
# porque el robot no tiene pantalla ni teclado.
# QUITAR de la imagen de entrega final junto con debug-tweaks.
IMAGE_FEATURES += "ssh-server-openssh"
IMAGE_INSTALL:append = " openssh-sftp-server"

# Sin entorno grafico: el enunciado prohibe interfaz grafica local en el sistema
# embebido. Se declara explicitamente para que ninguna dependencia lo reintroduzca.
IMAGE_FEATURES:remove = "x11-base x11-sato splash"

# Tabla de particiones propia en la Raspberry: la playlist va en una tercera
# particion montada en /opt/robot/audio/canciones, para que ninguna pase de
# 200 MB. Ver meta-robot/wic/robot-sdimage.wks. QEMU arranca del ext4 directo
# y no usa wic.
WKS_FILE:rpi = "robot-sdimage.wks"

# Presupuesto de rootfs del enunciado: 200 MB.
# EXTRA_SPACE en 0 para que el tamano medido sea el real y no quede inflado por
# holgura artificial.
IMAGE_ROOTFS_SIZE ?= "204800"
IMAGE_ROOTFS_EXTRA_SPACE ?= "0"

# ── Nota sobre los kernel-module-* ───────────────────────────────────────────
# El nombre de estos paquetes lleva la version del kernel, que cambia al
# actualizar meta-raspberrypi. Si el build falla con "Nothing PROVIDES
# kernel-module-...", averiguar los nombres vigentes con:
#
#   oe-pkgdata-util list-pkgs | grep brcm
#   oe-pkgdata-util list-pkgs | grep snd
#   oe-pkgdata-util list-pkgs | grep i2c
#
# y actualizarlos aqui. Version usada: 6.6.63-v8.
