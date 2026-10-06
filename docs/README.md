# `docs/` — Documentación técnica y evidencias

## Sistema operativo y build

| Archivo | Contenido |
|---|---|
| `yocto-setup.md` | Preparación del host, layers, `local.conf` y construcción de la imagen |
| `sdk.md` | Generación, instalación y uso del Toolchain-SDK para ARM |
| `qemu.md` | La imagen completa en QEMU (`MACHINE=qemuarm64-robot runqemu robot-image nographic slirp`), sin la Raspberry |
| `api-librobot.md` | Referencia de la API pública de `librobot` (motores, radar, servo, MPU-6050, LEDs, odometría, audio) |
| `navegacion-radar.md` | Barrido del radar, velocidad con el MPU-6050, tiempo antes de chocar, evasión y mapa |
| `opcionales.md` | Los tres requerimientos opcionales: desnivel con sensores IR, fin de ciclo de limpieza y playlist persistente |
| `odometria.md` | Procedimiento de calibración en campo (motores, servo, MPU-6050, umbrales, odometría) |
| `paquetes.md` | Todo paquete agregado sobre la imagen mínima, con su justificación |
| `metricas.md` | Métricas de eficiencia: método, resultados y justificación de las desviaciones |
| `arranque-automatico.md` | Unidades systemd, arranque automático y recuperación ante fallos |

## Hardware

| Archivo | Contenido |
|---|---|
| `hardware-pinout.md` | Mapa de pines GPIO — **referencia única del cableado** |
| `hardware-aislamiento.md` | Etapa de potencia, optoacopladores y separación de tierras |
| `hardware-alimentacion.md` | Power bank para la lógica y baterías de 9 V para los motores |
| `hardware-sensores.md` | Radar (HC-SR04 sobre servo de 180°), MPU-6050, LEDs, audio con PAM8403 y diagrama del dominio lógico |
| `hardware-chasis.md` | Carcasa impresa en 3D, tracción, succión y distribución interna |

> Antes de energizar cualquier cosa, leer `hardware-aislamiento.md` y
> `hardware-sensores.md`. Ambos contienen procedimientos que, si se saltan, destruyen
> la Raspberry Pi.

Los diagramas de hardware dibujados están en
[`../documentación/hardware-diagramas.pdf`](../documentación/hardware-diagramas.pdf),
junto con el Documento de Diseño (`documento-diseno.pdf`) y el de Aprendizaje Continuo
(`aprendizaje-continuo.pdf`).
Aquí se guardan los fragmentos de `log.do_compile` que evidencian la compilación
cruzada y las capturas de ejecución en el target.
