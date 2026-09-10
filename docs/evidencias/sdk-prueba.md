# Evidencia — Toolchain-SDK generado y probado

Complementa `docs/sdk.md` (el procedimiento) con la prueba real de que el SDK
se genera, se instala y **compila para ARM** en una máquina del grupo, sin la
Raspberry. Cubre el issue #8.

## 1. Generación

```
bitbake -c populate_sdk robot-image
```

Instalador producido en `tmp/deploy/sdk/`:

```
poky-glibc-x86_64-robot-image-cortexa72-raspberrypi4-64-toolchain-5.0.20.sh   264 MB
```

## 2. Instalación

Se instala sin privilegios de root, en el `$HOME` del desarrollador:

```
./poky-glibc-x86_64-...-toolchain-5.0.20.sh -y -d ~/robot-sdk
```

Tamaño instalado: 1,8G.

> Nota: si el perfil del host tiene `LD_LIBRARY_PATH` definido (p. ej. por
> OpenMPI), hay que `unset LD_LIBRARY_PATH` antes de cargar el entorno del SDK;
> el propio script del SDK lo advierte.

## 3. Prueba de compilación cruzada

```console
$ unset LD_LIBRARY_PATH
$ . ~/robot-sdk/environment-setup-cortexa72-poky-linux
$ echo $CC
aarch64-poky-linux-gcc  -mcpu=cortex-a72+crc  -fstack-protector-strong ...  --sysroot=.../sysroots/cortexa72-poky-linux

$ $CC hola_sdk.c -o hola_sdk
$ file hola_sdk
hola_sdk: ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV),
          dynamically linked, interpreter /lib/ld-linux-aarch64.so.1, ...

$ ./hola_sdk
bash: ./hola_sdk: no se puede ejecutar fichero binario: Formato de ejecutable incorrecto
```

El binario es **ARM aarch64** y **no corre en el host x86** — la prueba
objetiva de que el SDK cross-compila para la Raspberry Pi 4 y de que ningún
binario se construye de forma nativa en el target.

## Estado

- [x] `bitbake -c populate_sdk` genera el instalador
- [x] Instalado y probado en una máquina del grupo (esta laptop)
- [x] Verificado que compila para ARM y no para el host (`file` + fallo de ejecución en x86)
- [x] Procedimiento documentado (`docs/sdk.md` + esta evidencia)

Pendiente de la Pi: ejecutar el binario resultante en el target real
(se cubrirá junto con el arranque de la imagen).
