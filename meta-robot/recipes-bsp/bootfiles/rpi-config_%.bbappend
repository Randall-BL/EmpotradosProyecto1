# Ajustes de config.txt del bootloader de la Raspberry Pi 4.
#
#   dtparam=audio=on        habilita el driver de la salida de audio PWM analogica
#   dtoverlay=audremap,pins_18_19
#                           saca esa salida por GPIO 18 y 19 en vez del jack de
#                           3.5 mm: GPIO 18 va al filtro RC y al amplificador
#                           PAM8403 (docs/hardware-sensores.md). No 12/13, que
#                           son el PWM de los motores.
#   audio_pwm_mode=2        modo PWM de mayor calidad para esa salida
#   dtparam=krnbt=off       desactiva Bluetooth: el robot no lo usa y libera la UART
#   hdmi_ignore_edid_audio  evita que el kernel enrute el audio al HDMI
#   hdmi_force_hotplug      fuerza salida HDMI aunque no haya monitor, util para depurar
#
# ENABLE_I2C agrega dtparam=i2c_arm=on: el bus I2C-1 en GPIO 2/3, donde va el
# MPU-6050. El .dtbo de audremap no viene en la lista por defecto de
# meta-raspberrypi: local.conf lo agrega a RPI_KERNEL_DEVICETREE_OVERLAYS.

ENABLE_UART = "1"
ENABLE_I2C = "1"

RPI_EXTRA_CONFIG = " \
dtparam=audio=on\n\
dtoverlay=audremap,pins_18_19\n\
audio_pwm_mode=2\n\
dtparam=krnbt=off\n\
hdmi_ignore_edid_audio=1\n\
hdmi_force_hotplug=1\n\
hdmi_group=2\n\
hdmi_mode=82\n\
"

MACHINE_FEATURES:append = " wifi"
