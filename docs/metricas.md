# Métricas de eficiencia de recursos

El enunciado pide medir y reportar, **sobre la imagen final**, el tamaño del rootfs, el
tiempo de arranque hasta que el servicio de control queda operativo, y el uso de RAM y
CPU en operación normal: navegación autónoma, audio y servidor web a la vez.

**Presupuesto de referencia:** rootfs ≤ 200 MB y arranque ≤ 15 s. Toda desviación se
justifica.

---

## Resultados

| Métrica | Referencia | Resultado | Dónde se midió | Estado |
|---|---|---|---|---|
| Rootfs | ≤ 200 MB | **129 MB** | Imagen construida, en el host | Medido |
| Tiempo de arranque | ≤ 15 s | **17 s** | Raspberry Pi 4 con WiFi, 17 de setiembre de 2026 | Medido; repetir sobre la imagen final |
| RAM en operación normal | — | **PENDIENTE** | Raspberry Pi 4 | Pendiente |
| CPU en operación normal | — | **PENDIENTE** | Raspberry Pi 4 | Pendiente |

> **PENDIENTE:** correr `scripts/medir-metricas.sh` contra el robot recién arrancado y
> pegar aquí la tabla que imprime. Ver [Cómo reproducir](#cómo-reproducir).

Como referencia, bajo emulación (`qemuarm64-robot`, 11 de setiembre de 2026) la imagen
usó 98 MB de rootfs y unos 50 MB de RAM. No son las métricas del enunciado: ver
[Qué vale y qué no vale medir en QEMU](#qué-vale-y-qué-no-vale-medir-en-qemu).

---

## Método

### Rootfs

| | |
|---|---|
| Herramienta | `du`, sobre el árbol que BitBake empaqueta en la imagen |
| Por qué | Mide lo que la imagen contiene, sin la holgura de la partición |

```bash
du -sh tmp/work/raspberrypi4_64-poky-linux/robot-image/1.0/rootfs
wic ls tmp/deploy/images/raspberrypi4-64/robot-image-raspberrypi4-64.rootfs.wic.bz2
```

El presupuesto además está **garantizado por construcción**. La tabla de particiones
(`meta-robot/wic/robot-sdimage.wks`) fija el tamaño de cada una con `--fixed-size`:

| Partición | Montaje | Tamaño |
|---|---|---|
| p1 `boot` | `/boot` | 130 MiB |
| p2 `root` | `/` | 180 MiB (188.7 MB) |
| p3 `canciones` | `/opt/robot/audio/canciones` | 180 MiB (188.7 MB) |

Si el contenido crece por encima de ese tope, `bitbake robot-image` falla con
*"Actual rootfs size … is larger than allowed size"* en lugar de producir una partición
de más de 200 MB. En el target, `df -k /` confirma el uso real.

### Tiempo de arranque

| | |
|---|---|
| Herramienta | Relojes monotónicos de `systemd` y el journal |
| Por qué | La imagen no trae `systemd-analyze`; `systemctl` y `journalctl` ya están y dan el mismo dato sin agregar un paquete |

```bash
# Segundos desde que arranco el kernel hasta que el servidor aviso que escucha
journalctl -u robot-server -b -o short-monotonic | grep -m1 "Servidor escuchando"

# Microsegundos hasta que systemd dio la unidad por activa
systemctl show robot-server -p ActiveEnterTimestampMonotonic --value
```

Se reporta el instante en que el servidor **escucha en el puerto 8080**, no el instante
en que `systemd` lanza el proceso: la unidad es `Type=simple`, así que `systemd` la da
por activa apenas la ejecuta, antes de que `robot_init()` calibre el MPU-6050 y de que
el servidor HTTP abra el socket. El servidor escribe con salida por líneas
(`setvbuf(stdout, NULL, _IOLBF, 0)`), de modo que la marca de tiempo del journal es la
del momento real.

Estos relojes arrancan con el kernel y **no incluyen el firmware** de la Raspberry Pi,
que corre antes. Para el tiempo completo desde el reset se graba un video del robot al
energizarlo y se cuenta hasta que enciende el LED verde, que `robot-server` prende al
iniciar.

### RAM

| | |
|---|---|
| Herramienta | `/proc/meminfo` y `/proc/PID/status` |
| Por qué | Lectura directa del kernel, sin depender de la versión de `free` de BusyBox |

- **Del sistema:** `MemTotal − MemAvailable`. `MemAvailable` descuenta la caché que el
  kernel puede liberar; usar `MemFree` haría parecer ocupada memoria que no lo está.
- **Por proceso:** `VmRSS` de `robot-server` y de `pigpiod`.

### CPU

| | |
|---|---|
| Herramienta | `/proc/stat` y `/proc/PID/stat`, dos muestras separadas por 60 s |
| Por qué | Un promedio sobre una ventana, no el valor instantáneo de `top`, que oscila con cada barrido del radar |

- **Del sistema:** porcentaje del tiempo de los cuatro núcleos que no fue `idle` ni
  `iowait`.
- **Por proceso:** diferencia de `utime + stime`, como porcentaje de un núcleo.

### Escenario de operación normal

Las tres cargas a la vez, durante toda la ventana:

1. **Navegación autónoma:** el robot en modo autónomo, en el piso.
2. **Audio:** una pista de la playlist sonando.
3. **Servidor web:** una consulta a `/api/status` cada 500 ms, que es lo que hace el
   panel abierto en un navegador.

---

## Cómo reproducir

`scripts/medir-metricas.sh` monta el escenario por la API, toma todas las mediciones
por SSH y deja el robot detenido al terminar. Imprime la tabla en Markdown.

```bash
./scripts/medir-metricas.sh <ip-del-robot>            # ventana de 60 s
DUR=120 ./scripts/medir-metricas.sh <ip-del-robot>    # ventana de 2 minutos
```

Condiciones:

- **Robot recién arrancado**, sin reiniciar el servicio a mano: el tiempo de arranque
  sale de los relojes de ese arranque.
- **En el piso y con espacio libre:** el script lo pone en modo autónomo y se mueve.
- El script entra por SSH, que la imagen trae; ver [`paquetes.md`](paquetes.md).

---

## Justificación de la desviación en el arranque

El arranque medido, 17 s, excede la referencia en 2 s.

`robot-server` declara `Wants=network-online.target` y `After=network-online.target`:
espera a que la red esté operativa antes de arrancar. En un robot sin cable, eso es la
asociación WPA2 con el punto de acceso más la concesión DHCP, que dependen de la red y
no del robot. Sin red el panel de control no es alcanzable, así que el servicio no
estaría operativo en el sentido del enunciado aunque el proceso ya corriera.

Ese tiempo ya se redujo una vez. El servidor arrancaba a los **130 s** porque
`systemd-networkd-wait-online` agotaba su espera por dos motivos: `eth0` sin cable se
quedaba configurando, y dos clientes DHCP competían por `wlan0`. Marcar la Ethernet
como no requerida y dejar un solo cliente DHCP lo llevó a 17 s (commit `5890dbb`).

Para bajar de 15 s quedaría arrancar el servidor sin esperar a la red: escucha en todas
las interfaces, así que respondería en cuanto la WiFi asociara. El costo es que el LED
verde y el sonido de inicio se adelantarían al momento en que el robot es alcanzable.

---

## Qué vale y qué no vale medir en QEMU

La imagen completa arranca en la máquina `qemuarm64-robot` (ver [`qemu.md`](qemu.md)),
y `scripts/medir-metricas.sh` también corre contra ella:

```bash
SSH_PORT=2222 ./scripts/medir-metricas.sh localhost
```

Sirve para probar el procedimiento, pero **no sustituye la medición en la Raspberry
Pi**: el enunciado pide las métricas sobre la imagen final.

| Métrica | En QEMU | Por qué |
|---|---|---|
| Rootfs | Válido como referencia | Es otra imagen: sin `pigpiod`, sin WiFi ni firmware Broadcom. Por eso da 98 MB y no 129 MB |
| RAM | Cota inferior | El espacio de usuario es el mismo (mismo *tune* Cortex-A72), pero el kernel es otro, falta `pigpiod` y el audio no arranca porque la máquina no tiene tarjeta de sonido |
| CPU | No representativo | QEMU emula el procesador por software (TCG) sobre un host x86: el porcentaje mide al emulador, no al Cortex-A72 |
| Tiempo de arranque | No representativo | Lo que domina el arranque real es la asociación WiFi, que en QEMU no existe |

---

## Estado

- [x] Rootfs medido y dentro del presupuesto
- [x] Método y herramientas documentados para las cuatro métricas
- [x] Script de medición reproducible
- [x] Tiempo de arranque medido en la Raspberry Pi 4 y desviación justificada
- [ ] Tiempo de arranque repetido sobre la imagen final
- [ ] RAM y CPU medidos en operación normal
- [ ] Tabla final copiada al README
