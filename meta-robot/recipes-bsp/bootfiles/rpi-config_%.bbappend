# Ajustes de config.txt del bootloader de la Raspberry Pi 4.
#
#   dtparam=audio=on        habilita el driver del jack analogico de 3.5 mm
#   audio_pwm_mode=2        modo PWM de mayor calidad para ese jack
#   dtparam=krnbt=off       desactiva Bluetooth: el robot no lo usa y libera la UART
#   hdmi_ignore_edid_audio  evita que el kernel enrute el audio al HDMI
#   hdmi_force_hotplug      fuerza salida HDMI aunque no haya monitor, util para depurar

ENABLE_UART = "1"

RPI_EXTRA_CONFIG = " \
dtparam=audio=on\n\
audio_pwm_mode=2\n\
dtparam=krnbt=off\n\
hdmi_ignore_edid_audio=1\n\
hdmi_force_hotplug=1\n\
hdmi_group=2\n\
hdmi_mode=82\n\
"

MACHINE_FEATURES:append = " wifi"
