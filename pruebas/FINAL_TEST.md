# Prueba final — lo que falta ejecutar

Este documento es para quien vaya a correr las pruebas que **no se pudieron ejecutar**
el 11 de octubre de 2026, porque la máquina usada no tenía Yocto ni la Raspberry Pi.
Está escrito para seguirse de arriba hacia abajo en un host Linux con el entorno de
build del proyecto. Cada bloque dice qué correr, qué debe salir y cuándo se considera
fallo.

## Estado

Ejecutadas el 11 de octubre de 2026 en WSL2 (Ubuntu 24.04.3, gcc 13.3.0), sobre el
commit `b1187e1`, que tiene el mismo árbol que `main` en `82f6472`:

| # | Prueba | Resultado | Registro | PR |
|---|---|---|---|---|
| 01 | Licencias de las recetas (emulación, sin BitBake) | Pasa | [`01-licencias-recetas.md`](01-licencias-recetas.md) | #66 |
| 02 | `librobot` contra el simulador | Pasa, 49 de 49 | [`02-librobot-simulador.md`](02-librobot-simulador.md) | #67 |
| 03 | API del servidor sobre el simulador | Pasa tras corregir el script | [`03-api-servidor-simulador.md`](03-api-servidor-simulador.md) | #68 |
| 04 | Reinicio bajo `systemd` (unidad de usuario) | Pasa tras corregir el script | [`04-reinicio-systemd.md`](04-reinicio-systemd.md) | #69 |
| 05 | Compilación por CMake como en las recetas | Pasa, 6 de 6 | [`05-compilacion-cmake-recetas.md`](05-compilacion-cmake-recetas.md) | #70 |

Cada registro llega a `main` con su PR; mientras alguno siga abierto, su enlace de
la tabla no resuelve.

Pendientes, en el orden en que conviene correrlas:

| # | Prueba | Necesita | Duración aproximada |
|---|---|---|---|
| F1 | Verificación rápida del clon | Linux, Python 3 | 1 min |
| F2 | Licencias con BitBake | Entorno Yocto | 5 a 20 min |
| F3 | Recetas e imagen para las dos máquinas | Entorno Yocto | de minutos a horas, según el `sstate-cache` |
| F4 | Imagen completa en QEMU | Imagen `qemuarm64-robot` de F3 | 15 min |
| F5 | SDK y binario ARM bajo `qemu-aarch64` | Entorno Yocto | 1 h la primera vez |
| F6 | Raspberry Pi 4 | El robot, una microSD y la red WiFi | 30 min |

Las pruebas 01 a 05 no hace falta repetirlas, salvo F1, que reutiliza la 01 como
control de que el clon está sano.

## Cómo registrar cada prueba

Misma convención que las pruebas 01 a 05: **un archivo, un commit y un Pull Request
por prueba**.

1. Rama nueva desde `main`: `git switch -c docs/prueba-<tema> origin/main`.
2. Archivo `pruebas/NN-<tema>.md`, continuando la numeración desde `06`, con estas
   secciones: tabla de cabecera (fecha, commit probado, entorno, tipo, resultado),
   *Qué verifica*, *Cómo se ejecuta*, *Resultado*, *Observaciones* y *Límites*.
3. Pegar la **salida real** de los comandos. Si una prueba falla, se registra el fallo
   con su salida y su causa; no se omite ni se reescribe como si hubiera pasado.
4. Commit con Conventional Commits, por ejemplo
   `test(yocto): documenta la verificacion de licencias con bitbake`, y PR contra
   `main` cuyo cuerpo diga qué se probó y qué no.

Anote siempre el commit probado (`git log -1 --format=%h`) y la versión de las capas.

## Requisitos del entorno

- Host Linux nativo: Ubuntu 22.04 o 24.04, o Debian 12. El build **no** funciona
  sobre NTFS, exFAT, OneDrive ni un directorio de red.
- Paquetes, espacio en disco y capas: los de [`docs/yocto-setup.md`](../docs/yocto-setup.md),
  secciones 1 a 5. Yocto `scarthgap` en todas las capas.
- El repositorio clonado **en Linux**, junto a `poky/`:

  ```bash
  cd ~/yocto
  git clone https://github.com/Randall-BL/EmpotradosProyecto1.git
  cd EmpotradosProyecto1 && git log -1 --format='%h %s'
  ```

  Un clon hecho en Windows con `core.autocrlf=true` no sirve: el `LICENSE` sale con
  CRLF, su md5 cambia y `do_populate_lic` falla.
- Los comandos de BitBake suponen el entorno ya cargado:

  ```bash
  cd ~/yocto/poky && source oe-init-build-env
  ```

  y que `build/conf/local.conf` incluye el contenido de
  `meta-robot/conf/local.conf.sample`.

---

## F1. Verificación rápida del clon

**Objetivo.** Confirmar, antes de gastar tiempo de build, que el clon tiene el
`LICENSE` correcto y que ningún `file://` de las recetas apunta a algo que no existe.

```bash
cd ~/yocto/EmpotradosProyecto1
md5sum LICENSE
python3 pruebas/herramientas/verificar_licencias.py .
```

**Resultado esperado.** `6b4015c4e5d69df853a3874986fba2e7  LICENSE`, dos líneas
`COINCIDE` y `RESULTADO: OK (0 problemas)`.

**Falla si** el md5 es otro (revise los saltos de línea del clon) o si aparece una
línea `FALTA`. El script llega con el PR #66; si aún no está en `main`, basta el
`md5sum`.

## F2. Licencias con BitBake

**Objetivo.** Es la confirmación definitiva del arreglo del PR #65. La prueba 01
solo lo emuló.

```bash
bitbake -c cleansstate librobot robot-server
bitbake -c populate_lic librobot robot-server
find tmp/deploy/licenses -type f -name LICENSE \( -path '*librobot*' -o -path '*robot-server*' \) -exec md5sum {} +

# Las dos recetas son específicas de la máquina: repetir para la de QEMU.
MACHINE=qemuarm64-robot bitbake -c cleansstate librobot robot-server
MACHINE=qemuarm64-robot bitbake -c populate_lic librobot robot-server
```

**Resultado esperado.** Las cuatro órdenes terminan con
`Tasks Summary: ... all succeeded`. Cada `LICENSE` desplegado tiene md5
`6b4015c4e5d69df853a3874986fba2e7`.

**Falla si** aparece alguno de estos mensajes:

- `Unable to get checksum for librobot SRC_URI entry LICENSE: file could not be found`
- `LIC_FILES_CHKSUM points to an invalid file`
- `The LIC_FILES_CHKSUM does not match for file://LICENSE`

En ese caso guarde `tmp/work/*/librobot/1.0/temp/log.do_populate_lic` y su
equivalente de `robot-server`.

## F3. Recetas e imagen para las dos máquinas

**Objetivo.** Comprobar que las recetas compilan con el compilador cruzado y que la
imagen completa se arma para la Raspberry Pi y para QEMU.

```bash
# Raspberry Pi 4 (MACHINE por defecto del local.conf)
bitbake librobot robot-server
bitbake robot-image

# QEMU
MACHINE=qemuarm64-robot bitbake librobot robot-server
MACHINE=qemuarm64-robot bitbake robot-image
```

Evidencia de compilación cruzada y del contenido de la imagen:

```bash
grep -m1 -o 'aarch64-poky-linux-gcc[^ ]*' tmp/work/*/librobot/1.0/temp/log.do_compile
grep -m1 -o 'aarch64-poky-linux-gcc[^ ]*' tmp/work/*/robot-server/1.0/temp/log.do_compile
grep -E 'librobot|robot-server|pigpio|mpg123' \
    tmp/deploy/images/raspberrypi4-64/robot-image-raspberrypi4-64.rootfs.manifest
ls -la tmp/deploy/images/raspberrypi4-64/robot-image-raspberrypi4-64.rootfs.wic.bz2
ls tmp/deploy/images/qemuarm64-robot/ | grep -E 'qemuboot.conf|rootfs.ext4'
```

**Resultado esperado.** Todas las órdenes de `bitbake` terminan con
`all succeeded`. Los `grep` de `log.do_compile` imprimen `aarch64-poky-linux-gcc`. El
manifiesto lista `librobot`, `robot-server`, `mpg123` y los paquetes de `pigpio`.

**Avisos conocidos, que no son fallo.** `wifi-config` emite un `WARNING` si no existe
el archivo real de credenciales y usa la plantilla; la imagen se arma, pero el robot
no se conectará a la red. Para F6 hay que crear ese archivo antes (sección 6 de
`docs/yocto-setup.md`).

**Falla si** alguna tarea de `librobot`, `robot-server` o `robot-image` termina en
`ERROR`, o si `do_package_qa` reporta algo nuevo en las recetas de `meta-robot`.

**Anotar.** El aviso `-Wmisleading-indentation` de `lib/lib_audio.c:826` debe verse
también en `log.do_compile` de `librobot`; se registró en la prueba 02.

## F4. Imagen completa en QEMU

**Objetivo.** Que la imagen construida por Yocto arranca, levanta `robot-server`
sola como servicio y se recupera de un fallo, con la unidad `systemd` real. Detalle
en [`docs/qemu.md`](../docs/qemu.md).

Terminal 1:

```bash
MACHINE=qemuarm64-robot runqemu robot-image nographic slirp
```

Entre como `root`, sin contraseña. Dentro de la máquina virtual:

```bash
systemctl is-enabled robot-server            # enabled
systemctl is-active robot-server             # active
ls /usr/lib/librobot.so*                     # librobot.so.1 y librobot.so.1.0.0
systemctl show robot-server -p MainPID -p NRestarts

systemctl kill -s SIGKILL robot-server
sleep 30
systemctl is-active robot-server             # active
systemctl show robot-server -p MainPID -p NRestarts   # otro PID, NRestarts=1
```

Terminal 2, en el host, mientras QEMU corre:

```bash
URL=http://localhost:8080; CK=$(mktemp)
curl -s -o /dev/null -w 'GET /            %{http_code}\n' $URL/
curl -s -o /dev/null -w 'status sin login %{http_code}\n' $URL/api/status
curl -s -o /dev/null -w 'login malo       %{http_code}\n' -X POST $URL/api/login \
     -d '{"username":"user1","password":"x"}'
curl -s -c $CK -o /dev/null -w 'login bueno      %{http_code}\n' -X POST $URL/api/login \
     -H 'Content-Type: application/json' -d '{"username":"user1","password":"password1"}'
curl -s -b $CK -X POST $URL/api/mode -d '{"mode":"autonomous"}'; echo
for i in 1 2; do
    curl -s -b $CK $URL/api/status | python3 -c "import sys,json; print('visitadas:', json.load(sys.stdin)['map']['visited'])"
    sleep 20
done
```

Salga de QEMU con `Ctrl-a x`.

**Resultado esperado.**

| Verificación | Esperado |
|---|---|
| Servicio al arrancar | `enabled` y `active`, sin intervención |
| Códigos HTTP | 200, 401, 401, 200, en ese orden |
| Cambio de modo | `{"ok":true}` |
| Mapa en modo autónomo | la segunda lectura de `visitadas` es mayor que la primera |
| Tras `SIGKILL` | `active`, PID distinto y `NRestarts=1` |
| Audio | error `snd_pcm_open` en el journal: es esperado, la VM no tiene tarjeta de sonido |

**Falla si** el servicio no está activo al arrancar, si no vuelve tras el `SIGKILL`
o si algún código HTTP difiere. Si runqemu reporta el puerto 8080 ocupado, usa el
siguiente libre y lo imprime al arrancar: ajuste `URL`.

## F5. SDK y binario ARM bajo `qemu-aarch64`

**Objetivo.** Que el Toolchain-SDK produce binarios ARM que ejecutan, sin la
Raspberry Pi. Detalle en [`docs/sdk.md`](../docs/sdk.md).

```bash
bitbake -c populate_sdk robot-image
./tmp/deploy/sdk/poky-glibc-x86_64-robot-image-cortexa72-raspberrypi4-64-toolchain-*.sh -d ~/robot-sdk -y

cd ~/yocto/EmpotradosProyecto1/sim
./construir_arm.sh; echo "salida: $?"
```

**Resultado esperado.** La línea `binario:` dice `ELF 64-bit LSB ... ARM aarch64`. El
programa termina con `TODAS LAS PRUEBAS PASARON` y código de salida 0. En x86_64 son
49 comprobaciones; la evidencia anterior, `docs/evidencias/ejecucion-cruzada-qemu.md`,
registra 47 porque es previa a las dos de los sensores de desnivel.

Compilación cruzada con CMake, en una terminal nueva:

```bash
source ~/robot-sdk/environment-setup-cortexa72-poky-linux
cd ~/yocto/EmpotradosProyecto1/lib && rm -rf build && mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake ..
make -j"$(nproc)" && file librobot.so.1.0.0
```

**Resultado esperado.** `ELF 64-bit LSB shared object, ARM aarch64`.

**Falla si** `file` dice `x86-64` (faltó el `source` o el `-DCMAKE_TOOLCHAIN_FILE`), si
`find_library` no encuentra `pigpiod_if2` en el SDK o si alguna comprobación de
`prueba_librobot_arm` falla. Las comprobaciones de tiempo pueden ser más sensibles
bajo emulación: si falla una de ±15 %, repita la corrida y anote ambas.

## F6. Raspberry Pi 4

**Objetivo.** Lo único que ninguna simulación cubre: la imagen real, en el hardware
real. Ya hay evidencia del arranque en el target y de las métricas con el robot
armado, del 5 y 6 de octubre
([`docs/evidencias/ejecucion-target.md`](../docs/evidencias/ejecucion-target.md) y
[`docs/metricas.md`](../docs/metricas.md)); esta prueba la repite sobre la imagen de
`main` y agrega lo que falta.

Antes de construir la imagen para esta prueba, cree el archivo de credenciales WiFi
(sección 6 de `docs/yocto-setup.md`) y reconstruya `wifi-config` y `robot-image`.

**1. Grabar y arrancar.**

```bash
cd ~/yocto/EmpotradosProyecto1
./scripts/grabar-sd.sh        # pide confirmación; lea el resumen antes de contestar
```

Inserte la microSD, alimente la Raspberry Pi y entre con `ssh root@<ip-del-robot>`.

**2. Sistema base.**

```bash
uname -a                               # aarch64, kernel 6.6.x-v8
ip a show wlan0 | grep 'inet '         # IP asignada
aplay -l                               # tarjeta de audio presente
ls /dev/i2c-1                          # bus del MPU-6050
ls /usr/lib/librobot.so*               # librobot.so.1 y librobot.so.1.0.0
which gcc make cmake                   # los tres deben fallar: nada se compila en el target
systemctl is-enabled robot-server pigpiod
systemctl is-active  robot-server pigpiod
systemctl show robot-server -p NRestarts     # NRestarts=0 recién arrancado
```

**3. Recuperación ante fallo con la unidad real.** No hay evidencia de esto en el
target; hoy solo existe sobre el simulador y sobre QEMU.

```bash
systemctl show robot-server -p MainPID
systemctl kill -s SIGKILL robot-server
sleep 30
systemctl is-active robot-server             # active
systemctl show robot-server -p MainPID -p NRestarts   # otro PID, NRestarts=1
```

Desde el host: `curl -s -o /dev/null -w '%{http_code}\n' http://<ip-del-robot>:8080/`
debe imprimir `200`.

**4. Métricas.** Ya están medidas: 129 MB de rootfs, 19.2 s de arranque, 97 MB de RAM
y 3.6 % de CPU, el 5 de octubre. Repetirlas confirma que la imagen de `main` no las
cambió. Con el robot **en el piso y con espacio libre**, recién arrancado y sin
reiniciar el servicio; el script lo pone en modo autónomo y se va a mover.

```bash
./scripts/medir-metricas.sh <ip-del-robot>
```

Compare la tabla que imprime con la de `docs/metricas.md` y pegue ambas en el registro
de la prueba.

**5. Funcionamiento con los periféricos conectados.** Desde el navegador, en
`http://<ip-del-robot>:8080`, con `user1` / `password1`:

| Verificación | Esperado |
|---|---|
| Login | entra con la contraseña correcta y rechaza una incorrecta |
| Modo manual | avanzar, retroceder y girar mueven el robot en el sentido indicado |
| Modo autónomo | navega y evita un obstáculo puesto al frente |
| Radar | la lectura frontal baja al acercar un objeto |
| MPU-6050 | el journal no dice que el MPU no responde |
| LEDs | encendido, autónomo, manual y obstáculo siguen el estado |
| Audio | suenan los avisos de evento; una canción se reproduce y el volumen cambia |
| Mapa | las celdas visitadas crecen mientras navega |
| Arranque automático | tras cortar y reponer la alimentación, el panel responde sin tocar nada |

**Falla si** el servicio no arranca solo, si no vuelve tras el `SIGKILL` o si alguna
fila de la tabla no se cumple. Registre cada fila con lo observado, no con lo esperado.

---

## Observaciones abiertas

Cosas que dejaron las pruebas 01 a 05 y que nadie ha resuelto. No bloquean la prueba
final, pero conviene tenerlas a la vista al ejecutarla:

- Aviso `-Wmisleading-indentation` en `lib/lib_audio.c:826`.
- `robot_state.h` está duplicado en `lib/` y `server/src/`.
- `sim/prueba_api.sh` imprime los códigos HTTP pero no los compara contra lo esperado.
