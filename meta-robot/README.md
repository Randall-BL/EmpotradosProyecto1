# `meta-robot/` — Capa Yocto del proyecto

Capa propia que aporta todo lo que distingue la imagen del robot de una
`core-image-minimal`: la biblioteca dinámica, el servidor web, sus unidades systemd y
la configuración de WiFi, audio y kernel de la Raspberry Pi 4.

Compatible con **Poky `scarthgap`**.

## Contenido

| Directorio | Qué aporta |
|---|---|
| `conf/layer.conf` | Declaración de la capa: prioridad 10 y dependencias |
| `conf/local.conf.sample` | Bloque de configuración del proyecto para `build/conf/local.conf` |
| `recipes-image/images/` | `robot-image.bb` — la imagen final |
| `recipes-multimedia/librobot/` | `librobot_1.0.bb` — biblioteca dinámica de control |
| `recipes-robot/robot-server/` | `robot-server_1.0.bb` — servidor web + unidad systemd |
| `recipes-support/pigpio/` | `pigpio_1.0.bb` — acceso a GPIO y PWM por hardware |
| `recipes-audio/alsa-config/` | `asound.conf` — fuerza la salida por el jack 3.5 mm |
| `recipes-connectivity/wifi-config/` | WiFi preconfigurado para arranque headless |
| `recipes-bsp/bootfiles/` | `bbappend` de `config.txt` y `cmdline.txt` |
| `recipes-kernel/linux/` | Fragmento de configuración del kernel para el WiFi Broadcom |

## Dependencias

Declaradas en `LAYERDEPENDS_meta-robot`:

`core` · `raspberrypi` · `openembedded-layer` (meta-oe) · `multimedia-layer` · `networking-layer`

## Agregar la capa al build

```bash
bitbake-layers add-layer <ruta-del-repositorio>/meta-robot
bitbake-layers show-layers
```

> Agréguela **desde su ubicación dentro del repositorio clonado**. Las recetas
> `librobot` y `robot-server` toman sus fuentes de `lib/`, `server/` y `audio/` — que
> están en la raíz del repositorio — mediante `FILESEXTRAPATHS`. Si se copia solo esta
> carpeta a otro lado, el `do_fetch` falla.

El procedimiento completo está en [`../docs/yocto-setup.md`](../docs/yocto-setup.md).

## Antes del primer build

Cree el archivo de credenciales de WiFi a partir de la plantilla — el archivo real está
en `.gitignore` y nunca se sube al repositorio:

```bash
cp recipes-connectivity/wifi-config/files/wpa_supplicant-wlan0.conf.sample \
   recipes-connectivity/wifi-config/files/wpa_supplicant-wlan0.conf
```

Si se omite, la receta cae a la plantilla y la imagen se construye de todas formas, con
un `WARNING` de bitbake: el robot arranca pero no se conecta al WiFi. Es deliberado, para
que un clon limpio reproduzca la imagen sin pasos manuales previos.
