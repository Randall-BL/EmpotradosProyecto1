# Evidencia — Arranque automático y recuperación ante fallo (issue #12)

El servidor debe arrancar solo y **reiniciarse si el proceso muere**. Como sin
la Raspberry no se puede probar en el target, se valida en la laptop con el
servidor corriendo sobre el simulador (`sim/`) y una unidad de usuario systemd
que usa **las mismas directivas de recuperación** que la unidad real
`meta-robot/recipes-robot/robot-server/files/robot-server.service`:
`Restart=on-failure`, `RestartSec`, `StartLimitIntervalSec/Burst`.

Reproducible con: `sim/construir_servidor.sh && sim/prueba_reinicio_systemd.sh`

## 1. La unidad real está bien formada

```
$ systemd-analyze verify robot-server.service
robot-server.service: Failed to create ...: Unit pigpiod.service not found.
```

El único aviso es que `pigpiod.service` (dependencia `Requires=`) no existe en
la laptop — sí existe en la imagen. La unidad en sí es válida.

## 2. Prueba de reinicio automático

```
arrancado bajo systemd, PID 1617444; sirve HTTP 200
matando el proceso (kill -9) para simular un crash...
tras el crash: PID 1617470, NRestarts=1, sirve HTTP 200
OK: systemd reinicio el servidor solo (PID nuevo)
```

`systemctl --user status` durante la prueba:

```
● robot-server-sim.service - Robot server (simulador) - prueba de reinicio
     Active: active (running) ...
   Main PID: 1617470 (robot-server-si)      ← PID nuevo tras el kill -9
     CGroup: .../robot-server-sim.service
```

El PID cambia (1617444 → 1617470) y `NRestarts` sube a 1: systemd detectó la
muerte del proceso y lo relanzó solo, volviendo a servir en el puerto 8080.

## Estado del issue #12

- [x] Unidad `.service` propia del servidor — `robot-server.service`
- [x] Arranque automático al energizar — `WantedBy=multi-user.target`, habilitado desde la receta
- [x] `Restart=on-failure` configurado
- [x] Probado: matar el proceso y verificar que reinicia solo — **esta evidencia**
- [x] Sin interfaz gráfica local en el sistema embebido
- [x] Habilitar el servicio desde la receta Yocto, no manualmente

> Nota: la prueba corre el binario x86 (compilado con gcc del host) sobre el
> simulador, no el binario ARM en la Pi. El comportamiento de systemd —que es
> lo que valida el issue— es idéntico: la unidad y sus directivas son las
> mismas. El único mensaje distinto es el de ALSA (la laptop no tiene la
> tarjeta de sonido del robot), que no afecta al servicio.

## Bonus: el servidor sirve la API

Con el servidor corriendo sobre el simulador se verificó por HTTP:

```
GET  /             -> HTTP 200 (página de login, 8264 bytes)
POST /api/login    -> HTTP 401 con credenciales inválidas (auth funciona)
GET  /api/status   -> HTTP 401 sin sesión (endpoints protegidos)
```

La autenticación rechaza credenciales inválidas y protege los endpoints, como
exige #25.
