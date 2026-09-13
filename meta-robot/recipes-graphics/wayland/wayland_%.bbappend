# Mismo recorte que meta-raspberrypi aplica a wayland en la RPi (sin vc4graphics
# se quita libwayland-egl), extendido a la maquina de QEMU. Asi el paquete sale
# identico en las dos maquinas y el userspace que depende de el (libxkbcommon,
# systemd, dbus...) se reutiliza del sstate de la Pi en vez de recompilarse.
# Ver wayland-protocols_1.45.bbappend y docs/qemu.md.
do_install:append:qemuarm64-robot () {
    if [ "${@bb.utils.contains("MACHINE_FEATURES", "vc4graphics", "1", "0", d)}" = "0" ]; then
        rm -f ${D}${libdir}/libwayland-egl*
        rm -f ${D}${libdir}/pkgconfig/wayland-egl.pc
    fi
}
