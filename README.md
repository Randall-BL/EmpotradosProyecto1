# Proyecto I — Robot Aspiradora Autónomo con Yocto

Curso **CE-1113 Sistemas Empotrados** — Instituto Tecnológico de Costa Rica
Robot aspiradora autónomo sobre **Raspberry Pi 4 Model B** con una imagen Linux mínima
construida con **Yocto Project (Poky, rama `scarthgap`)**.

El robot navega de forma autónoma evadiendo obstáculos, reproduce MP3, y se controla
remotamente desde un servidor web embebido que muestra telemetría y el mapa de recorrido
en tiempo real. Todo el acceso al hardware pasa por una biblioteca dinámica propia
(`librobot.so`) compilada de forma cruzada para ARM.

> Estado del proyecto y desglose de tareas: [`TODO.md`](TODO.md) y los
> [issues del repositorio](https://github.com/Randall-BL/EmpotradosProyecto1/issues).

---

## Estructura del repositorio

```
.
├── lib/                    Biblioteca dinámica librobot.so (motores, sensores, LEDs, audio)
├── server/                 Servidor web embebido en C
│   ├── src/                  Código del servidor (API REST, autenticación, estado)
│   └── www/                  Interfaz web estática (login + dashboard)
├── audio/                  Recursos de audio
│   ├── notify_*.mp3          Sonidos de los 4 eventos del sistema
│   └── music/                Biblioteca musical (no se versiona, ver audio/README.md)
├── meta-robot/             Capa Yocto propia del proyecto
│   ├── conf/                 layer.conf y local.conf.sample
│   ├── recipes-image/        Receta de la imagen final (robot-image)
│   ├── recipes-multimedia/   Receta de librobot
│   ├── recipes-robot/        Receta del servidor web + unidad systemd
│   ├── recipes-support/      Receta de pigpio
│   ├── recipes-audio/        Configuración de ALSA
│   ├── recipes-connectivity/ Configuración de WiFi para arranque headless
│   ├── recipes-bsp/          Ajustes de config.txt y cmdline.txt
│   └── recipes-kernel/       Fragmentos de configuración del kernel
├── docs/                   Documentación técnica, diagramas y evidencias
├── CONTRIBUTING.md         Flujo de trabajo Git y convenciones de código
├── NOTICE.md               Atribución de software de terceros
└── TODO.md                 Desglose completo de requerimientos del enunciado
```

Los directorios de build de Yocto (`build/`, `tmp/`, `sstate-cache/`, `downloads/`) y los
artefactos de compilación (`*.o`, `*.so`, `CMakeFiles/`) están excluidos vía
[`.gitignore`](.gitignore) — solo se versiona código fuente, recetas y documentación.

---

## Documentación

| Documento | Contenido |
|---|---|
| [`docs/yocto-setup.md`](docs/yocto-setup.md) | Preparación del host, capas, `local.conf`, build de la imagen y grabado de la microSD |
| [`docs/sdk.md`](docs/sdk.md) | Toolchain-SDK: generación, uso y verificación de la compilación cruzada |
| [`docs/paquetes.md`](docs/paquetes.md) | Justificación de cada paquete incluido en la imagen |
| [`docs/api-librobot.md`](docs/api-librobot.md) | Referencia de la API pública de la biblioteca de control |
| [`docs/arranque-automatico.md`](docs/arranque-automatico.md) | Unidades systemd, arranque automático y recuperación ante fallos |
| [`docs/hardware-pinout.md`](docs/hardware-pinout.md) | Mapa de pines GPIO — referencia única del cableado |
| [`docs/hardware-aislamiento.md`](docs/hardware-aislamiento.md) | Etapa de potencia, optoacopladores y separación de tierras |
| [`docs/hardware-alimentacion.md`](docs/hardware-alimentacion.md) | Batería, BMS y los dos rieles regulados |
| [`docs/hardware-sensores.md`](docs/hardware-sensores.md) | Sensores, LEDs, audio y diagrama del dominio lógico |
| [`docs/hardware-chasis.md`](docs/hardware-chasis.md) | Diseño del modelo físico, tracción y montaje |
| [`meta-robot/README.md`](meta-robot/README.md) | Contenido de la capa Yocto y cómo agregarla al build |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | Ramas, Conventional Commits, Pull Requests y estilo de código |
| [`NOTICE.md`](NOTICE.md) | Software de terceros y sus licencias |

---

## Construcción rápida

```bash
# 1. Poky y capas, todas en rama scarthgap
mkdir -p ~/yocto && cd ~/yocto
git clone -b scarthgap https://git.yoctoproject.org/poky.git
cd poky
git clone -b scarthgap https://github.com/agherzan/meta-raspberrypi.git
git clone -b scarthgap https://github.com/openembedded/meta-openembedded.git
cd .. && git clone https://github.com/Randall-BL/EmpotradosProyecto1.git

# 2. Entorno de build
cd poky && source oe-init-build-env

# 3. Capas
bitbake-layers add-layer ../meta-raspberrypi
bitbake-layers add-layer ../meta-openembedded/meta-oe
bitbake-layers add-layer ../meta-openembedded/meta-python
bitbake-layers add-layer ../meta-openembedded/meta-multimedia
bitbake-layers add-layer ../meta-openembedded/meta-networking
bitbake-layers add-layer ~/yocto/EmpotradosProyecto1/meta-robot

# 4. Configuración y credenciales de WiFi
cat ~/yocto/EmpotradosProyecto1/meta-robot/conf/local.conf.sample >> conf/local.conf
cp ~/yocto/EmpotradosProyecto1/meta-robot/recipes-connectivity/wifi-config/files/wpa_supplicant-wlan0.conf{.sample,}
# ... editar con la red real ...

# 5. Imagen
bitbake robot-image
```

El detalle completo, incluido el grabado de la microSD y la verificación del rootfs,
está en [`docs/yocto-setup.md`](docs/yocto-setup.md).

---

## Licencia

Ver [`LICENSE`](LICENSE) y [`NOTICE.md`](NOTICE.md).
