# Verificacion de los binarios producidos
# Generado el 2026-09-08 00:15 en el host de build

## librobot.so.1.0.0
```
librobot.so.1.0.0: ELF 64-bit LSB shared object, ARM aarch64, version 1 (SYSV), dynamically linked, BuildID[sha1]=0f1a44cdbe673865961a05f1338d004fea4eaad7, with debug_info, not stripped
```

## robot-server
```
robot-server: ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV), dynamically linked, interpreter /usr/lib/ld-linux-aarch64.so.1, for GNU/Linux 5.15.0, not stripped
```

### Bibliotecas dinamicas contra las que enlaza
```
  NEEDED               libmicrohttpd.so.12
  NEEDED               librobot.so.1
  NEEDED               libpigpiod_if2.so.1
  NEEDED               libc.so.6
  NEEDED               ld-linux-aarch64.so.1
```

El servidor enlaza contra librobot.so.1: cumple el requisito del enunciado de
que todo acceso al hardware pase por la biblioteca dinamica propia.

## Artefactos instalados en la imagen por robot-server
```
/opt/robot/audio/notify_autonomous.mp3
/opt/robot/audio/notify_manual.mp3
/opt/robot/audio/notify_obstacle.mp3
/opt/robot/audio/notify_startup.mp3
/opt/robot/www/dashboard.html
/opt/robot/www/login.html
/usr/bin/robot-server
/usr/lib/systemd/system/robot-server.service
```

## Artefactos instalados en la imagen por librobot
```
/usr/include/robot/lib_audio.h
/usr/include/robot/lib_leds.h
/usr/include/robot/lib_motors.h
/usr/include/robot/lib_sensors.h
/usr/lib/librobot.so
/usr/lib/librobot.so.1
/usr/lib/librobot.so.1.0.0
```
