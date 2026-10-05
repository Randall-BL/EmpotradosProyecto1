# Ejecución en la Raspberry Pi 4

Capturado el 2026-10-05 por SSH sobre la Raspberry Pi 4 Model B Rev 1.2, con la imagen
`robot-image` construida el 3 de octubre de 2026. `/usr/bin/robot-server` y
`/usr/lib/librobot.so.1.0.0` tienen el mismo md5 en el target y en el rootfs del build
(ver [`binarios-target.md`](binarios-target.md)).

La placa estaba **sola, sin periféricos**: sin motores, HC-SR04, servo ni MPU-6050. Por
eso el journal avisa que el MPU-6050 no responde; el servidor sigue con el modelo de
los motores, que es el comportamiento previsto.

## Kernel y arquitectura

```
$ uname -a
Linux raspberrypi4-64 6.6.63-v8 #1 SMP PREEMPT Fri Dec  6 10:10:05 UTC 2024 aarch64 GNU/Linux

$ cat /etc/os-release
ID=poky
NAME="Poky (Yocto Project Reference Distro)"
VERSION="5.0.20 (scarthgap)"
VERSION_ID=5.0.20
VERSION_CODENAME="scarthgap"
PRETTY_NAME="Poky (Yocto Project Reference Distro) 5.0.20 (scarthgap)"
CPE_NAME="cpe:/o:openembedded:poky:5.0.20"
```

## El servicio arranca solo

Habilitado por la receta (`preset: enabled`), sin `systemctl enable` manual, y sin
reinicios en este arranque (`NRestarts=0`).

```
$ systemctl status robot-server
● robot-server.service - Servidor de control del robot aspiradora
     Loaded: loaded (/usr/lib/systemd/system/robot-server.service; enabled; preset: enabled)
     Active: active (running) since Mon 2026-10-05 22:30:23 UTC; 25min ago
       Docs: https://github.com/Randall-BL/EmpotradosProyecto1
   Main PID: 270 (robot-server)
      Tasks: 9 (limit: 2130)
        CPU: 15.859s
     CGroup: /system.slice/robot-server.service
             └─270 /usr/bin/robot-server
```

## La biblioteca dinámica instalada por la receta

Solo el binario y su enlace de `SONAME`: `librobot.so` y los headers van en
`librobot-dev`, que no entra en la imagen.

```
$ ls -l /usr/lib/librobot.so*
lrwxrwxrwx    1 root     root            17 Mar  9  2018 /usr/lib/librobot.so.1 -> librobot.so.1.0.0
-rwxr-xr-x    1 root     root         67528 Mar  9  2018 /usr/lib/librobot.so.1.0.0
```

Las fechas de 2018 son las de `SOURCE_DATE_EPOCH`, que Yocto fija para que el build sea
reproducible.

## Journal del servidor en este arranque

Con `-o short-monotonic`: segundos desde que arrancó el kernel.

```
$ journalctl -u robot-server -b -o short-monotonic | head -n 30
[   17.845266] raspberrypi4-64 systemd[1]: Started Servidor de control del robot aspiradora.
[   18.115970] raspberrypi4-64 robot-server[270]: ╔════════════════════════════════════╗
[   18.115970] raspberrypi4-64 robot-server[270]: ║  Servidor Vaccum Robot             ║
[   18.115970] raspberrypi4-64 robot-server[270]: ╚════════════════════════════════════╝
[   18.115970] raspberrypi4-64 robot-server[270]: [state] initialized OK
[   18.119643] raspberrypi4-64 robot-server[270]: [leds] Inicializando pines GPIOs (via pigpiod)
[   18.119969] raspberrypi4-64 robot-server[270]: [leds] GPIO 16 configurado para LED_POWER
[   18.120153] raspberrypi4-64 robot-server[270]: [leds] GPIO 20 configurado para LED_AUTONOMOUS
[   18.120258] raspberrypi4-64 robot-server[270]: [leds] GPIO 21 configurado para LED_MANUAL
[   18.120361] raspberrypi4-64 robot-server[270]: [leds] GPIO 26 configurado para LED_OBSTACLE
[   18.120454] raspberrypi4-64 robot-server[270]: [leds] LED_POWER      GPIO 16 → ON
[   18.120454] raspberrypi4-64 robot-server[270]: [leds] Inicializado GPIOs OK
[   18.120884] raspberrypi4-64 robot-server[270]: [imu] nadie contesta en 0x68: revisar el cableado de SDA/SCL y que AD0 este a GND
[   18.120954] raspberrypi4-64 robot-server[270]: [librobot] MPU-6050 no disponible: la velocidad y el rumbo salen del modelo de los motores
[   18.121439] raspberrypi4-64 robot-server[270]: [radar] barriendo de 0 a 180 grados en pasos de 30
[   18.124109] raspberrypi4-64 robot-server[270]: [audio] Volumen card 1 'PCM': 85% (raw=-1196)
[   18.558190] raspberrypi4-64 robot-server[270]: [audio] Escaneo: 9 pista(s) en './audio'
[   18.558190] raspberrypi4-64 robot-server[270]: [audio] Inicializado — dir='./audio'  pistas=9  volumen=85%
[   18.558190] raspberrypi4-64 robot-server[270]: [auth] inicializado — 1 usuario(s), maximo 4 sesiones simultaneas
[   18.558190] raspberrypi4-64 robot-server[270]: [audio] Notificación: notify_startup.mp3
[   18.558564] raspberrypi4-64 robot-server[270]: [audio] Volumen card 1 'PCM': 92% (raw=-452)
[   23.452748] raspberrypi4-64 robot-server[270]: [audio] Volumen card 1 'PCM': 85% (raw=-1196)
[   23.452748] raspberrypi4-64 robot-server[270]: [main] Modo inicial: AUTONOMO
[   23.453522] raspberrypi4-64 robot-server[270]: [audio] Notificación: notify_autonomous.mp3
[   23.453522] raspberrypi4-64 robot-server[270]: [audio] Volumen card 1 'PCM': 92% (raw=-452)
[   24.702737] raspberrypi4-64 robot-server[270]: [audio] Volumen card 1 'PCM': 85% (raw=-1196)
[   24.702737] raspberrypi4-64 robot-server[270]: [leds] LEDS sincronizados
[   24.703636] raspberrypi4-64 robot-server[270]: [leds] LED_AUTONOMOUS GPIO 20 → ON
[   24.703636] raspberrypi4-64 robot-server[270]: [server] Servidor escuchando en puerto 8080
```

## Nada se compila en el target

```
$ which gcc make cmake
$ echo $?
1
```

## Servicios en ejecución

```
$ systemctl list-units --type=service --state=running
  busybox-klogd.service      Kernel Logging Service
  busybox-syslog.service     System Logging Service
  dbus.service               D-Bus System Message Bus
  getty@tty1.service         Getty on tty1
  pigpiod.service            Demonio pigpio de acceso a GPIO
  robot-server.service       Servidor de control del robot aspiradora
  serial-getty@ttyS0.service Serial Getty on ttyS0
  sshd@….service             OpenSSH Per-Connection Daemon
  systemd-journald.service   Journal Service
  systemd-logind.service     User Login Management
  systemd-networkd.service   Network Configuration
  systemd-resolved.service   Network Name Resolution
  systemd-timesyncd.service  Network Time Synchronization
  systemd-udevd.service      Rule-based Manager for Device Events and Files
  systemd-userdbd.service    User Database Manager
  wpa_supplicant@wlan0.service WPA supplicant daemon (interface-specific version)
```

## Tiempos de arranque de este encendido

Relojes monotónicos de `systemd` (desde el kernel, sin el firmware de la Raspberry Pi):

| Hito | Tiempo |
|---|---|
| Kernel listo, arranca el espacio de usuario (`UserspaceTimestampMonotonic`) | 4.8 s |
| `robot-server` activo (`ActiveEnterTimestampMonotonic`) | 17.8 s |
| Sistema completo (`FinishTimestampMonotonic`) | 17.9 s |
| Servidor escuchando en el puerto 8080 (journal) | 24.7 s |

Entre el 18.6 y el 24.7 s el servidor reproduce `notify_startup.mp3` y
`notify_autonomous.mp3` antes de abrir el socket HTTP: `lib_audio_notify()` bloquea
hasta que termina cada sonido.

## Pendiente con el robot armado

- `vcgencmd get_throttled`: `vcgencmd` no está en la imagen.
- RAM y CPU en operación normal: [`../metricas.md`](../metricas.md).
