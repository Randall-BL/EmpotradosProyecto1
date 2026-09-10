# `docs/` — Documentación técnica y evidencias

## Sistema operativo y build

| Archivo | Contenido |
|---|---|
| `yocto-setup.md` | Preparación del host, layers, `local.conf` y construcción de la imagen |
| `sdk.md` | Generación, instalación y uso del Toolchain-SDK para ARM |
| `api-librobot.md` | Referencia de la API pública de `librobot` (motores, sensores, LEDs, odometría, audio) |
| `paquetes.md` | Todo paquete agregado sobre la imagen mínima, con su justificación |
| `arranque-automatico.md` | Unidades systemd, arranque automático y recuperación ante fallos |

## Hardware

| Archivo | Contenido |
|---|---|
| `hardware-pinout.md` | Mapa de pines GPIO — **referencia única del cableado** |
| `hardware-aislamiento.md` | Etapa de potencia, optoacopladores y separación de tierras |
| `hardware-alimentacion.md` | Batería, BMS y los dos rieles regulados |
| `hardware-sensores.md` | Sensores HC-SR04, LEDs, audio y diagrama del dominio lógico |
| `hardware-chasis.md` | Diseño del modelo físico, tracción y montaje |

> Antes de energizar cualquier cosa, leer `hardware-aislamiento.md` y
> `hardware-sensores.md`. Ambos contienen procedimientos que, si se saltan, destruyen
> la Raspberry Pi.

Aquí también se guardan los diagramas de arquitectura (HW y SW), los fragmentos de
`log.do_compile` que evidencian la compilación cruzada y las capturas de ejecución en
el target.
