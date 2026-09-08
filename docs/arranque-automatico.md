# Arranque automático y recuperación ante fallos

El robot no tiene pantalla, teclado ni login: al energizarlo tiene que quedar operativo
solo. Eso lo resuelven dos unidades systemd, ambas instaladas y habilitadas **desde las
recetas**, nunca a mano en el target.

---

## Las dos unidades

| Unidad | Receta que la instala | Qué hace |
|---|---|---|
| `pigpiod.service` | `pigpio_1.0.bb` | Demonio de acceso a GPIO y PWM |
| `robot-server.service` | `robot-server_1.0.bb` | Servidor web de control |

### Orden de arranque

```
network-online.target ─┐
sound.target ──────────┼──> robot-server.service
pigpiod.service ───────┘
```

`robot-server` declara `Requires=pigpiod.service`, no un simple `Wants`: `librobot` habla
con `pigpiod` por socket, así que sin el demonio **toda** operación de hardware falla.
`Requires` hace que systemd lo arranque primero y detenga el servidor si `pigpiod` se cae.

`pigpiod` arranca con `-l`, que le impide escuchar en interfaces de red. Sin esa bandera
el GPIO del robot quedaría accesible para cualquiera en la misma red WiFi.

---

## Habilitación desde la receta

El enunciado exige que la imagen se reproduzca desde cero sin pasos manuales. Las recetas
usan la clase `systemd` de Yocto:

```bitbake
inherit cmake systemd

SYSTEMD_SERVICE:${PN} = "robot-server.service"
SYSTEMD_AUTO_ENABLE = "enable"
```

`SYSTEMD_AUTO_ENABLE = "enable"` crea el enlace en `multi-user.target.wants/` durante el
build. **No se ejecuta `systemctl enable` en el target.**

En `pigpio` el servicio se asocia a `${PN}-bin-pigpiod` y no al paquete `${PN}`, que está
vacío, para que la unidad viaje siempre junto al binario que arranca:

```bitbake
SYSTEMD_PACKAGES = "${PN}-bin-pigpiod"
SYSTEMD_SERVICE:${PN}-bin-pigpiod = "pigpiod.service"
```

---

## Sin interfaz gráfica local

- `DISABLE_VC4GRAPHICS = "1"` en `local.conf`.
- `IMAGE_FEATURES:remove = "x11-base x11-sato splash"` en `robot-image.bb`.

La única interfaz es la web, servida por `robot-server`.

---

## Validación de las unidades en el host

Antes de construir la imagen, `systemd-analyze verify` revisa la sintaxis y las
dependencias de una unidad sin instalarla ni arrancarla. Detecta claves mal ubicadas,
secciones inválidas y dependencias inexistentes:

```bash
cd meta-robot/recipes-robot/robot-server/files
systemd-analyze verify ./robot-server.service
```

Reporta que `/usr/bin/robot-server` no existe —es normal en el host, el binario vive en
el target— pero cualquier otro mensaje es un defecto real. Ambas unidades del proyecto
pasan esta verificación sin advertencias de sintaxis.

---

## Verificación en el target

### 1. Arranca solo al energizar

Sin tocar nada tras el `poweron`:

```bash
systemctl is-enabled robot-server    # -> enabled
systemctl is-active  robot-server    # -> active
systemctl status robot-server
```

Verificar también que el enlace lo creó el build y no un `systemctl enable` manual:

```bash
ls -l /etc/systemd/system/multi-user.target.wants/robot-server.service
```

### 2. `Restart=on-failure` funciona

Prueba pedida explícitamente por el enunciado: matar el proceso y comprobar que vuelve.

```bash
systemctl show robot-server -p MainPID     # anotar el PID
kill -9 <PID>
sleep 8                                     # RestartSec=5
systemctl status robot-server               # activo otra vez, con PID distinto
journalctl -u robot-server -n 30            # el journal registra el reinicio
```

`kill -9` provoca una terminación anómala, que es lo que `on-failure` cubre. Un
`systemctl stop` es una parada limpia y **no** dispara el reinicio: ese es el
comportamiento correcto.

`StartLimitBurst=5` / `StartLimitIntervalSec=60` cortan el ciclo si el servicio falla
cinco veces en un minuto, para no quemar CPU cuando el fallo es permanente — un sensor
desconectado, por ejemplo.

> Ambas van en la sección **`[Unit]`**, no en `[Service]`. Desde systemd 229 el limitador
> de arranques es propiedad de la unidad; puesto en `[Service]`, systemd lo **ignora en
> silencio** y el límite no existe. Se detectó con `systemd-analyze verify`, que reporta
> `Unknown key 'StartLimitIntervalSec' in section [Service], ignoring`.

Para sacar el servicio de ese estado:

```bash
systemctl reset-failed robot-server
systemctl start robot-server
```

### 3. Tiempo de arranque

Alimenta el reporte de métricas (presupuesto del enunciado: ≤ 15 s):

```bash
systemd-analyze
systemd-analyze blame | head -20
systemd-analyze critical-chain robot-server.service
```

---

## Diagnóstico sin pantalla

Todo va al journal:

```bash
journalctl -u robot-server -f       # en vivo
journalctl -u robot-server -b       # de este arranque
journalctl -u pigpiod -b
journalctl -b -p err                # solo errores del arranque
```

---

## Estado

- [x] Unidad `.service` propia del servidor, escrita y documentada
- [x] Arranque automático habilitado desde la receta, no en el target
- [x] `Restart=on-failure` configurado con `RestartSec` y límite de reintentos
- [x] Ambas unidades validadas con `systemd-analyze verify`, sin advertencias
- [x] Sin interfaz gráfica local en la imagen
- [ ] Probado en el target: matar el proceso y verificar el reinicio — **pendiente: requiere la RPi 4**
- [ ] Tiempo de arranque medido — **pendiente**
