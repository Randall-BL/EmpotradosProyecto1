#!/usr/bin/env python3
#
# Proyecto I - Robot Aspiradora Autonomo
# CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
#
# Genera la playlist versionada de audio/canciones/: melodias clasicas de
# dominio publico sintetizadas aqui mismo, asi que no arrastran derechos de
# autor de ninguna grabacion y pueden vivir en el repositorio.
#
#   ./scripts/generar_canciones.py              # escribe en audio/canciones/
#   ./scripts/generar_canciones.py /otro/dir
#
# Necesita numpy y libmp3lame (paquete libmp3lame0 en Debian/Ubuntu); el
# codificador se llama por ctypes, no hace falta el ejecutable lame ni ffmpeg.
#
# Salida: MP3 mono, 44.1 kHz, 64 kbps. El robot tiene un solo parlante detras
# de una salida PWM (ver lib/lib_audio.h), mas calidad no se oiria. Las notas
# graves se escriben una octava arriba de la partitura porque el parlante del
# PAM8403 casi no reproduce por debajo de ~150 Hz.
#

# ctypes: llamar a libmp3lame sin un modulo de extension.
import ctypes
import ctypes.util
# Rutas y carpetas.
import os
# Expresion regular para leer los nombres de las notas.
import re
# Argumentos y salida con error.
import sys

# Sintesis del audio con arreglos.
import numpy as np

# Frecuencia de muestreo, en Hz.
FS = 44100
# Tasa de bits del MP3, en kbps.
KBPS = 64

# ── Timbres ────────────────────────────────────────────────────────────────
# Armonicos, decaimiento (1/s) y fraccion de la duracion que suena la nota.
TIMBRES = {
    "melodia":  dict(arm=(1.0, 0.45, 0.25, 0.12, 0.06), decay=2.2, legato=0.92, vol=0.60),
    "staccato": dict(arm=(1.0, 0.50, 0.30, 0.15),       decay=5.0, legato=0.55, vol=0.60),
    "bajo":     dict(arm=(1.0, 0.55, 0.25),             decay=1.6, legato=0.95, vol=0.38),
}

# Nombre de una nota: letra, alteracion opcional y octava. Ejemplos: C4,
# F#5, Bb3.
_NOTA = re.compile(r"^([A-G])([#b]?)(-?\d)$")
# Semitonos de cada nota natural sobre el do de su octava.
_PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


# Convierte un nombre de nota en su numero MIDI (La4 = 69).
def midi(nombre):
    m = _NOTA.match(nombre)
    if not m:
        raise ValueError(f"nota invalida: {nombre}")
    # Letra, alteracion y octava.
    letra, alt, octava = m.groups()
    # MIDI: 12 por octava (la -1 empieza en 0), mas la nota y su alteracion.
    return 12 * (int(octava) + 1) + _PC[letra] + {"": 0, "#": 1, "b": -1}[alt]


def parsear(texto):
    """'E5:0.5 D#5 R:1 ...' -> [(midi|None, beats)]. Sin ':' repite la duracion anterior."""
    # Lista de notas, y duracion vigente, que parte en una negra.
    notas, dur = [], 1.0
    # Las barras de compas son solo visuales: se tratan como espacios.
    for tok in texto.replace("|", " ").split():
        # Separa el nombre de la duracion, si viene.
        nombre, _, d = tok.partition(":")
        if d:
            dur = float(d)
        # R es un silencio: se guarda como None.
        notas.append((None if nombre == "R" else midi(nombre), dur))
    return notas


def tono(freq, dur, timbre):
    """Una nota con ataque corto, decaimiento exponencial y cola de liberacion."""
    # Vector de tiempos de la nota, con 50 ms extra para la cola.
    t = np.arange(int((dur + 0.05) * FS)) / FS
    # Suma de armonicos; se descartan los que pasan de la mitad de la
    # frecuencia de muestreo, para evitar aliasing.
    onda = sum(a * np.sin(2 * np.pi * (k + 1) * freq * t)
               for k, a in enumerate(timbre["arm"]) if (k + 1) * freq < FS / 2)
    # Envolvente: ataque de 8 ms y decaimiento exponencial.
    env = np.minimum(t / 0.008, 1.0) * np.exp(-t * timbre["decay"])
    # Desvanecimiento lineal en los ultimos 50 ms.
    env *= np.clip((dur + 0.05 - t) / 0.05, 0.0, 1.0)
    return onda * env * timbre["vol"]


# Sintetiza una voz completa. texto: notas; bpm: tempo; transp: semitonos
# de transposicion; total: duracion en segundos.
def render_voz(texto, timbre, bpm, transp, total):
    # Segundos que dura un tiempo.
    seg_beat = 60.0 / bpm
    # Buffer de la voz, con un segundo de margen.
    buf = np.zeros(int(total * FS) + FS)
    # Instante en que empieza la nota siguiente.
    t = 0.0
    for nota, beats in parsear(texto):
        dur = beats * seg_beat
        if nota is not None:
            # Frecuencia de la nota: La4 (MIDI 69) = 440 Hz, y un semitono
            # es la raiz
            # doceava de 2.
            f = 440.0 * 2 ** ((nota + transp - 69) / 12)
            # La nota suena la fraccion legato de su duracion.
            x = tono(f, dur * timbre["legato"], timbre)
            # Se suma al buffer en su posicion.
            i = int(t * FS)
            buf[i:i + len(x)] += x
        # Avanza el tiempo, tambien en los silencios.
        t += dur
    return buf


# Duracion total de una voz, en tiempos.
def duracion_beats(texto):
    return sum(b for _, b in parsear(texto))


def render_cancion(secciones):
    """Cada seccion: dict(bpm, voces=[(texto, timbre)], transp=0)."""
    # Una parte de audio por cada seccion de la partitura.
    partes = []
    for s in secciones:
        # Tempo y transposicion de la seccion.
        bpm, transp = s["bpm"], s.get("transp", 0)
        # La seccion dura lo que su voz mas larga.
        beats = max(duracion_beats(v) for v, _ in s["voces"])
        total = beats * 60.0 / bpm
        # Mezcla de todas las voces.
        mezcla = sum(render_voz(v, TIMBRES[tb], bpm, transp, total)
                     for v, tb in s["voces"])
        # Se recorta a la duracion exacta.
        partes.append(mezcla[:int(total * FS)])
    # Sin silencio al principio ni al final: la pista se repite en bucle y un
    # hueco se oiria en cada vuelta. Las colas de ~50 ms que se cortan al
    # unir secciones son inaudibles.
    # Une las secciones y normaliza el pico al 89 % de la escala.
    audio = np.concatenate(partes)
    audio *= 0.89 / max(np.max(np.abs(audio)), 1e-9)
    # Pasa a enteros de 16 bits.
    return (audio * 32767).astype(np.int16)


# ── Codificacion MP3 (libmp3lame por ctypes) ──────────────────────────────

# Carga libmp3lame y devuelve la biblioteca lista para llamarse.
def _lame():
    # Nombre de la biblioteca en este sistema, o el habitual de Linux.
    nombre = ctypes.util.find_library("mp3lame") or "libmp3lame.so.0"
    try:
        lib = ctypes.CDLL(nombre)
    except OSError:
        sys.exit("No se encontro libmp3lame (sudo apt install libmp3lame0)")
    # lame_init devuelve un puntero.
    lib.lame_init.restype = ctypes.c_void_p
    # En estas funciones no se declaran tipos: ctypes convierte los
    # argumentos
    # al llamarlas.
    for fn in ("lame_set_in_samplerate", "lame_set_num_channels", "lame_set_mode",
               "lame_set_brate", "lame_set_quality", "lame_init_params",
               "lame_encode_buffer", "lame_encode_flush", "lame_close",
               "id3tag_init", "id3tag_set_title", "id3tag_set_artist",
               "id3tag_set_comment", "id3tag_add_v2"):
        getattr(lib, fn).argtypes = None
    return lib


# Codifica audio PCM mono de 16 bits a MP3 y lo escribe en ruta, con sus
# etiquetas ID3.
def guardar_mp3(pcm, ruta, titulo, autor):
    lib = _lame()
    # Configuracion del codificador: frecuencia, un canal, modo mono, tasa
    # de
    # bits y calidad.
    gf = ctypes.c_void_p(lib.lame_init())
    lib.lame_set_in_samplerate(gf, FS)
    lib.lame_set_num_channels(gf, 1)
    lib.lame_set_mode(gf, 3)               # MONO
    lib.lame_set_brate(gf, KBPS)
    lib.lame_set_quality(gf, 2)
    # Etiquetas ID3: titulo, autor y comentario.
    lib.id3tag_init(gf)
    lib.id3tag_add_v2(gf)
    lib.id3tag_set_title(gf, titulo.encode("latin-1"))
    lib.id3tag_set_artist(gf, autor.encode("latin-1"))
    lib.id3tag_set_comment(gf, b"Sintetizado - dominio publico")
    # Aplica la configuracion.
    if lib.lame_init_params(gf) < 0:
        sys.exit("lame_init_params fallo")

    # El arreglo tiene que estar contiguo en memoria para pasarlo como
    # puntero.
    pcm = np.ascontiguousarray(pcm)
    # Tamano del buffer de salida que recomienda LAME: 1.25 x muestras +
    # 7200.
    tam = int(1.25 * len(pcm)) + 7200
    out = (ctypes.c_ubyte * tam)()
    ptr = pcm.ctypes.data_as(ctypes.POINTER(ctypes.c_short))
    # Codifica todo el audio; al ser mono, el mismo buffer va en los dos
    # canales.
    n = lib.lame_encode_buffer(gf, ptr, ptr, ctypes.c_int(len(pcm)), out, ctypes.c_int(tam))
    if n < 0:
        sys.exit(f"lame_encode_buffer fallo ({n})")
    datos = bytes(out[:n])
    # Vacia lo que quedo dentro del codificador.
    n = lib.lame_encode_flush(gf, out, ctypes.c_int(tam))
    datos += bytes(out[:max(n, 0)])
    lib.lame_close(gf)

    # Escribe el MP3.
    with open(ruta, "wb") as f:
        f.write(datos)


# ── Repertorio ─────────────────────────────────────────────────────────────
# Duraciones en tiempos de negra (1 = negra, 0.5 = corchea, 0.25 = semicorchea).
# Cada pista es una sola frase de ~10 s que el reproductor repite en bucle
# (ver playback_thread en lib/lib_audio.c): la frase termina donde empieza la
# siguiente vuelta, asi el empalme suena como parte de la musica.

# Cada funcion de cancion devuelve su lista de secciones: el tempo y las
# voces, que son la melodia y el bajo.
def himno_alegria():
    a = "F#5:1 F#5 G5 A5 | A5 G5 F#5 E5 | D5 D5 E5 F#5 |"
    mel = a + " F#5:1.5 E5:0.5 E5:2 |" + a + " E5:1.5 D5:0.5 D5:2"
    bajo = "D4:2 D4 | A3 A3 | D4 D4 | A3 A3 | D4 D4 | A3 A3 | D4 A3 | D4 D4"
    return [dict(bpm=160, voces=[(mel, "melodia"), (bajo, "bajo")])]


# Para Elisa, de Beethoven.
def para_elisa():
    # 3/8: cada compas dura 1.5 tiempos. La anacrusa E5 D#5 abre cada vuelta.
    cab = "E5:0.25 D#5 E5 B4 D5 C5 |"
    mel = ("E5:0.25 D#5 |" + cab +
           " A4:0.5 R:0.25 C4 E4 A4 | B4:0.5 R:0.25 E4 G#4 B4 |"
           " C5:0.5 R:0.25 E4 E5 D#5 |" + cab +
           " A4:0.5 R:0.25 C4 E4 A4 | B4:0.5 R:0.25 E4 C5 B4 | A4:0.5 R:1")
    # Bajo una octava arriba de la partitura, ver cabecera.
    bajo = ("R:0.5 | R:1.5 | A3:0.25 E4 A4 R:0.75 | E3:0.25 E4 G#4 R:0.75 |"
            " A3:0.25 E4 A4 R:0.75 | R:1.5 |"
            " A3:0.25 E4 A4 R:0.75 | E3:0.25 E4 G#4 R:0.75 | A3:0.25 E4 A4 R:0.75")
    return [dict(bpm=80, voces=[(mel, "melodia"), (bajo, "bajo")])]


# Canon en Re, de Pachelbel.
def canon():
    mel = "D5:1 F#5 A5 G5 F#5 D5 F#5 E5 D5 B4 D5 A5 G5 B5 A5 G5"
    bajo = "D4:2 A3 B3 F#3 G3 D3 G3 A3"
    return [dict(bpm=96, voces=[(mel, "melodia"), (bajo, "bajo")])]


# Pequena serenata nocturna, de Mozart.
def serenata():
    mel = ("G5:1 R:0.5 D5:0.5 G5:1 R:0.5 D5:0.5 | G5:0.5 D5 G5 B5 D6:1 R:1 |"
           " C6:1 R:0.5 A5:0.5 C6:1 R:0.5 A5:0.5 | C6:0.5 A5 F#5 A5 D5:1 R:1 |"
           " D5:0.5 F#5 A5 C6 B5 A5 G5 F#5 | G5:1 D5 G4 R")
    bajo = ("G3:1 R:0.5 D3:0.5 G3:1 R:1 | G3:2 B3:1 R:1 | D4:1 R:0.5 A3:0.5 D4:1 R:1 |"
            " D4:2 D3:1 R:1 | D4:2 D3:2 | G3:1 D3 G3 R")
    return [dict(bpm=132, voces=[(mel, "staccato"), (bajo, "bajo")])]


# En la gruta del rey de la montana, de Grieg.
def rey_montana():
    mel = ("B4:0.5 C#5 D5 E5 F#5 D5 F#5:1 | F5:0.5 C#5 F5:1 E5:0.5 C5 E5:1 |"
           " B4:0.5 C#5 D5 E5 F#5 D5 F#5 B5 | A5:0.5 F#5 D5 F#5 A5:2 |"
           " B4:0.5 C#5 D5 E5 F#5 D5 F#5:1 | F5:0.5 C#5 F5:1 E5:0.5 C5 E5:1 |"
           " B4:0.5 C#5 D5 E5 F#5 D5 F#5 B5 | A5:0.5 F#5 D5 F#5 B5:2")
    bajo = "B3:1 F#3 B3 F#3 | C#4 C#4 C4 C4 | B3 F#3 B3 F#3 | D4 D4 D4 F#3 |" * 2
    return [dict(bpm=168, voces=[(mel, "staccato"), (bajo, "bajo")])]


# Minueto en Sol, de Petzold.
def minueto():
    # 3/4: cada compas dura 3 tiempos.
    mel = ("D5:1 G4:0.5 A4 B4 C5 | D5:1 G4 G4 | E5:1 C5:0.5 D5 E5 F#5 | G5:1 G4 G4 |"
           " C5:1 D5:0.5 C5 B4 A4 | B4:1 C5:0.5 B4 A4 G4 | A4:1 B4:0.5 A4 G4 F#4 | G4:3")
    bajo = "G3:3 | B3 | C4 | B3 | A3 | G3 | D4 | G3"
    return [dict(bpm=140, voces=[(mel, "melodia"), (bajo, "bajo")])]


# Cancion de cuna, de Brahms.
def cuna():
    # La anacrusa E5 E5 de la vuelta siguiente completa el ultimo compas.
    mel = ("E5:0.5 E5 | G5:2 E5:0.5 E5 | G5:2 E5:0.5 G5 | C6:1 B5:1.5 A5:0.5 |"
           " A5:1 G5 D5:0.5 E5 | F5:1 D5 D5:0.5 E5 | F5:2 D5:0.5 F5 |"
           " B5:0.5 A5 G5:1 B5 | C6:2")
    bajo = "R:1 | C4:3 | C4 | C4 | C4:2 G3:1 | G3:3 | G3 | G3:2 G3:1 | C4:2"
    return [dict(bpm=120, voces=[(mel, "melodia"), (bajo, "bajo")])]


# Estrellita, tradicional.
def estrellita():
    mel = "C5:1 C5 G5 G5 | A5 A5 G5:2 | F5:1 F5 E5 E5 | D5 D5 C5:2"
    bajo = "C4:4 | F3:2 C4 | F3 C4 | G3 C4"
    return [dict(bpm=100, voces=[(mel, "melodia"), (bajo, "bajo")])]

# (archivo, titulo, autor, generador). El prefijo numerico fija el orden de la
# playlist: lib_audio_scan ordena las pistas por nombre de archivo, y el panel
# web muestra el nombre sin extension y con '_' como espacio.
CANCIONES = [
    ("01_Himno_a_la_alegria.mp3",            "Himno a la alegria",            "L. v. Beethoven", himno_alegria),
    ("02_Para_Elisa.mp3",                    "Para Elisa",                    "L. v. Beethoven", para_elisa),
    ("03_Canon_en_Re.mp3",                   "Canon en Re",                   "J. Pachelbel",    canon),
    ("04_Pequena_serenata_nocturna.mp3",     "Pequena serenata nocturna",     "W. A. Mozart",    serenata),
    ("05_En_la_gruta_del_rey_de_la_montana.mp3", "En la gruta del rey de la montana", "E. Grieg", rey_montana),
    ("06_Minueto_en_Sol.mp3",                "Minueto en Sol",                "C. Petzold",      minueto),
    ("07_Cancion_de_cuna.mp3",               "Cancion de cuna",               "J. Brahms",       cuna),
    ("08_Estrellita.mp3",                    "Estrellita",                    "Tradicional",     estrellita),
]


# Genera todas las pistas en la carpeta de destino.
def main():
    # Raiz del repositorio: la carpeta que contiene a scripts/.
    raiz = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    # Destino: el argumento, o audio/canciones/.
    destino = sys.argv[1] if len(sys.argv) > 1 else os.path.join(raiz, "audio", "canciones")
    os.makedirs(destino, exist_ok=True)
    # Sintetiza, codifica y muestra la duracion y el tamano de cada pista.
    for archivo, titulo, autor, gen in CANCIONES:
        pcm = render_cancion(gen())
        ruta = os.path.join(destino, archivo)
        guardar_mp3(pcm, ruta, titulo, autor)
        print(f"  {archivo:45s} {len(pcm) / FS:5.1f} s  {os.path.getsize(ruta) // 1024:4d} KB")


if __name__ == "__main__":
    main()
