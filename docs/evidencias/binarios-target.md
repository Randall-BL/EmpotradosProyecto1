# Verificacion de los binarios producidos
# Generado el 2026-10-05 sobre la imagen robot-image del 2026-10-03 (raspberrypi4-64)

Los binarios se tomaron del rootfs de la imagen construida. Tienen el mismo md5 que los
que corren en la Raspberry Pi 4 (ver [`ejecucion-target.md`](ejecucion-target.md)):

```
22e17a74a5199c972607d60130d8b283  usr/lib/librobot.so.1.0.0
ee9213740d0623814673d48fe032150c  usr/bin/robot-server
```

## librobot.so.1.0.0
```
librobot.so.1.0.0: ELF 64-bit LSB shared object, ARM aarch64, version 1 (SYSV), dynamically linked, BuildID[sha1]=a6816f30e2b63396ab5f82a6f2a89524025aa3ac, stripped
```

### SONAME y bibliotecas dinamicas contra las que enlaza
```
  NEEDED               libpigpiod_if2.so.1
  NEEDED               libm.so.6
  NEEDED               libasound.so.2
  NEEDED               libmpg123.so.0
  NEEDED               libc.so.6
  NEEDED               ld-linux-aarch64.so.1
  SONAME               librobot.so.1
```

El acceso a GPIO (`libpigpiod_if2`), al audio (`libasound`, `libmpg123`) y a la
matematica queda dentro de la biblioteca.

## robot-server
```
robot-server: ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV), dynamically linked, interpreter /usr/lib/ld-linux-aarch64.so.1, BuildID[sha1]=5a7e1485cc832c19b3a6987e3aaba6cfbea5bc4b, for GNU/Linux 5.15.0, stripped
```

### Bibliotecas dinamicas contra las que enlaza
```
  NEEDED               libmicrohttpd.so.12
  NEEDED               librobot.so.1
  NEEDED               libm.so.6
  NEEDED               libc.so.6
  NEEDED               ld-linux-aarch64.so.1
```

El servidor enlaza contra `librobot.so.1` y ya no contra `libpigpiod_if2`: todo
acceso al hardware pasa por la biblioteca dinamica propia, como pide el enunciado.

## Artefactos instalados en la imagen por robot-server
```
/opt/robot/audio/canciones/01_Himno_a_la_alegria.mp3
/opt/robot/audio/canciones/02_Para_Elisa.mp3
/opt/robot/audio/canciones/03_Canon_en_Re.mp3
/opt/robot/audio/canciones/04_Pequena_serenata_nocturna.mp3
/opt/robot/audio/canciones/05_En_la_gruta_del_rey_de_la_montana.mp3
/opt/robot/audio/canciones/06_Minueto_en_Sol.mp3
/opt/robot/audio/canciones/07_Cancion_de_cuna.mp3
/opt/robot/audio/canciones/08_Estrellita.mp3
/opt/robot/audio/canciones/Ren - Bitter Sweet Symphony (Live).mp3
/opt/robot/audio/notify_autonomous.mp3
/opt/robot/audio/notify_manual.mp3
/opt/robot/audio/notify_obstacle.mp3
/opt/robot/audio/notify_startup.mp3
/opt/robot/www/dashboard.html
/opt/robot/www/login.html
/usr/bin/robot-server
/usr/lib/systemd/system-preset/98-robot-server.preset
/usr/lib/systemd/system/robot-server.service
```

## Artefactos instalados en la imagen por librobot
```
/usr/lib/librobot.so.1
/usr/lib/librobot.so.1.0.0
```

## Artefactos de librobot-dev (no entran en la imagen)
```
/usr/include/robot/lib_audio.h
/usr/include/robot/lib_imu.h
/usr/include/robot/lib_leds.h
/usr/include/robot/lib_motors.h
/usr/include/robot/lib_odom.h
/usr/include/robot/lib_radar.h
/usr/include/robot/lib_robot.h
/usr/include/robot/lib_sensors.h
/usr/include/robot/lib_servo.h
/usr/lib/librobot.so
```

Paquetes del manifiesto de la imagen (`robot-image-raspberrypi4-64.rootfs.manifest`):

```
librobot1 raspberrypi4_64 1.0
pigpio cortexa72 1.0
pigpio-bin-pigpiod cortexa72 1.0
robot-server raspberrypi4_64 1.0
```
