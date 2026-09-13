# meta-raspberrypi trae wayland-protocols 1.45 solo para sus maquinas
# (COMPATIBLE_MACHINE = "^rpi$"); en cualquier otra se usa la 1.33 de poky.
#
# La maquina de QEMU (qemuarm64-robot) comparte el tune cortexa72 con la RPi4
# justamente para reutilizar su sstate. Con otra version de wayland-protocols la
# firma de libxkbcommon cambia, y en cascada la de systemd, dbus, avahi,
# pulseaudio y mpg123: bitbake los recompilaria todos. Usando la misma version
# que la Pi, el userspace de la imagen de QEMU sale del sstate tal cual.

COMPATIBLE_MACHINE:qemuarm64-robot = "qemuarm64-robot"
