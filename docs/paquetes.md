# Paquetes de la imagen y su justificación

El enunciado exige incluir **únicamente** los paquetes estrictamente necesarios y
justificar cada uno agregado más allá de la imagen mínima base.

**Base:** `core-image-minimal` de poky — el punto de partida más pequeño disponible.
Se descartaron `core-image-base` y `core-image-full-cmdline` porque arrastran
utilidades, documentación y servicios que el robot no usa.

**Presupuesto de referencia:** rootfs ≤ 200 MB, arranque ≤ 15 s.

---

## 1. Software propio del proyecto

| Paquete | Justificación |
|---|---|
| `librobot` | Biblioteca dinámica de control. Requerimiento obligatorio: es la única vía de acceso al hardware. |
| `robot-server` | Servidor web de control remoto. Requerimiento obligatorio. |
| `wifi-config` | Credenciales de WiFi y DHCP en `wlan0`. Sin esto el arranque headless no conecta a la red y el robot queda inaccesible. |
| `alsa-config` | `asound.conf` que fuerza la salida por el jack de 3.5 mm. Sin esto ALSA elige la card 0 (HDMI) y no sale audio por el altavoz. |

## 2. Bibliotecas de las que depende el software propio

| Paquete | Justificación |
|---|---|
| `libmicrohttpd` | Servidor HTTP embebido sobre el que corre `robot-server`. Se eligió sobre nginx/lighttpd porque es una biblioteca enlazable en C, no un proceso aparte: no hay que mantener CGI ni un segundo demonio, y el binario resultante son unos pocos cientos de KB. |
| `mpg123` | Decodificación MP3. Requerimiento obligatorio del enunciado. Es el decodificador más liviano con API en C; se descartó GStreamer, que habría traído decenas de MB de plugins. |
| `alsa-utils` | `amixer` y `aplay`, para el control de volumen y para diagnosticar la salida de audio en el target. |
| `alsa-lib` | Runtime de ALSA. Entra como `RDEPENDS` de `librobot` y de `mpg123`, no se lista aparte. |

## 3. GPIO y PWM

| Paquete | Justificación |
|---|---|
| `pigpio`, `libpigpio`, `libpigpio_if2` | Bibliotecas de acceso a GPIO. Se eligió pigpio sobre `libgpiod` y WiringPi porque es la única que genera **PWM por hardware** con temporización estable, indispensable para el control diferencial de los motores DC, y porque mide pulsos con resolución de microsegundos, que es lo que necesitan los HC-SR04. |
| `pigpio-bin-pigpiod` | El demonio `pigpiod`. `pigpiod_if2` es un cliente que se conecta a él por socket, así que sin el demonio la biblioteca no funciona. Además permite que la biblioteca y el servidor compartan el GPIO sin conflictos. |

> Los binarios `pigs` y `pig2vcd` y los bindings de Python de pigpio **no** se instalan:
> la receta los separa en paquetes aparte y `do_install` borra `/usr/local`.

## 4. Red y WiFi

| Paquete | Justificación |
|---|---|
| `wpa-supplicant` | Autenticación WPA2 contra la red. Es la única forma de que un sistema headless se una solo a la red. |
| `wireless-regdb-static` | Base de datos regulatoria. Sin ella el kernel restringe los canales y la potencia de transmisión, y el enlace queda inestable o directamente no asocia. |
| `linux-firmware-rpidistro-bcm43455` | Firmware binario del chip WiFi Broadcom de la RPi 4. Sin él la interfaz `wlan0` ni siquiera aparece. |
| `dhcpcd` | Cliente DHCP: obtiene la IP con que se accede al panel de control. |
| `iw`, `rfkill` | Diagnóstico del enlace inalámbrico en el target (`iw dev wlan0 link`, `rfkill list`). Pesan pocos KB y sin ellos depurar un fallo de WiFi sin pantalla es a ciegas. |

## 5. Módulos de kernel

Se listan explícitamente porque `core-image-minimal` **no** instala el paquete
`kernel-modules` completo, y arrastrarlo entero sumaría decenas de MB de drivers que el
robot no usa.

| Módulo | Justificación |
|---|---|
| `kernel-module-brcmfmac` | Driver del chip WiFi Broadcom. |
| `kernel-module-brcmfmac-wcc` | Componente de control regulatorio que exige `brcmfmac` en el kernel 6.6. |
| `kernel-module-brcmutil` | Utilidades compartidas del driver Broadcom. |
| `kernel-module-snd`, `kernel-module-snd-pcm` | Núcleo del subsistema de sonido ALSA. |
| `kernel-module-snd-bcm2835` | Driver de la salida de audio analógica de la Raspberry Pi. |

> El sufijo de versión (`-6.6.63-v8`) cambia al actualizar `meta-raspberrypi`. Si el
> build falla con `Nothing PROVIDES kernel-module-...`, consulte los nombres vigentes
> con `oe-pkgdata-util list-pkgs | grep brcm` y actualice `robot-image.bb`.

## 6. Solo para desarrollo — **quitar de la imagen de entrega**

| Paquete / feature | Justificación | Acción final |
|---|---|---|
| `ssh-server-openssh` | Única vía de acceso al sistema sin pantalla ni teclado; necesario para recoger las métricas de recursos. | Quitar |
| `openssh-sftp-server` | Copiar archivos y evidencias desde y hacia el target. | Quitar |
| `debug-tweaks` (en `local.conf`) | Deja `root` sin contraseña para poder entrar. | Quitar |

Quitar los tres reduce el rootfs y elimina el acceso remoto sin contraseña, que es un
riesgo de seguridad real en la imagen entregada.

---

## Lo que se dejó fuera a propósito

| Excluido | Motivo |
|---|---|
| Entorno gráfico (X11, Wayland, Sato) | El enunciado prohíbe interfaz gráfica local en el sistema embebido. Se declara `IMAGE_FEATURES:remove` para que ninguna dependencia lo reintroduzca. |
| Driver VC4 / Mesa | `DISABLE_VC4GRAPHICS = "1"` en `local.conf`. Sin pantalla, todo el stack de DRM y OpenGL es peso muerto. |
| GStreamer | `mpg123` cubre el requerimiento de MP3 con una fracción del tamaño. |
| Python 3 | Todo el software del proyecto es C. Un intérprete completo son ~40 MB de rootfs. |
| nginx / lighttpd | `libmicrohttpd` se enlaza dentro del propio servidor: un proceso menos y sin configuración externa que mantener. |
| Bluetooth (`dtparam=krnbt=off`) | El robot no lo usa y libera la UART para la consola serie de depuración. |
| `kernel-modules` (paquete completo) | Decenas de MB de drivers irrelevantes. Se instalan solo los cinco módulos que el robot necesita. |

---

## Medición del tamaño

Estas mediciones alimentan el reporte de eficiencia de recursos (issue #30):

```bash
# Tamaño de la imagen comprimida
ls -lh tmp/deploy/images/raspberrypi4-64/robot-image-raspberrypi4-64.rootfs.wic.bz2

# Tamaño real del rootfs
du -sh tmp/work/raspberrypi4_64-poky-linux/robot-image/1.0/rootfs

# Qué paquete ocupa qué — para decidir qué recortar
cat tmp/deploy/images/raspberrypi4-64/robot-image-raspberrypi4-64.rootfs.manifest
```

## Estado

- [x] `IMAGE_INSTALL` revisado y recortado
- [x] Interfaz gráfica y servicios innecesarios excluidos explícitamente
- [x] Cada paquete agregado sobre la imagen mínima, justificado
- [ ] Tamaño real del rootfs medido — **pendiente: requiere construir la imagen**
- [ ] Paquetes de desarrollo retirados de la imagen de entrega final
