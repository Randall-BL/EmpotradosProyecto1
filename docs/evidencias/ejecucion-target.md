# Ejecución en la Raspberry Pi 4

Capturado el 2026-10-06 por SSH sobre la Raspberry Pi 4 Model B Rev 1.2, con la imagen
`robot-image` construida el 5 de octubre de 2026 (`robot-server` abre el puerto antes de
los sonidos de inicio). md5 en el target:

```
4c12a464c7ad6e6b9389b8de28bb3366  /usr/bin/robot-server
22e17a74a5199c972607d60130d8b283  /usr/lib/librobot.so.1.0.0
```

`librobot.so.1.0.0` es la misma de [`binarios-target.md`](binarios-target.md); el
servidor cambió solo en el orden de arranque de `main.c`.

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
     Active: active (running) since Tue 2026-10-06 03:10:27 UTC; 15min ago
       Docs: https://github.com/Randall-BL/EmpotradosProyecto1
   Main PID: 273 (robot-server)
      Tasks: 6 (limit: 2130)
        CPU: 143ms
     CGroup: /system.slice/robot-server.service
             └─273 /usr/bin/robot-server
```

El "15min ago" sale del reloj de pared, que `systemd-timesyncd` corrige al tener red;
la captura se tomó a los 20 s del arranque.

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
[   18.652792] raspberrypi4-64 systemd[1]: Started Servidor de control del robot aspiradora.
[   18.937936] raspberrypi4-64 robot-server[273]: ╔════════════════════════════════════╗
[   18.937936] raspberrypi4-64 robot-server[273]: ║  Servidor Vaccum Robot             ║
[   18.937936] raspberrypi4-64 robot-server[273]: ╚════════════════════════════════════╝
[   18.937936] raspberrypi4-64 robot-server[273]: [state] initialized OK
[   18.941586] raspberrypi4-64 robot-server[273]: [leds] Inicializando pines GPIOs (via pigpiod)
[   18.941903] raspberrypi4-64 robot-server[273]: [leds] GPIO 16 configurado para LED_POWER
[   18.942113] raspberrypi4-64 robot-server[273]: [leds] GPIO 20 configurado para LED_AUTONOMOUS
[   18.942291] raspberrypi4-64 robot-server[273]: [leds] GPIO 21 configurado para LED_MANUAL
[   18.942465] raspberrypi4-64 robot-server[273]: [leds] GPIO 26 configurado para LED_OBSTACLE
[   18.942593] raspberrypi4-64 robot-server[273]: [leds] LED_POWER      GPIO 16 → ON
[   18.942593] raspberrypi4-64 robot-server[273]: [leds] Inicializado GPIOs OK
[   18.943228] raspberrypi4-64 robot-server[273]: [imu] nadie contesta en 0x68: revisar el cableado de SDA/SCL y que AD0 este a GND
[   18.943368] raspberrypi4-64 robot-server[273]: [librobot] MPU-6050 no disponible: la velocidad y el rumbo salen del modelo de los motores
[   18.943902] raspberrypi4-64 robot-server[273]: [radar] barriendo de 0 a 180 grados en pasos de 30
[   18.946077] raspberrypi4-64 robot-server[273]: [audio] Volumen card 1 'PCM': 85% (raw=-1196)
[   19.388026] raspberrypi4-64 robot-server[273]: [audio] Escaneo: 9 pista(s) en './audio'
[   19.388026] raspberrypi4-64 robot-server[273]: [audio] Inicializado — dir='./audio'  pistas=9  volumen=85%
[   19.388026] raspberrypi4-64 robot-server[273]: [auth] inicializado — 1 usuario(s), maximo 4 sesiones simultaneas
[   19.388400] raspberrypi4-64 robot-server[273]: [server] Servidor escuchando en puerto 8080
[   19.388400] raspberrypi4-64 robot-server[273]:   Dev:               http://localhost:8080
[   19.388400] raspberrypi4-64 robot-server[273]:   Red local:         http://<hostname -I>:8080
[   19.388400] raspberrypi4-64 robot-server[273]: [audio] Notificación: notify_startup.mp3
[   19.388639] raspberrypi4-64 robot-server[273]: [audio] Volumen card 1 'PCM': 92% (raw=-452)
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

## Tiempos de arranque

Relojes monotónicos de `systemd` (desde el kernel, sin el firmware de la Raspberry Pi):

| Hito | Imagen del 3 oct | Imagen del 5 oct |
|---|---|---|
| Kernel listo, arranca el espacio de usuario (`UserspaceTimestampMonotonic`) | 4.8 s | 4.3 s |
| `robot-server` activo (`ActiveEnterTimestampMonotonic`) | 17.8 s | 18.7 s |
| Sistema completo (`FinishTimestampMonotonic`) | 17.9 s | 18.7 s |
| **Servidor escuchando en el puerto 8080** (journal) | **24.7 s** | **19.4 s** |

Con la imagen del 3 de octubre el servidor reproducía `notify_startup.mp3` y
`notify_autonomous.mp3` antes de abrir el socket HTTP (`lib_audio_notify()` bloquea
hasta que termina cada sonido): 6 s perdidos. Ahora el puerto se abre a los 0.7 s de
que arranca el proceso y los sonidos suenan después.

Lo que queda lo domina la WiFi. Del journal completo del arranque del 6 de octubre:

```
[   10.456201] wpa_supplicant[250]: Successfully initialized wpa_supplicant
[   17.252998] wpa_supplicant[250]: wlan0: Trying to associate with ca:8f:58:10:2c:d4 (SSID='iPhone de Chris' freq=2437 MHz)
[   18.392930] wpa_supplicant[250]: wlan0: CTRL-EVENT-CONNECTED - Connection to ca:8f:58:10:2c:d4 completed [id=0 id_str=]
[   18.393047] systemd-networkd[199]: wlan0: Gained carrier
```

`wpa_supplicant` tarda 6.8 s en encontrar el punto de acceso (un hotspot de iPhone) y
1.1 s en asociarse; `robot-server` espera a `network-online.target`.

## Pendiente con el robot armado

- `vcgencmd get_throttled`: `vcgencmd` no está en la imagen.
- Calibración del MPU-6050 y lecturas del radar en el journal, con los periféricos conectados.
- RAM y CPU en operación normal: [`../metricas.md`](../metricas.md).
