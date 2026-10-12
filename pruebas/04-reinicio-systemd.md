# Prueba 04 — Recuperación ante fallo bajo `systemd`

| | |
|---|---|
| **Fecha** | 11 de octubre de 2026 |
| **Commit probado** | `b1187e1` (mismo árbol que `main` en `82f6472`), más la corrección de `sim/prueba_reinicio_systemd.sh` de este mismo PR |
| **Entorno** | WSL2, Ubuntu 24.04.3 LTS, systemd 255 (instancia de usuario), gcc 13.3.0 |
| **Tipo** | Ejecución real: unidad de usuario de `systemd` que corre el servidor sobre el simulador |
| **Resultado** | **Pasa**, en 2 de 2 corridas con el script corregido. El script original también daba `OK`, pero sin comprobar que el servidor respondiera |

## Qué verifica

Requisito del enunciado: el servicio debe recuperarse solo ante un fallo. La prueba
monta una unidad de usuario con las directivas de recuperación de
`robot-server.service`, mata el servidor con `kill -9` y comprueba que `systemd` lo
relanza con otro PID y que vuelve a responder por HTTP.

## Cómo se ejecuta

```sh
cd sim
./construir_servidor.sh
./prueba_reinicio_systemd.sh; echo "salida: $?"
```

Necesita una sesión con `systemd --user` activo. El script crea
`~/.config/systemd/user/robot-server-sim.service` y lo borra al terminar.

En esta ejecución el binario se enlazó con `LD_RUN_PATH` apuntando a la carpeta de
bibliotecas extraídas, porque `systemd` no hereda `LD_LIBRARY_PATH` de la terminal.
Con las bibliotecas instaladas en el sistema no hace falta.

## Resultado con el script original

Terminó con código 0, pero las dos consultas HTTP devolvieron `000`:

```text
arrancado bajo systemd, PID 453; sirve HTTP 000
matando el proceso (kill -9) para simular un crash...
tras el crash: PID 463, NRestarts=1, sirve HTTP 000
OK: systemd reinicio el servidor solo (PID nuevo)
```

El journal de esa corrida muestra además un aviso sobre la unidad de prueba:

```text
17:57:07.603855 systemd: ~/.config/systemd/user/robot-server-sim.service:9: Unknown key name 'StartLimitIntervalSec' in section 'Service', ignoring.
17:57:07.630079 systemd: Started robot-server-sim.service - Robot server (simulador) - prueba de reinicio automatico.
17:57:10.652199 systemd: robot-server-sim.service: Main process exited, code=killed, status=9/KILL
17:57:10.652365 systemd: robot-server-sim.service: Failed with result 'signal'.
17:57:12.902419 systemd: robot-server-sim.service: Scheduled restart job, restart counter is at 1.
```

Dos defectos del script de prueba, ninguno del servidor:

1. **Esperas fijas.** Consultaba el servidor 3 s después de arrancarlo y 5 s después
   de matarlo. Aquí el servidor tardó 3,6 s en abrir el puerto, porque antes escanea
   la playlist (medición en la prueba 03), y tras el fallo hay que sumar los 2 s de
   `RestartSec`. El veredicto solo comparaba los PID, así que decía `OK` aunque el
   servidor no hubiera respondido.
2. **Limitador de arranques mal ubicado.** La unidad de prueba ponía
   `StartLimitIntervalSec` en `[Service]`, donde `systemd` la ignora. La unidad real,
   `meta-robot/recipes-robot/robot-server/files/robot-server.service`, la tiene bien
   puesta en `[Unit]`; la de prueba no la estaba replicando.

## Corrección

En `sim/prueba_reinicio_systemd.sh`:

- Sondea `GET /` hasta recibir 200, con un máximo de 15 s, tras el arranque y tras el
  fallo.
- El veredicto exige además HTTP 200 después del reinicio.
- `StartLimitIntervalSec` y `StartLimitBurst` pasan a `[Unit]`.
- Si el servidor no arranca, la prueba falla y limpia la unidad en vez de ejecutar
  `kill -9` sobre el PID 0.

## Resultado con el script corregido: pasa

| Corrida | Código de salida | PID antes → después | `NRestarts` | HTTP antes / después |
|---|---|---|---|---|
| 1 | 0 | 467 → 502 | 1 | 200 / 200 |
| 2 | 0 | 643 → 678 | 1 | 200 / 200 |

Salida de la corrida 1:

```text
arrancado bajo systemd, PID 467; sirve HTTP 200
matando el proceso (kill -9) para simular un crash...
tras el crash: PID 502, NRestarts=1, sirve HTTP 200
OK: systemd reinicio el servidor solo (PID nuevo) y volvio a servir
```

Journal de `systemd` de la corrida 1, ya sin el aviso:

```text
17:58:46.191591 systemd: Started robot-server-sim.service - Robot server (simulador) - prueba de reinicio automatico.
17:58:50.238081 systemd: robot-server-sim.service: Main process exited, code=killed, status=9/KILL
17:58:50.238234 systemd: robot-server-sim.service: Failed with result 'signal'.
17:58:52.407236 systemd: robot-server-sim.service: Scheduled restart job, restart counter is at 1.
17:58:52.423481 systemd: Started robot-server-sim.service - Robot server (simulador) - prueba de reinicio automatico.
17:58:57.556101 systemd: Stopping robot-server-sim.service - Robot server (simulador) - prueba de reinicio automatico...
17:59:04.336283 systemd: Stopped robot-server-sim.service - Robot server (simulador) - prueba de reinicio automatico.
```

Lectura del journal:

- El proceso muere con `status=9/KILL` y la unidad queda en `Failed with result 'signal'`.
- 2,2 s después `systemd` programa el reinicio (`RestartSec=2`) y arranca el proceso
  nuevo. El contador de reinicios queda en 1.
- Al detener la unidad pasan 6,8 s entre `Stopping` y `Stopped`: es el apagado
  ordenado del servidor, el mismo tiempo medido en la prueba 03.

Tras cada corrida se comprobó que la unidad quedó borrada, que no quedó ningún
servidor vivo y que el puerto 8080 quedó libre.

## Observaciones

- **`docs/evidencias/systemd-reinicio.md` muestra HTTP 200 con el script original.**
  Es coherente con una máquina o una versión del servidor que arrancaba en menos de
  3 s. Con la espera fija el resultado dependía de la velocidad del host.
- La unidad de prueba usa `RestartSec=2`; la real usa `RestartSec=5`.

## Límites

- Es una unidad de **usuario** sobre el binario x86_64 y el simulador. No prueba la
  unidad real instalada por la receta, que corre como `root`, depende de
  `pigpiod.service` y arranca con `multi-user.target`.
- Con la unidad real, la recuperación solo está registrada en la imagen de QEMU
  (`docs/qemu.md`: `kill -9`, vuelve con otro PID y `NRestarts=1`). En la Raspberry
  Pi 4, `docs/evidencias/ejecucion-target.md` registra el arranque automático con
  `NRestarts=0`, sin prueba de fallo.
