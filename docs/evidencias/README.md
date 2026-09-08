# Evidencias de compilación cruzada

Evidencia entregable de los issues #8 y #10: el software se compila en el host x86_64 y
se ejecuta en el target ARM.

Generada en un build real de `bitbake robot-image` sobre **poky scarthgap**, host Ubuntu
25.04 (x86_64), target `raspberrypi4-64` (ARM Cortex-A72).

| Archivo | Contenido |
|---|---|
| `log.do_compile.librobot.txt` | Fragmento del log de compilación de la biblioteca dinámica |
| `log.do_compile.robot-server.txt` | Fragmento del log de compilación del servidor web |
| `log.do_compile.pigpio.txt` | Fragmento del log de compilación de pigpio |
| `binarios-target.md` | Arquitectura de los binarios, bibliotecas enlazadas y artefactos instalados |

## Qué demuestra cada cosa

**El compilador es el cruzado, no el del host.** En los tres logs aparece
`aarch64-poky-linux-gcc` con `-mcpu=cortex-a72`, invocado desde
`recipe-sysroot-native/usr/bin/aarch64-poky-linux/`. El `gcc` del host nunca interviene.

**Los binarios son ARM64.** `librobot.so.1.0.0` y `robot-server` son
`ELF 64-bit LSB, ARM aarch64`. En un host x86_64 no se pueden ejecutar: solo corren en el
target.

**El servidor usa la biblioteca propia.** `robot-server` declara
`NEEDED librobot.so.1`. Ningún acceso directo a GPIO desde el servidor — es el
requisito central del enunciado, y aquí queda demostrado a nivel de enlazado.

**El versionado del `.so` es correcto.** `librobot.so.1.0.0` con su `SONAME`
`librobot.so.1` y el enlace de desarrollo `librobot.so`. La receta separa el binario
(paquete `librobot`) de los headers y el enlace (paquete `librobot-dev`), de modo que la
imagen final no cargue con lo segundo.

**La unidad systemd se instala desde la receta.**
`/usr/lib/systemd/system/robot-server.service` aparece entre los artefactos, sin ningún
`systemctl enable` manual en el target.

## Cómo reproducirlo

```bash
# Logs de compilación
cat tmp/work/cortexa72-poky-linux/librobot/1.0/temp/log.do_compile
cat tmp/work/cortexa72-poky-linux/robot-server/1.0/temp/log.do_compile

# Arquitectura de los binarios
file tmp/work/cortexa72-poky-linux/librobot/1.0/image/usr/lib/librobot.so.1.0.0
file tmp/work/cortexa72-poky-linux/robot-server/1.0/image/usr/bin/robot-server

# Bibliotecas enlazadas
aarch64-poky-linux-objdump -p <binario> | grep NEEDED
```

## Lo que todavía falta

- [ ] Capturas del binario **ejecutándose en la Raspberry Pi 4** — requiere el kit
- [ ] Imagen `.wic` completa — el build no llegó a terminar en el host disponible
