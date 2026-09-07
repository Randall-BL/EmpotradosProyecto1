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
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | Ramas, Conventional Commits, Pull Requests y estilo de código |
| [`docs/`](docs/) | Guías de Yocto, SDK, justificación de paquetes y evidencias |
| [`NOTICE.md`](NOTICE.md) | Software de terceros y sus licencias |

---

## Licencia

Ver [`LICENSE`](LICENSE) y [`NOTICE.md`](NOTICE.md).
