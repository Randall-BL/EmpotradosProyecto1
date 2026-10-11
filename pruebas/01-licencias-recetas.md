# Prueba 01 — Licencias de las recetas `librobot` y `robot-server`

| | |
|---|---|
| **Fecha** | 11 de octubre de 2026 |
| **Commit probado** | `b1187e1` (mismo árbol que `main` en `82f6472`, tras el PR #65) |
| **Entorno** | WSL2, Ubuntu 24.04.3 LTS, Python 3.12.3, clon con saltos de línea LF |
| **Tipo** | Emulación de `do_fetch`, `do_unpack` y `do_populate_lic`. **No ejecuta BitBake.** |
| **Resultado** | **Pasa** |

## Qué verifica

El commit `11e2fa6` borró el `LICENSE` de la raíz, pero `librobot_1.0.bb` y
`robot-server_1.0.bb` lo siguen pidiendo en `SRC_URI` y en `LIC_FILES_CHKSUM`. El
PR #65 lo restauró y actualizó el md5 en las dos recetas. Esta prueba comprueba que
quedó coherente:

1. Cada `file://` del `SRC_URI` existe en alguna ruta de `FILESEXTRAPATHS` o `FILESPATH`.
2. Tras desempaquetar respetando `;subdir=`, el `CMakeLists.txt` queda en `${S}`.
3. El archivo de `LIC_FILES_CHKSUM` existe donde la receta lo busca y su md5 es el
   declarado.
4. El md5 de `pigpio_1.0.bb` coincide con el `UNLICENCE` del `SRCREV` fijado.

## Cómo se ejecuta

Desde la raíz del repositorio, en un clon con saltos de línea LF:

```sh
python3 pruebas/herramientas/verificar_licencias.py .
```

Devuelve 0 si todo coincide y 1 si falta un archivo o un md5 no corresponde.

Para `pigpio`, que se descarga por `git://`:

```sh
REV=c33738a320a3e28824af7807edafda440952c05d
curl -fsSL https://raw.githubusercontent.com/joan2937/pigpio/$REV/UNLICENCE | md5sum
```

## Resultado

```text
commit probado: b1187e1 cambios en la documentación y faltan pruebas finales de simulación
md5sum LICENSE: 6b4015c4e5d69df853a3874986fba2e7   (CR en el archivo: 0)

== meta-robot/recipes-multimedia/librobot/librobot_1.0.bb ==
  SRC_URI: 27 entradas file://, 27 resueltas, 0 faltantes
  S = ${WORKDIR}/lib  ->  CMakeLists.txt presente
  LIC_FILES_CHKSUM: declarado 6b4015c4e5d69df853a3874986fba2e7
                    calculado 6b4015c4e5d69df853a3874986fba2e7  ->  COINCIDE

== meta-robot/recipes-robot/robot-server/robot-server_1.0.bb ==
  SRC_URI: 14 entradas file://, 14 resueltas, 0 faltantes
  S = ${WORKDIR}/src  ->  CMakeLists.txt presente
  LIC_FILES_CHKSUM: declarado 6b4015c4e5d69df853a3874986fba2e7
                    calculado 6b4015c4e5d69df853a3874986fba2e7  ->  COINCIDE

RESULTADO: OK (0 problemas)

== pigpio_1.0.bb: UNLICENCE del SRCREV fijado ==
  SRCREV    c33738a320a3e28824af7807edafda440952c05d
  declarado 61287f92700ec1bdf13bc86d8228cd13
  calculado 61287f92700ec1bdf13bc86d8228cd13  ->  COINCIDE
```

### Control negativo

La misma verificación sobre `11e2fa6`, el commit anterior al arreglo, detecta el
defecto. Confirma que la prueba no pasa por omisión:

```text
commit de control: 11e2fa6 Más cambios de docu

== meta-robot/recipes-multimedia/librobot/librobot_1.0.bb ==
  FALTA  file://LICENSE;subdir=lib   (SRC_URI)
  SRC_URI: 27 entradas file://, 26 resueltas, 1 faltantes
  S = ${WORKDIR}/lib  ->  CMakeLists.txt presente
  FALLO  LIC_FILES_CHKSUM apunta a un archivo que no existe: /tmp/workdir-librobot-sc80v6wv/lib/LICENSE

== meta-robot/recipes-robot/robot-server/robot-server_1.0.bb ==
  FALTA  file://LICENSE   (SRC_URI)
  SRC_URI: 14 entradas file://, 13 resueltas, 1 faltantes
  S = ${WORKDIR}/src  ->  CMakeLists.txt presente
  FALLO  LIC_FILES_CHKSUM apunta a un archivo que no existe: /tmp/workdir-robot-server-x_1pk4ot/LICENSE

RESULTADO: FALLO (4 problemas)
```

## Límites

- **Es una emulación.** Reproduce la búsqueda de archivos y la comparación de md5,
  no el fetcher ni las clases de BitBake. La confirmación definitiva es
  `bitbake -c populate_lic librobot robot-server`, que queda pendiente.
- **Depende de los saltos de línea.** En un clon de Windows con `core.autocrlf=true`
  el `LICENSE` sale con CRLF y su md5 es `0840020f639996f18299c696ece454be`, no el de
  las recetas. BitBake debe correr sobre un clon hecho en Linux.
- `alsa-config` y `wifi-config` declaran `LICENSE = "CLOSED"` y no llevan checksum,
  así que no hay nada que comparar en ellas.
