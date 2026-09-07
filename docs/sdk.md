# Toolchain-SDK para desarrollo cruzado ARM

El enunciado exige que **todo** el software se compile en el host y se ejecute en el
target. Nada se compila en la Raspberry Pi: no hay compilador en la imagen, y meterlo
rompería el presupuesto de rootfs.

Hay dos caminos de compilación cruzada, y ambos deben funcionar:

| Camino | Cuándo se usa |
|---|---|
| `bitbake robot-image` | Build de la imagen completa. Es el que produce el entregable. |
| Toolchain-SDK | Ciclo de desarrollo diario: compilar y probar un cambio sin reconstruir la imagen entera. |

---

## 1. Generar el SDK

Desde el directorio de build, con el entorno inicializado:

```bash
cd ~/yocto/poky
source oe-init-build-env
bitbake -c populate_sdk robot-image
```

Se genera a partir de `robot-image`, no de `core-image-minimal`, para que el SDK traiga
los headers y las bibliotecas de desarrollo que el software del robot necesita
(`libmicrohttpd`, `mpg123`, `alsa-lib`, `pigpio`).

El instalador queda en:

```
tmp/deploy/sdk/poky-glibc-x86_64-robot-image-cortexa72-raspberrypi4-64-toolchain-5.0.x.sh
```

Pesa alrededor de 1,5 GB y el paso tarda una hora larga la primera vez.

> El instalador `.sh` **no se versiona** (`.gitignore` excluye `*.sdk.sh`). Compártalo
> entre el grupo por almacenamiento externo, o que cada quien lo genere.

---

## 2. Instalar el SDK

En cada máquina del grupo:

```bash
chmod +x poky-glibc-x86_64-robot-image-cortexa72-*-toolchain-5.0.x.sh
./poky-glibc-x86_64-robot-image-cortexa72-*-toolchain-5.0.x.sh -d ~/robot-sdk
```

`-d` fija el directorio de instalación. Se recomienda la misma ruta en todas las
máquinas del grupo para que los scripts de build sean intercambiables.

---

## 3. Usar el SDK

```bash
source ~/robot-sdk/environment-setup-cortexa72-poky-linux
```

Ese script exporta las variables que apuntan al compilador cruzado:

```bash
echo $CC
# aarch64-poky-linux-gcc -mcpu=cortex-a72 -march=armv8-a+crc -fstack-protector-strong ...
echo $SDKTARGETSYSROOT
# /home/<usuario>/robot-sdk/sysroots/cortexa72-poky-linux
```

> Cada terminal nueva necesita su `source`. Si `cmake` encuentra el compilador del host
> en vez del cruzado, casi siempre es que faltó ese paso.

### Compilar la biblioteca

```bash
cd ~/EmpotradosProyecto1/lib
mkdir -p build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake ..
make -j$(nproc)
```

### Compilar el servidor

```bash
cd ~/EmpotradosProyecto1/server/src
mkdir -p build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake ..
make -j$(nproc)
```

Los directorios `build/` están en `.gitignore`: no se versionan artefactos de
compilación.

---

## 4. Verificar que la compilación fue cruzada

Esta es la evidencia que pide el enunciado. **Un binario x86 no arranca en la
Raspberry Pi**, así que conviene comprobarlo antes de copiarlo.

```bash
file librobot.so.1.0.0
# ELF 64-bit LSB shared object, ARM aarch64, version 1 (SYSV), dynamically linked

readelf -h robot-server | grep Machine
# Machine: AArch64

aarch64-poky-linux-objdump -f robot-server | head
```

Si `file` dice `x86-64`, se compiló con el compilador del host: falta el
`source environment-setup-*` o el `-DCMAKE_TOOLCHAIN_FILE`.

### Evidencia desde el log de BitBake

Para el camino de BitBake, la evidencia es el log de la tarea `do_compile`:

```bash
cat tmp/work/cortexa72-poky-linux/librobot/1.0/temp/log.do_compile
cat tmp/work/cortexa72-poky-linux/robot-server/1.0/temp/log.do_compile
```

Debe verse el compilador cruzado en las líneas de compilación:

```
aarch64-poky-linux-gcc  -mcpu=cortex-a72 -march=armv8-a+crc ... -o librobot.so.1.0.0
```

Guarde ese fragmento en `docs/` — es evidencia entregable (issue #10).

---

## 5. Probar en el target

```bash
scp librobot.so.1.0.0 root@<ip-del-robot>:/usr/lib/
scp robot-server      root@<ip-del-robot>:/usr/bin/
ssh root@<ip-del-robot> 'ldconfig && systemctl restart robot-server && systemctl status robot-server'
```

Útil para iterar rápido. Los cambios así **no** persisten como entregable: lo que se
entrega es la imagen que produce `bitbake robot-image`.

---

## 6. Verificar que nada se compila en la Raspberry Pi

```bash
ssh root@<ip-del-robot>
which gcc make cmake        # los tres deben fallar
```

`core-image-minimal` no incluye toolchain, y `robot-image` no la agrega. Que esos
comandos no existan **es** la comprobación.

---

## Estado

- [x] Procedimiento de generación, instalación y uso del SDK documentado
- [x] Método de verificación de la compilación cruzada documentado (`file`, `readelf`, `log.do_compile`)
- [ ] SDK generado con `bitbake -c populate_sdk robot-image` — **pendiente: requiere el host de build**
- [ ] SDK instalado y probado en las máquinas del grupo — **pendiente**
- [ ] Fragmento de `log.do_compile` guardado como evidencia — **pendiente**
