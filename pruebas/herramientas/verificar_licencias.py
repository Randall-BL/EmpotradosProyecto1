#!/usr/bin/env python3
# Proyecto I - Robot Aspiradora Autonomo — CE-1113, TEC
#
# Emula do_fetch / do_unpack / do_populate_lic de BitBake para las recetas
# librobot y robot-server, SIN BitBake. Sirve para detectar en segundos, y en
# cualquier maquina, los dos errores que rompen esas tareas:
#   - un file:// del SRC_URI que ya no existe en el repositorio;
#   - un md5 de LIC_FILES_CHKSUM que no corresponde al LICENSE versionado.
#
# Para cada receta:
#   1. resuelve cada file:// del SRC_URI contra FILESEXTRAPATHS y FILESPATH;
#   2. copia lo encontrado a un WORKDIR temporal respetando ;subdir=;
#   3. ubica el archivo de LIC_FILES_CHKSUM (relativo a S o absoluto) y compara
#      su md5 con el declarado.
#
# Uso:  python3 pruebas/herramientas/verificar_licencias.py [raiz-del-repo]
#
# Debe correrse sobre un clon con saltos de linea LF (Linux, o Windows con
# core.autocrlf=false): con CRLF el md5 del LICENSE es otro y la prueba falla,
# igual que fallaria BitBake sobre ese mismo clon.
#
# No sustituye a `bitbake -c populate_lic librobot robot-server`.

import hashlib
import re
import shutil
import sys
import tempfile
from pathlib import Path

RAIZ = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
RECETAS = [
    "meta-robot/recipes-multimedia/librobot/librobot_1.0.bb",
    "meta-robot/recipes-robot/robot-server/robot-server_1.0.bb",
]
fallos = 0


def asignaciones(texto, nombre):
    """Valores de todas las asignaciones NOMBRE[:override] = "..." (multilinea)."""
    patron = re.compile(
        r'^(' + re.escape(nombre) + r'(?::[\w:${}-]+)?)\s*(?::=|\?=|\+=|=)\s*"((?:[^"\\]|\\.|\\\n)*)"',
        re.M,
    )
    return [(m.group(1), m.group(2).replace("\\\n", " ")) for m in patron.finditer(texto)]


for rel in RECETAS:
    receta = RAIZ / rel
    texto = receta.read_text(encoding="utf-8")
    thisdir = receta.parent
    pn, pv = receta.stem.split("_")
    print(f"\n== {rel} ==")

    # Rutas de busqueda: FILESEXTRAPATHS primero, luego el FILESPATH por defecto.
    busqueda = []
    for _, valor in asignaciones(texto, "FILESEXTRAPATHS"):
        for p in valor.split(":"):
            if p.strip():
                busqueda.append(Path(p.replace("${THISDIR}", str(thisdir))).resolve())
    busqueda += [thisdir / f"{pn}-{pv}", thisdir / pn, thisdir / "files"]

    workdir = Path(tempfile.mkdtemp(prefix=f"workdir-{pn}-"))
    total = faltan = 0
    for clave, valor in asignaciones(texto, "SRC_URI"):
        for uri in valor.split():
            if not uri.startswith("file://"):
                continue
            ruta, *params = uri[len("file://"):].split(";")
            subdir = next((p.split("=", 1)[1] for p in params if p.startswith("subdir=")), "")
            total += 1
            origen = next((d / ruta for d in busqueda if (d / ruta).exists()), None)
            if origen is None:
                faltan += 1
                print(f"  FALTA  {uri}   ({clave})")
                continue
            destino = workdir / subdir / ruta
            destino.parent.mkdir(parents=True, exist_ok=True)
            if origen.is_dir():
                shutil.copytree(origen, destino, dirs_exist_ok=True)
            else:
                shutil.copy2(origen, destino)
    print(f"  SRC_URI: {total} entradas file://, {total - faltan} resueltas, {faltan} faltantes")
    fallos += faltan

    # inherit cmake configura desde ${S}: ahi tiene que estar el CMakeLists.txt.
    s = dict(asignaciones(texto, "S")).get("S", "${WORKDIR}").replace("${WORKDIR}", str(workdir))
    cmakelists = Path(s) / "CMakeLists.txt"
    print(f"  S = {s.replace(str(workdir), '${WORKDIR}')}  ->  CMakeLists.txt {'presente' if cmakelists.is_file() else 'AUSENTE'}")
    fallos += 0 if cmakelists.is_file() else 1

    chk = dict(asignaciones(texto, "LIC_FILES_CHKSUM"))["LIC_FILES_CHKSUM"].strip()
    ruta, *params = chk[len("file://"):].split(";")
    esperado = next(p.split("=", 1)[1] for p in params if p.startswith("md5="))
    ruta = ruta.replace("${WORKDIR}", str(workdir))
    lic = Path(ruta) if Path(ruta).is_absolute() else Path(s) / ruta
    if not lic.is_file():
        print(f"  FALLO  LIC_FILES_CHKSUM apunta a un archivo que no existe: {lic}")
        fallos += 1
    else:
        real = hashlib.md5(lic.read_bytes()).hexdigest()
        ok = real == esperado
        print(f"  LIC_FILES_CHKSUM: declarado {esperado}")
        print(f"                    calculado {real}  ->  {'COINCIDE' if ok else 'NO COINCIDE'}")
        fallos += 0 if ok else 1
    shutil.rmtree(workdir, ignore_errors=True)

print(f"\nRESULTADO: {'OK' if fallos == 0 else 'FALLO'} ({fallos} problemas)")
sys.exit(1 if fallos else 0)
