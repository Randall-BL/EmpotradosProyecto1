# Atribución de software de terceros

## Proyecto de referencia — `Proyecto1_RobotYocto---Sistemas-Empotrados`

Parte de la capa `meta-robot/`, de las recetas BitBake y del código de `lib/` y `server/`
se basa en el proyecto **Proyecto1_RobotYocto---Sistemas-Empotrados**, publicado bajo
licencia **MIT**:

> MIT License — Copyright (c) 2026 Javier Tenorio Cervantes
> Autores: Javier Tenorio Cervantes, Axel Flores Lara, Julio Varela Venegas,
> Kendall Marin Muñoz.

Se conserva el aviso de copyright y la licencia MIT según exige esa licencia.
Los archivos derivados de ese proyecto lo indican en su encabezado.

## Componentes de terceros incluidos en la imagen

| Componente | Licencia | Uso en el proyecto |
|---|---|---|
| Poky / OpenEmbedded-Core | MIT | Sistema de construcción de la imagen |
| `meta-raspberrypi` | MIT | BSP de la Raspberry Pi 4 |
| `meta-openembedded` | MIT | Recetas de `mpg123`, `libmicrohttpd` y utilidades |
| `pigpio` | Unlicense | Acceso a GPIO y generación de PWM por hardware |
| `libmicrohttpd` | LGPL-2.1+ | Servidor HTTP embebido |
| `mpg123` | LGPL-2.1 | Decodificación MP3 |
| `alsa-lib` / `alsa-utils` | LGPL-2.1+ / GPL-2.0+ | Subsistema de audio |
| `wpa_supplicant` | BSD-3-Clause | Conexión WiFi |

## Audio

Los sonidos de evento `audio/notify_*.mp3` provienen del proyecto de referencia citado
arriba. La biblioteca musical (`audio/music/`) **no se versiona**: cada quien coloca
localmente sus propios archivos y es responsable de sus derechos de uso.
