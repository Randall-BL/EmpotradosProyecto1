# Construcción de la imagen con Yocto

Procedimiento completo, desde un host limpio hasta una Raspberry Pi 4 arrancando la
imagen del proyecto. Versión de Yocto: **Poky, rama `scarthgap` (LTS 5.0)**.

Todas las capas deben estar en la misma rama: mezclar `scarthgap` con `kirkstone` o
`walnascar` provoca errores de `LAYERSERIES_COMPAT` al parsear.

---

## 1. Preparación del host

Host de referencia: Ubuntu 22.04 / 24.04 o Debian 12, 64 bits.

```bash
sudo apt update
sudo apt install -y gawk wget git diffstat unzip texinfo gcc build-essential \
    chrpath socat cpio python3 python3-pip python3-pexpect xz-utils \
    debianutils iputils-ping python3-git python3-jinja2 python3-subunit \
    zstd liblz4-tool file locales libacl1 bmap-tools
sudo locale-gen en_US.UTF-8
```

Requisitos de recursos, medidos en el build de referencia:

| Recurso | Mínimo | Recomendado |
|---|---|---|
| Espacio en disco | 100 GB | 150 GB (con `rm_work` activo) |
| RAM | 8 GB | 16 GB |
| Tiempo del primer build | ~3 h | menos con más núcleos |

> **La RAM es el límite real, no el disco.** En el host del grupo (4 núcleos, 7 GB) un
> build con `BB_NUMBER_THREADS = "3"` agotó los 4 GB de swap y colgó la máquina al 67 %
> del progreso: coincidieron el `do_fetch` del kernel de la Raspberry Pi, el
> `do_compile` de `binutils` y un `do_configure`. No hubo ningún error de receta.
>
> En máquinas de 8 GB o menos hay que bajar a 2 hilos **y** activar la regulación por
> presión de recursos. Ver la sección [Hosts con poca memoria](#hosts-con-poca-memoria).

> El build **no** debe hacerse sobre un sistema de archivos NTFS, exFAT ni sobre un
> directorio de red: BitBake necesita permisos POSIX y enlaces simbólicos.

---

## 2. Clonar Poky y las capas

Todo se clona dentro de un mismo directorio de trabajo, junto al repositorio del
proyecto:

```bash
mkdir -p ~/yocto && cd ~/yocto

git clone -b scarthgap https://git.yoctoproject.org/poky.git
cd poky
git clone -b scarthgap https://github.com/agherzan/meta-raspberrypi.git
git clone -b scarthgap https://github.com/openembedded/meta-openembedded.git
cd ..

# Repositorio del proyecto: aporta la capa meta-robot/
git clone https://github.com/Randall-BL/EmpotradosProyecto1.git
```

| Capa | Para qué |
|---|---|
| `poky/meta`, `meta-poky`, `meta-yocto-bsp` | Núcleo de OpenEmbedded y la distro `poky` |
| `meta-raspberrypi` | BSP de la Raspberry Pi 4: kernel, bootloader, firmware, `config.txt` |
| `meta-openembedded/meta-oe` | Utilidades base y dependencias comunes |
| `meta-openembedded/meta-python` | Dependencias Python de otras recetas |
| `meta-openembedded/meta-multimedia` | `mpg123` (decodificador MP3) |
| `meta-openembedded/meta-networking` | Utilidades de red |
| `EmpotradosProyecto1/meta-robot` | Capa propia: `librobot`, `robot-server`, imagen y configuración |

---

## 3. Inicializar el entorno de build

```bash
cd ~/yocto/poky
source oe-init-build-env          # crea build/ y deja la shell dentro
```

Este comando genera `build/conf/local.conf` y `build/conf/bblayers.conf`. Ninguno de
los dos se versiona: contienen rutas absolutas propias de cada máquina.

---

## 4. Registrar las capas

```bash
bitbake-layers add-layer ../meta-raspberrypi
bitbake-layers add-layer ../meta-openembedded/meta-oe
bitbake-layers add-layer ../meta-openembedded/meta-python
bitbake-layers add-layer ../meta-openembedded/meta-multimedia
bitbake-layers add-layer ../meta-openembedded/meta-networking
bitbake-layers add-layer ~/yocto/EmpotradosProyecto1/meta-robot

bitbake-layers show-layers        # verificar que aparezcan las 8 capas
```

> **`meta-robot` debe agregarse desde su ubicación dentro del repositorio clonado**, no
> copiarse suelta al directorio de Poky. Las recetas `librobot` y `robot-server` toman
> sus fuentes de `lib/`, `server/` y `audio/`, que están en la raíz del repositorio, vía
> `FILESEXTRAPATHS`. Si se mueve solo la carpeta `meta-robot/`, el `do_fetch` falla.

---

## 5. Configurar `local.conf`

Anexe al final de `build/conf/local.conf` el contenido de
[`meta-robot/conf/local.conf.sample`](../meta-robot/conf/local.conf.sample):

```bash
cat ~/yocto/EmpotradosProyecto1/meta-robot/conf/local.conf.sample >> conf/local.conf
```

Ese archivo lleva comentada la razón de cada variable. Lo esencial:

```
MACHINE = "raspberrypi4-64"
DISTRO_FEATURES:append = " systemd usrmerge wifi"
VIRTUAL-RUNTIME_init_manager = "systemd"
LICENSE_FLAGS_ACCEPTED = "synaptics-killswitch"
DISABLE_VC4GRAPHICS = "1"
```

Ajuste `BB_NUMBER_THREADS` y `PARALLEL_MAKE` al host — pero al número de núcleos **y** a
la RAM, no solo a los núcleos:

| RAM del host | `BB_NUMBER_THREADS` |
|---|---|
| ≥ 16 GB | un hilo por núcleo |
| 8–16 GB | la mitad de los núcleos |
| ≤ 8 GB | 2, más la regulación por presión |

### Hosts con poca memoria

Cada tarea de compilación pesada —`gcc-cross`, `binutils`, `glibc`, el kernel— puede
pedir cerca de 2 GB. Con tres corriendo a la vez en una máquina de 7 GB, el sistema entra
en *thrashing* de swap y se cuelga sin emitir un solo error: bitbake reporta
`Bitbake still alive (no events for 3000s)` y termina perdiendo contacto con su servidor.

La defensa es la regulación por presión de recursos, que necesita PSI en el kernel:

```bash
ls /proc/pressure/          # deben aparecer cpu, io y memory
```

Y en `build/conf/local.conf`:

```
BB_NUMBER_THREADS = "2"
PARALLEL_MAKE = "-j 2"

BB_PRESSURE_MAX_MEMORY = "5000"
BB_PRESSURE_MAX_IO     = "15000"
```

Los valores son microsegundos de bloqueo por segundo: mientras la presión los supere,
bitbake **no lanza tareas nuevas**. Limitar los hilos acota el caso promedio; esto acota
el pico, que es lo que en la práctica tumba la máquina.

### Si el build se cuelga

No se pierde nada. Yocto guarda el trabajo terminado en `sstate-cache/`, así que al
relanzar `bitbake robot-image` retoma desde donde quedó en vez de empezar de cero. Basta
con bajar el paralelismo y volver a lanzarlo.

---

## 6. Credenciales de WiFi

El arranque es headless: el robot debe conectarse solo a la red. Cree el archivo de
credenciales (está en `.gitignore`, nunca se sube al repositorio):

```bash
cd ~/yocto/EmpotradosProyecto1
cp meta-robot/recipes-connectivity/wifi-config/files/wpa_supplicant-wlan0.conf.sample \
   meta-robot/recipes-connectivity/wifi-config/files/wpa_supplicant-wlan0.conf
$EDITOR meta-robot/recipes-connectivity/wifi-config/files/wpa_supplicant-wlan0.conf
```

Después de cambiar las credenciales hay que invalidar el estado en caché de esa receta:

```bash
bitbake -c cleansstate wifi-config && bitbake robot-image
```

> **Si se omite este paso la imagen se construye igual.** La receta `wifi-config` cae a
> la plantilla cuando no encuentra el archivo real, para que un clon limpio del
> repositorio produzca la imagen sin pasos manuales previos —que es el requisito de
> reproducibilidad del enunciado. En ese caso bitbake emite un `WARNING` explícito y el
> robot arranca pero **no se conecta a la red**, así que hay que revisar el log del
> build antes de dar por buena la imagen.

---

## 7. Construir

Primero una imagen mínima, para validar que el BSP, el WiFi y el audio funcionan antes
de meter el software del robot:

```bash
bitbake core-image-minimal
```

Luego la imagen completa del proyecto:

```bash
bitbake robot-image
```

El resultado queda en:

```
tmp/deploy/images/raspberrypi4-64/robot-image-raspberrypi4-64.rootfs.wic.bz2
```

---

## 8. Grabar la microSD

```bash
cd tmp/deploy/images/raspberrypi4-64/
bzip2 -dk robot-image-raspberrypi4-64.rootfs.wic.bz2
lsblk                              # identificar el dispositivo, p. ej. /dev/sdb
sudo bmaptool copy --nobmap robot-image-raspberrypi4-64.rootfs.wic /dev/sdX
sync
```

> Verifique el dispositivo con `lsblk` **antes** de ejecutar `bmaptool`. Escribir sobre
> el disco equivocado destruye el sistema del host.

---

## 9. Primer arranque

1. Inserte la microSD y alimente la Raspberry Pi 4.
2. Consola serie por UART (115200 8N1) o SSH una vez que tome IP:
   ```bash
   ssh root@<ip-del-robot>
   ```
3. Verificaciones en el target:
   ```bash
   uname -a                          # kernel y arquitectura aarch64
   ip a                              # wlan0 con IP asignada
   aplay -l                          # tarjeta de audio detectada
   systemctl status robot-server     # servicio activo
   ls /usr/lib/librobot.so*          # biblioteca dinamica instalada
   ```

---

## 10. Verificación del rootfs antes de flashear

Permite detectar módulos o firmware faltantes sin gastar un ciclo de escritura y
arranque:

```bash
ROOTFS=tmp/work/raspberrypi4_64-poky-linux/robot-image/1.0/rootfs

echo "=== Modulos Broadcom (WiFi) ==="
find $ROOTFS/usr/lib/modules -name "*.ko.xz" | grep brcm | sort

echo "=== Modulos de sonido ==="
find $ROOTFS/usr/lib/modules -name "*.ko.xz" | grep snd | sort

echo "=== Firmware WiFi ==="
ls $ROOTFS/lib/firmware/brcm/ | grep 43455

echo "=== Servicios habilitados en el arranque ==="
ls -la $ROOTFS/etc/systemd/system/multi-user.target.wants/

echo "=== Configuracion ALSA ==="
cat $ROOTFS/etc/asound.conf

echo "=== Biblioteca dinamica ==="
find $ROOTFS/usr/lib -name "librobot*"
```

Y el manifiesto de paquetes de la imagen:

```bash
grep -E "librobot|robot-server|pigpio|mpg123" \
  tmp/deploy/images/raspberrypi4-64/robot-image-raspberrypi4-64.rootfs.manifest
```

---

## 11. Comandos de mantenimiento

| Comando | Para qué |
|---|---|
| `bitbake -c cleansstate <receta>` | Fuerza la reconstrucción de una receta ignorando la caché sstate |
| `bitbake -c menuconfig virtual/kernel` | Menú de configuración del kernel |
| `bitbake -e <receta> \| grep ^VARIABLE=` | Muestra el valor final de una variable |
| `bitbake-layers show-recipes <nombre>` | Indica qué capa provee una receta |
| `oe-pkgdata-util list-pkgs \| grep <patrón>` | Busca el nombre exacto de un paquete |
| `bitbake -g robot-image` | Genera el grafo de dependencias de la imagen |

### Nombres de los módulos de kernel

Los paquetes `kernel-module-*` llevan la versión del kernel en el nombre, y esa versión
cambia al actualizar `meta-raspberrypi`. Si `bitbake robot-image` falla con
`Nothing PROVIDES kernel-module-...`, hay que averiguar el nombre vigente:

```bash
bitbake -e virtual/kernel | grep "^PACKAGES"
oe-pkgdata-util list-pkgs | grep brcm
oe-pkgdata-util list-pkgs | grep snd
```

y actualizar los nombres en `meta-robot/recipes-image/images/robot-image.bb`.
El build de referencia usó el kernel **6.6.63-v8**.

---

## Estado

- [x] Dependencias del host documentadas
- [x] Capas y ramas compatibles definidas (`scarthgap`)
- [x] `local.conf` del proyecto versionado como `local.conf.sample`
- [x] Procedimiento de build, grabado y verificación documentado
- [ ] Imagen construida y arrancada en la Raspberry Pi 4 — **pendiente: requiere el kit**
