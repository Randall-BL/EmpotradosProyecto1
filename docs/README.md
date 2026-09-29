# `docs/` — Documentación técnica y evidencias

## Sistema operativo y build

| Archivo | Contenido |
|---|---|
| `yocto-setup.md` | Preparación del host, layers, `local.conf` y construcción de la imagen |
| `sdk.md` | Generación, instalación y uso del Toolchain-SDK para ARM |
| `qemu.md` | La imagen completa en QEMU (`MACHINE=qemuarm64-robot runqemu robot-image nographic slirp`), sin la Raspberry |
| `api-librobot.md` | Referencia de la API pública de `librobot` (motores, radar, servo, MPU-6050, LEDs, odometría, audio) |
| `navegacion-radar.md` | Barrido del radar, velocidad con el MPU-6050, tiempo antes de chocar, evasión y mapa |
| `odometria.md` | Procedimiento de calibración en campo (motores, servo, MPU-6050, umbrales, odometría) |
| `paquetes.md` | Todo paquete agregado sobre la imagen mínima, con su justificación |
| `arranque-automatico.md` | Unidades systemd, arranque automático y recuperación ante fallos |

## Hardware

| Archivo | Contenido |
|---|---|
| `hardware-pinout.md` | Mapa de pines GPIO — **referencia única del cableado** |
| `hardware-aislamiento.md` | Etapa de potencia, optoacopladores y separación de tierras |
| `hardware-alimentacion.md` | Batería, BMS y los dos rieles regulados |
| `hardware-sensores.md` | Radar (HC-SR04 sobre servo de 180°), MPU-6050, LEDs, audio con PAM8403 y diagrama del dominio lógico |
| `hardware-chasis.md` | Diseño del modelo físico, tracción y montaje |

> Antes de energizar cualquier cosa, leer `hardware-aislamiento.md` y
> `hardware-sensores.md`. Ambos contienen procedimientos que, si se saltan, destruyen
> la Raspberry Pi.

Los diagramas de hardware dibujados están en
[`../documentación/hardware-diagramas.pdf`](../documentación/hardware-diagramas.pdf).
Aquí se guardan los fragmentos de `log.do_compile` que evidencian la compilación
cruzada y las capturas de ejecución en el target.
