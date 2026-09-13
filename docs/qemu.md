# Imagen completa en QEMU (`runqemu ... nographic`)

Arranca **`robot-image` completa** —systemd, `robot-server` como servicio, el panel
web y la navegación autónoma— en una máquina ARM64 emulada, sin la Raspberry Pi.
Complementa las otras dos formas de probar sin el kit:

| Qué | Cómo | Prueba |
|---|---|---|
| Biblioteca en la laptop | `sim/construir.sh` | `librobot` contra el robot simulado, con gcc del host |
| Binario ARM suelto | `sim/construir_arm.sh` (`qemu-aarch64`, modo usuario) | que la compilación cruzada produce binarios que ejecutan |
| **Imagen completa** | **`MACHINE=qemuarm64-robot runqemu robot-image`** (este documento) | que la imagen construida por Yocto arranca y levanta el servicio solo |

## La máquina `qemuarm64-robot`

Definida en `meta-robot/conf/machine/qemuarm64-robot.conf`. Es la `qemuarm64` de poky
con dos diferencias:

- **Tune Cortex-A72**, el mismo de la RPi4. Todo el userspace (glibc, systemd,
  mpg123, libmicrohttpd…) sale del `sstate-cache` del build de `raspberrypi4-64`: solo
  se compilan el kernel (`linux-yocto`) y el propio QEMU. Los binarios que corren en
  la emulación son los mismos que corren en la Pi.
- **Reenvío del puerto 8080** en modo `slirp`, para abrir el panel desde el navegador
  del host.

Qué cambia en la imagen respecto de la de la Pi:

| | `raspberrypi4-64` | `qemuarm64-robot` |
|---|---|---|
| Kernel | `linux-raspberrypi` | `linux-yocto` con virtio |
| `librobot` | enlazada a `pigpiod` (GPIO real) | enlazada a `sim/pigpio_sim.c` + `sim/mundo.c` (`-DROBOT_SIM=ON`) |
| `pigpiod` | sí (`Requires=` en la unidad) | no; se quita el `Requires=` de `robot-server.service` |
| WiFi, firmware Broadcom, `kernel-module-*` de la RPi | sí | no (`IMAGE_INSTALL:append:rpi`) |
| Red | `wlan0` por `wpa_supplicant` | `eth0` virtio por DHCP de `slirp` |

El código de `lib/` y `server/` es exactamente el mismo; lo único que cambia es contra
qué se enlaza la biblioteca. El simulador mueve un robot virtual por una sala de
4×4 m con muebles y responde los sensores midiendo sobre ese mundo (ver
`sim/README.md`).

## Construir

Desde el entorno de build (`source oe-init-build-env build`), con el `local.conf` al
día respecto de `meta-robot/conf/local.conf.sample` (en particular `MACHINE ?=` y los
`:append:rpi`):

```bash
MACHINE=qemuarm64-robot bitbake robot-image
```

La primera vez compila `linux-yocto`, `qemu-system-native` y las pocas recetas propias
de la máquina; lo demás sale del sstate. El `PACKAGECONFIG:remove` de
`qemu-system-native` en el `local.conf.sample` evita que el build arrastre SDL, Mesa y
LLVM nativos, que solo sirven para la ventana gráfica.

Los artefactos quedan en `tmp/deploy/images/qemuarm64-robot/`, separados de los de la
Pi. Construir una máquina no toca la imagen de la otra.

## Arrancar

```bash
MACHINE=qemuarm64-robot runqemu robot-image nographic slirp
```

> La máquina va en la variable de entorno, **no** como argumento. Con
> `runqemu qemuarm64-robot robot-image ...` runqemu consulta `bitbake -e` sin la imagen
> y aborta con `IMAGE_LINK_NAME wasn't set to find corresponding .qemuboot.conf file`.
> Alternativa equivalente: pasarle directamente
> `tmp/deploy/images/qemuarm64-robot/robot-image-qemuarm64-robot.rootfs.qemuboot.conf`.

- `nographic`: la consola serie (`ttyAMA0`) sale en la misma terminal. Usuario `root`
  sin contraseña (`debug-tweaks`).
- `slirp`: red de usuario, **no necesita `sudo`**. Puertos reenviados:
  - `http://localhost:8080` → panel de control (usuario `user1`, contraseña `password1`)
  - `ssh -p 2222 root@localhost`
- Salir de QEMU: `Ctrl-a x`, o `poweroff` desde la consola.

Si runqemu encuentra ocupado un puerto, toma el siguiente libre y lo imprime al
arrancar (`Port forward: ...`).

## Qué verificar dentro

```bash
systemctl status robot-server        # activo, habilitado desde la receta
journalctl -u robot-server -f        # log del servidor
systemctl kill -s SIGKILL robot-server; sleep 7
systemctl show robot-server -p MainPID -p NRestarts
                                     # Restart=on-failure lo levanta con otro PID
```

La imagen no trae `systemd-analyze` (tampoco la de la Pi): para medir el tiempo de
arranque hay que agregar el paquete `systemd-analyze` a la imagen. Las utilidades de
consola son las de BusyBox (`head -n 5`, no `head -5`).

Desde el navegador del host: login, cambio de modo, controles manuales, telemetría de
sensores y el mapa actualizándose mientras el robot virtual navega en modo autónomo.

### Resultado de la primera prueba (11 de setiembre de 2026)

| Verificación | Resultado |
|---|---|
| Arranque con `runqemu ... nographic slirp` | kernel `linux-yocto` 6.6, systemd, login en `ttyAMA0` |
| `robot-server` al arrancar | `enabled` / `active`, sin `Requires=pigpiod` |
| `GET /` y `/api/status` sin sesión | 200 y 401 |
| Login `user1` (contraseña mala / buena) | 401 / 200 |
| Modo manual + avanzar | la distancia frontal baja de 218,9 a 177,5 cm: el robot virtual se mueve |
| Modo autónomo 20 s | el mapa pasa de 57 a 78 celdas visitadas; LEDs siguen el modo |
| `kill -9` al servidor | vuelve solo en ~5 s con otro PID, `NRestarts=1` |
| Audio | `snd_pcm_open: No such file or directory` (esperado, ver límites) |
| Recursos en la VM | rootfs 98 MB usados, ~50 MB de RAM en uso |

## Límites

- **No sustituye al target.** Es otra máquina (`virt`), otro kernel y un simulador en
  lugar del GPIO real. Las evidencias de ejecución en la Pi (issues #7 y #10) siguen
  pendientes del kit.
- **Audio.** `lib_audio` abre `hw:1,0`, el jack de la RPi4; la máquina virtual no tiene
  tarjeta de sonido, así que la reproducción falla con un error en el journal y el
  servidor sigue funcionando.
- **Métricas.** El tiempo de arranque, la RAM y la CPU medidos bajo emulación (TCG, sin
  KVM en un host x86) **no** representan a la Pi. Las métricas del enunciado se toman
  en el hardware real.
