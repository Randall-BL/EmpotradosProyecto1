# ─────────────────────────────────────────────────────────────────────────────
#  pigpio — Acceso a GPIO y PWM por hardware en la Raspberry Pi
#
#  No existe receta de pigpio en poky ni en meta-raspberrypi, asi que la capa la
#  aporta. Se elige pigpio sobre las alternativas (libgpiod, WiringPi) porque es
#  la unica que genera PWM por hardware con temporizacion estable, requisito para
#  el control diferencial de los motores DC, y porque su modelo cliente/servidor
#  (pigpiod + pigpiod_if2) permite que varios procesos compartan el GPIO.
#
#  SRCREV fija un commit concreto: sin eso el build no seria reproducible.
# ─────────────────────────────────────────────────────────────────────────────

SUMMARY = "Biblioteca de acceso a GPIO y PWM por hardware para Raspberry Pi"
DESCRIPTION = "pigpio permite controlar los GPIO de la Raspberry Pi, generar PWM por \
hardware y medir pulsos con resolucion de microsegundos."
SECTION = "utils"
HOMEPAGE = "https://github.com/joan2937/pigpio"

LICENSE = "Unlicense"
LIC_FILES_CHKSUM = "file://UNLICENCE;md5=61287f92700ec1bdf13bc86d8228cd13"

SRC_URI = " \
    git://github.com/joan2937/pigpio.git;protocol=https;branch=master \
    file://pigpiod.service \
"
SRCREV = "c33738a320a3e28824af7807edafda440952c05d"

S = "${WORKDIR}/git"

# El Makefile de pigpio asume compilacion nativa: hay que forzarle el compilador
# cruzado y desactivar su strip, que llamaria al strip del host sobre binarios ARM.
EXTRA_OEMAKE += "CC='${CC}'"
EXTRA_OEMAKE += "CROSS_PREFIX=${TARGET_PREFIX}"
EXTRA_OEMAKE += "STRIP=echo"
EXTRA_OEMAKE += "PYINSTALLARGS='--root=$(DESTDIR) --prefix=${prefix}'"
TARGET_CC_ARCH += "${LDFLAGS}"

# Se separa en varios paquetes para instalar en la imagen solo lo necesario:
# el demonio y las bibliotecas, sin las herramientas de linea de comandos ni
# los bindings de Python.
PACKAGES =+ " ${PN}-bin-pigpiod ${PN}-bin-pigs ${PN}-bin-pig2vcd \
              lib${PN} lib${PN}_if lib${PN}_if2"

ALLOW_EMPTY:${PN}     = "1"
ALLOW_EMPTY:${PN}-bin = "1"
ALLOW_EMPTY:${PN}-dbg = "0"
ALLOW_EMPTY:${PN}-dev = "0"

FILES:${PN}-bin-pigpiod    = "${bindir}/pigpiod"
FILES:${PN}-bin-pigs       = "${bindir}/pigs"
FILES:${PN}-bin-pig2vcd    = "${bindir}/pig2vcd"
RDEPENDS:${PN}-bin-pigpiod = "lib${PN}"
RDEPENDS:${PN}-bin         = "${PN}-bin-pigpiod ${PN}-bin-pigs ${PN}-bin-pig2vcd"

FILES:lib${PN}     = "${libdir}/lib${PN}.so.*"
FILES:lib${PN}    =+ "/opt/${PN}/cgi"

# OJO con la 'd': el Makefile de pigpio construye libpigpiod_if.so y
# libpigpiod_if2.so (daemon interface), no libpigpio_if*.so. Con el nombre sin
# 'd' el glob no encuentra nada, los paquetes salen vacios y no se genera el
# RPM, asi que do_rootfs falla con "Unable to find a match: libpigpio_if2".
FILES:lib${PN}_if  = "${libdir}/lib${PN}d_if.so.*"
FILES:lib${PN}_if2 = "${libdir}/lib${PN}d_if2.so.*"

FILES:${PN}-dev += "${libdir}/lib${PN}*.so"
FILES:${PN}-doc  = "${mandir}"

inherit lib_package systemd

# El servicio se asocia al paquete que contiene el binario, no al paquete vacio
# ${PN}: asi la unidad viaja siempre junto al demonio que arranca.
SYSTEMD_PACKAGES = "${PN}-bin-pigpiod"
SYSTEMD_SERVICE:${PN}-bin-pigpiod = "pigpiod.service"
SYSTEMD_AUTO_ENABLE = "enable"

FILES:${PN}-bin-pigpiod += "${systemd_system_unitdir}/pigpiod.service"

do_install() {
    oe_runmake install DESTDIR=${D} prefix=${prefix} mandir=${mandir}

    # El Makefile de pigpio instala los bindings de Python bajo /usr/local.
    # No se usan y solo agregan peso al rootfs.
    rm -rf ${D}/usr/local

    # Unidad systemd del demonio.
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/pigpiod.service \
                    ${D}${systemd_system_unitdir}/pigpiod.service
}
