#!/usr/bin/env python3
# Proyecto I - Robot Aspiradora Autonomo — CE-1113, TEC
#
# Compila librobot y robot-server como lo hacen sus recetas en la maquina
# qemuarm64-robot, pero con el gcc del host y SIN BitBake:
#   1. desempaqueta el SRC_URI de cada receta (con su :append:qemuall) en un
#      WORKDIR propio, respetando ;subdir=, igual que do_unpack;
#   2. configura con CMake desde ${S}, con el EXTRA_OECMAKE de la receta;
#   3. compila e instala en un sysroot de prueba; el servidor se configura
#      contra ese sysroot, como hace BitBake con DEPENDS = "librobot";
#   4. comprueba que lo instalado es lo que FILES espera;
#   5. arranca el robot-server instalado y le pide GET / y /api/status.
#
# Detecta un fuente que falte en el SRC_URI, un CMakeLists roto o un header que
# no se instala. No prueba la compilacion cruzada ni el empaquetado.
#
# Uso:  python3 pruebas/herramientas/compilar_como_receta.py [raiz] [dir-de-trabajo]
#   CMAKE=<ruta>          binario de cmake (por defecto "cmake")
#   CMAKE_EXTRA="<args>"  argumentos extra de configuracion, p. ej. para tomar
#                         alsa, mpg123 y microhttpd de una carpeta propia
#
# Necesita cmake, gcc y las cabeceras de desarrollo de alsa, mpg123 y
# microhttpd. El puerto 8080 debe estar libre.

import os
import re
import shlex
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

# Raiz del repositorio y carpeta de trabajo, por argumento o por defecto.
RAIZ = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
TRABAJO = Path(sys.argv[2] if len(sys.argv) > 2 else RAIZ / "build-receta").resolve()
# Binario de cmake y argumentos extra, por variable de entorno.
CMAKE = os.environ.get("CMAKE", "cmake")
CMAKE_EXTRA = shlex.split(os.environ.get("CMAKE_EXTRA", ""))
# Sysroot de prueba donde se instala todo.
SYSROOT = TRABAJO / "sysroot"
# Direccion del servidor que se arranca al final.
URL = "http://localhost:8080"
# Comprobaciones fallidas.
fallos = 0


# Registra una comprobacion: imprime OK o FALLO y lleva la cuenta.
def verificar(texto, condicion):
    global fallos
    print(f"  [{'OK ' if condicion else 'FALLO'}] {texto}")
    fallos += 0 if condicion else 1


def asignaciones(texto, nombre):
    """Valores de todas las asignaciones NOMBRE[:override] = "..." (multilinea)."""
    # La misma expresion regular de verificar_licencias.py.
    patron = re.compile(
        r'^(' + re.escape(nombre) + r'(?::[\w:${}-]+)?)\s*(?::=|\?=|\+=|=)\s*"((?:[^"\\]|\\.|\\\n)*)"',
        re.M,
    )
    return [(m.group(1), m.group(2).replace("\\\n", " ")) for m in patron.finditer(texto)]


def desempaquetar(rel):
    """Emula do_unpack. Devuelve (WORKDIR, S, EXTRA_OECMAKE de qemuall)."""
    # Receta, su texto, su carpeta y su nombre y version.
    receta = RAIZ / rel
    texto = receta.read_text(encoding="utf-8")
    thisdir = receta.parent
    pn, pv = receta.stem.split("_")

    # Carpetas de busqueda: FILESEXTRAPATHS y despues las de FILESPATH.
    busqueda = []
    for _, valor in asignaciones(texto, "FILESEXTRAPATHS"):
        for p in valor.split(":"):
            if p.strip():
                busqueda.append(Path(p.replace("${THISDIR}", str(thisdir))).resolve())
    busqueda += [thisdir / f"{pn}-{pv}", thisdir / pn, thisdir / "files"]

    # WORKDIR propio de la receta, vacio.
    workdir = TRABAJO / pn
    shutil.rmtree(workdir, ignore_errors=True)
    total = 0
    for clave, valor in asignaciones(texto, "SRC_URI"):
        # El SRC_URI base y el de la variante de QEMU; no los de otras maquinas.
        if clave not in ("SRC_URI", "SRC_URI:append:qemuall"):
            continue
        for uri in valor.split():
            if not uri.startswith("file://"):
                continue
            # Ruta del archivo y parametros de la entrada.
            ruta, *params = uri[len("file://"):].split(";")
            subdir = next((p.split("=", 1)[1] for p in params if p.startswith("subdir=")), "")
            # Primera carpeta de busqueda donde existe; si no esta en
            # ninguna, se aborta.
            origen = next((d / ruta for d in busqueda if (d / ruta).exists()), None)
            if origen is None:
                sys.exit(f"FALLO: {rel} pide {uri} y no existe en el repositorio")
            # Se copia respetando subdir=.
            destino = workdir / subdir / ruta
            destino.parent.mkdir(parents=True, exist_ok=True)
            if origen.is_dir():
                shutil.copytree(origen, destino, dirs_exist_ok=True)
            else:
                shutil.copy2(origen, destino)
            total += 1
    # S de la receta, con WORKDIR resuelto.
    s = Path(dict(asignaciones(texto, "S"))["S"].replace("${WORKDIR}", str(workdir)))
    # Argumentos de CMake que la receta agrega para la maquina de QEMU.
    extra = " ".join(v for k, v in asignaciones(texto, "EXTRA_OECMAKE") if k.endswith(":qemuall"))
    print(f"  {pn}: {total} entradas de SRC_URI desempaquetadas en {workdir.relative_to(TRABAJO)}/")
    return workdir, s, shlex.split(extra.replace("${S}", str(s)))


def compilar(nombre, s, args):
    """do_configure + do_compile + do_install con CMake. Devuelve los avisos."""
    # Carpeta de compilacion, junto a S, y archivo de log.
    build = s.parent / "build"
    log = TRABAJO / f"{nombre}.log"
    pasos = [
        # prefix y libdir como los fija cmake.bbclass: /usr y /usr/lib.
        [CMAKE, "-S", str(s), "-B", str(build), "-DCMAKE_INSTALL_PREFIX=/usr",
         "-DCMAKE_INSTALL_LIBDIR=lib", "-DCMAKE_C_FLAGS=-Wall -Wextra", *args, *CMAKE_EXTRA],
        [CMAKE, "--build", str(build), "--parallel"],
        [CMAKE, "--install", str(build)],
    ]
    # Ejecuta los tres pasos con DESTDIR apuntando al sysroot; si uno falla,
    # muestra el final del log y termina.
    with open(log, "w") as salida:
        for paso in pasos:
            r = subprocess.run(paso, stdout=salida, stderr=subprocess.STDOUT,
                               env={**os.environ, "DESTDIR": str(SYSROOT)})
            if r.returncode != 0:
                print(log.read_text()[-3000:])
                sys.exit(f"FALLO: {nombre}: `{' '.join(paso[:3])} ...` termino con codigo {r.returncode}")
    # Lineas del log que son avisos del compilador.
    return [l for l in log.read_text().splitlines() if "warning:" in l]


# Codigo HTTP de una peticion GET; 0 si no hubo conexion.
def http(ruta):
    try:
        return urllib.request.urlopen(URL + ruta, timeout=2).status
    except urllib.error.HTTPError as e:
        return e.code
    except OSError:
        return 0


def arrancar(workdir_servidor):
    """Arranca el servidor instalado desde un directorio armado como /opt/robot."""
    # Carpeta que imita /opt/robot: la interfaz web y los sonidos de evento.
    opt = TRABAJO / "opt-robot"
    shutil.rmtree(opt, ignore_errors=True)
    shutil.copytree(workdir_servidor / "www", opt / "www")
    (opt / "audio").mkdir()
    for mp3 in (workdir_servidor / "audio").glob("notify_*.mp3"):
        shutil.copy2(mp3, opt / "audio")

    # Si algo ya responde en el puerto, la prueba no seria valida.
    if http("/") != 0:
        sys.exit("FALLO: el puerto 8080 ya esta ocupado por otro proceso")
    # El servidor instalado necesita encontrar librobot en el sysroot.
    entorno = {**os.environ,
               "LD_LIBRARY_PATH": f"{SYSROOT}/usr/lib:" + os.environ.get("LD_LIBRARY_PATH", "")}
    with open(TRABAJO / "robot-server.log", "w") as log:
        # Lanza el servidor con esa carpeta como directorio de trabajo.
        proc = subprocess.Popen([str(SYSROOT / "usr/bin/robot-server")], cwd=opt, env=entorno,
                                stdout=log, stderr=subprocess.STDOUT)
        try:
            inicio = time.time()
            raiz = 0
            # Sondea la raiz hasta 15 s, o hasta que el proceso termine
            # solo.
            while raiz == 0 and proc.poll() is None and time.time() - inicio < 15:
                raiz = http("/")
                if raiz == 0:
                    time.sleep(0.5)
            print(f"  el servidor respondio a los {time.time() - inicio:.1f} s")
            verificar(f"GET / devuelve 200 (obtenido {raiz})", raiz == 200)
            # Sin sesion, la API tiene que responder 401.
            estado = http("/api/status")
            verificar(f"GET /api/status sin sesion devuelve 401 (obtenido {estado})", estado == 401)
        finally:
            # Siempre se detiene el servidor; si no termina en 20 s, se lo
            # mata.
            proc.terminate()
            try:
                proc.wait(timeout=20)
            except subprocess.TimeoutExpired:
                proc.kill()


# Empieza con el sysroot vacio.
shutil.rmtree(SYSROOT, ignore_errors=True)
TRABAJO.mkdir(parents=True, exist_ok=True)

print("== do_unpack ==")
# Desempaqueta las dos recetas.
_, s_lib, args_lib = desempaquetar("meta-robot/recipes-multimedia/librobot/librobot_1.0.bb")
wd_srv, s_srv, _ = desempaquetar("meta-robot/recipes-robot/robot-server/robot-server_1.0.bb")

print("\n== librobot: configurar, compilar e instalar ==")
print(f"  EXTRA_OECMAKE: {' '.join(a.replace(str(TRABAJO), '${TRABAJO}') for a in args_lib)}")
# Compila e instala la biblioteca, y comprueba lo instalado contra FILES.
avisos = compilar("librobot", s_lib, args_lib)
lib = SYSROOT / "usr/lib"
inc = SYSROOT / "usr/include/robot"
verificar("FILES:${PN}: usr/lib/librobot.so.1.0.0 y su enlace librobot.so.1",
          (lib / "librobot.so.1.0.0").is_file() and (lib / "librobot.so.1").is_symlink())
verificar("FILES:${PN}-dev: usr/lib/librobot.so", (lib / "librobot.so").is_symlink())
headers = sorted(h.name for h in inc.glob("*.h")) if inc.is_dir() else []
verificar(f"FILES:${{PN}}-dev: {len(headers)} headers en usr/include/robot/", len(headers) > 0)
print(f"         {' '.join(headers)}")
print(f"  avisos del compilador: {len(avisos)}")
for a in avisos:
    print(f"    {a.replace(str(TRABAJO) + '/', '')}")

print("\n== robot-server: configurar contra el sysroot, compilar e instalar ==")
# El servidor se configura contra el sysroot donde quedo la biblioteca.
avisos = compilar("robot-server", s_srv, [f"-DCMAKE_PREFIX_PATH={SYSROOT}/usr"])
verificar("usr/bin/robot-server instalado", (SYSROOT / "usr/bin/robot-server").is_file())
print(f"  avisos del compilador: {len(avisos)}")
for a in avisos:
    print(f"    {a.replace(str(TRABAJO) + '/', '')}")

print("\n== arranque del servidor instalado ==")
# Arranca lo instalado y le hace dos peticiones.
arrancar(wd_srv)

# Resumen y codigo de salida.
print(f"\nRESULTADO: {'OK' if fallos == 0 else 'FALLO'} ({fallos} problemas)")
sys.exit(1 if fallos else 0)
