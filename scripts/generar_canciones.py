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

import ctypes
import ctypes.util
import os
import re
import sys

import numpy as np

FS = 44100
KBPS = 64

# ── Timbres ────────────────────────────────────────────────────────────────
# Armonicos, decaimiento (1/s) y fraccion de la duracion que suena la nota.
TIMBRES = {
    "melodia":  dict(arm=(1.0, 0.45, 0.25, 0.12, 0.06), decay=2.2, legato=0.92, vol=0.60),
    "staccato": dict(arm=(1.0, 0.50, 0.30, 0.15),       decay=5.0, legato=0.55, vol=0.60),
    "bajo":     dict(arm=(1.0, 0.55, 0.25),             decay=1.6, legato=0.95, vol=0.38),
}

_NOTA = re.compile(r"^([A-G])([#b]?)(-?\d)$")
_PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(nombre):
    m = _NOTA.match(nombre)
    if not m:
        raise ValueError(f"nota invalida: {nombre}")
    letra, alt, octava = m.groups()
    return 12 * (int(octava) + 1) + _PC[letra] + {"": 0, "#": 1, "b": -1}[alt]


def parsear(texto):
    """'E5:0.5 D#5 R:1 ...' -> [(midi|None, beats)]. Sin ':' repite la duracion anterior."""
    notas, dur = [], 1.0
    for tok in texto.replace("|", " ").split():
        nombre, _, d = tok.partition(":")
        if d:
            dur = float(d)
        notas.append((None if nombre == "R" else midi(nombre), dur))
    return notas


def tono(freq, dur, timbre):
    """Una nota con ataque corto, decaimiento exponencial y cola de liberacion."""
    t = np.arange(int((dur + 0.05) * FS)) / FS
    onda = sum(a * np.sin(2 * np.pi * (k + 1) * freq * t)
               for k, a in enumerate(timbre["arm"]) if (k + 1) * freq < FS / 2)
    env = np.minimum(t / 0.008, 1.0) * np.exp(-t * timbre["decay"])
    env *= np.clip((dur + 0.05 - t) / 0.05, 0.0, 1.0)
    return onda * env * timbre["vol"]


def render_voz(texto, timbre, bpm, transp, total):
    seg_beat = 60.0 / bpm
    buf = np.zeros(int(total * FS) + FS)
    t = 0.0
    for nota, beats in parsear(texto):
        dur = beats * seg_beat
        if nota is not None:
            f = 440.0 * 2 ** ((nota + transp - 69) / 12)
            x = tono(f, dur * timbre["legato"], timbre)
            i = int(t * FS)
            buf[i:i + len(x)] += x
        t += dur
    return buf


def duracion_beats(texto):
    return sum(b for _, b in parsear(texto))


def render_cancion(secciones):
    """Cada seccion: dict(bpm, voces=[(texto, timbre)], transp=0)."""
    partes = []
    for s in secciones:
        bpm, transp = s["bpm"], s.get("transp", 0)
        beats = max(duracion_beats(v) for v, _ in s["voces"])
        total = beats * 60.0 / bpm
        mezcla = sum(render_voz(v, TIMBRES[tb], bpm, transp, total)
                     for v, tb in s["voces"])
        partes.append(mezcla[:int(total * FS)])
    # Sin silencio al principio ni al final: la pista se repite en bucle y un
    # hueco se oiria en cada vuelta. Las colas de ~50 ms que se cortan al
    # unir secciones son inaudibles.
    audio = np.concatenate(partes)
    audio *= 0.89 / max(np.max(np.abs(audio)), 1e-9)
    return (audio * 32767).astype(np.int16)


# ── Codificacion MP3 (libmp3lame por ctypes) ──────────────────────────────

def _lame():
    nombre = ctypes.util.find_library("mp3lame") or "libmp3lame.so.0"
    try:
        lib = ctypes.CDLL(nombre)
    except OSError:
        sys.exit("No se encontro libmp3lame (sudo apt install libmp3lame0)")
    lib.lame_init.restype = ctypes.c_void_p
    for fn in ("lame_set_in_samplerate", "lame_set_num_channels", "lame_set_mode",
               "lame_set_brate", "lame_set_quality", "lame_init_params",
               "lame_encode_buffer", "lame_encode_flush", "lame_close",
               "id3tag_init", "id3tag_set_title", "id3tag_set_artist",
               "id3tag_set_comment", "id3tag_add_v2"):
        getattr(lib, fn).argtypes = None
    return lib


def guardar_mp3(pcm, ruta, titulo, autor):
    lib = _lame()
    gf = ctypes.c_void_p(lib.lame_init())
    lib.lame_set_in_samplerate(gf, FS)
    lib.lame_set_num_channels(gf, 1)
    lib.lame_set_mode(gf, 3)               # MONO
    lib.lame_set_brate(gf, KBPS)
    lib.lame_set_quality(gf, 2)
    lib.id3tag_init(gf)
    lib.id3tag_add_v2(gf)
    lib.id3tag_set_title(gf, titulo.encode("latin-1"))
    lib.id3tag_set_artist(gf, autor.encode("latin-1"))
    lib.id3tag_set_comment(gf, b"Sintetizado - dominio publico")
    if lib.lame_init_params(gf) < 0:
        sys.exit("lame_init_params fallo")

    pcm = np.ascontiguousarray(pcm)
    tam = int(1.25 * len(pcm)) + 7200
    out = (ctypes.c_ubyte * tam)()
    ptr = pcm.ctypes.data_as(ctypes.POINTER(ctypes.c_short))
    n = lib.lame_encode_buffer(gf, ptr, ptr, ctypes.c_int(len(pcm)), out, ctypes.c_int(tam))
    if n < 0:
        sys.exit(f"lame_encode_buffer fallo ({n})")
    datos = bytes(out[:n])
    n = lib.lame_encode_flush(gf, out, ctypes.c_int(tam))
    datos += bytes(out[:max(n, 0)])
    lib.lame_close(gf)

    with open(ruta, "wb") as f:
        f.write(datos)


# ── Repertorio ─────────────────────────────────────────────────────────────
# Duraciones en tiempos de negra (1 = negra, 0.5 = corchea, 0.25 = semicorchea).
# Cada pista es una sola frase de ~10 s que el reproductor repite en bucle
# (ver playback_thread en lib/lib_audio.c): la frase termina donde empieza la
# siguiente vuelta, asi el empalme suena como parte de la musica.

def himno_alegria():
    a = "F#5:1 F#5 G5 A5 | A5 G5 F#5 E5 | D5 D5 E5 F#5 |"
    mel = a + " F#5:1.5 E5:0.5 E5:2 |" + a + " E5:1.5 D5:0.5 D5:2"
    bajo = "D4:2 D4 | A3 A3 | D4 D4 | A3 A3 | D4 D4 | A3 A3 | D4 A3 | D4 D4"
    return [dict(bpm=160, voces=[(mel, "melodia"), (bajo, "bajo")])]


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


def canon():
    mel = "D5:1 F#5 A5 G5 F#5 D5 F#5 E5 D5 B4 D5 A5 G5 B5 A5 G5"
    bajo = "D4:2 A3 B3 F#3 G3 D3 G3 A3"
    return [dict(bpm=96, voces=[(mel, "melodia"), (bajo, "bajo")])]


def serenata():
    mel = ("G5:1 R:0.5 D5:0.5 G5:1 R:0.5 D5:0.5 | G5:0.5 D5 G5 B5 D6:1 R:1 |"
           " C6:1 R:0.5 A5:0.5 C6:1 R:0.5 A5:0.5 | C6:0.5 A5 F#5 A5 D5:1 R:1 |"
           " D5:0.5 F#5 A5 C6 B5 A5 G5 F#5 | G5:1 D5 G4 R")
    bajo = ("G3:1 R:0.5 D3:0.5 G3:1 R:1 | G3:2 B3:1 R:1 | D4:1 R:0.5 A3:0.5 D4:1 R:1 |"
            " D4:2 D3:1 R:1 | D4:2 D3:2 | G3:1 D3 G3 R")
    return [dict(bpm=132, voces=[(mel, "staccato"), (bajo, "bajo")])]


def rey_montana():
    mel = ("B4:0.5 C#5 D5 E5 F#5 D5 F#5:1 | F5:0.5 C#5 F5:1 E5:0.5 C5 E5:1 |"
           " B4:0.5 C#5 D5 E5 F#5 D5 F#5 B5 | A5:0.5 F#5 D5 F#5 A5:2 |"
           " B4:0.5 C#5 D5 E5 F#5 D5 F#5:1 | F5:0.5 C#5 F5:1 E5:0.5 C5 E5:1 |"
           " B4:0.5 C#5 D5 E5 F#5 D5 F#5 B5 | A5:0.5 F#5 D5 F#5 B5:2")
    bajo = "B3:1 F#3 B3 F#3 | C#4 C#4 C4 C4 | B3 F#3 B3 F#3 | D4 D4 D4 F#3 |" * 2
    return [dict(bpm=168, voces=[(mel, "staccato"), (bajo, "bajo")])]


def minueto():
    # 3/4: cada compas dura 3 tiempos.
    mel = ("D5:1 G4:0.5 A4 B4 C5 | D5:1 G4 G4 | E5:1 C5:0.5 D5 E5 F#5 | G5:1 G4 G4 |"
           " C5:1 D5:0.5 C5 B4 A4 | B4:1 C5:0.5 B4 A4 G4 | A4:1 B4:0.5 A4 G4 F#4 | G4:3")
    bajo = "G3:3 | B3 | C4 | B3 | A3 | G3 | D4 | G3"
    return [dict(bpm=140, voces=[(mel, "melodia"), (bajo, "bajo")])]


def cuna():
    # La anacrusa E5 E5 de la vuelta siguiente completa el ultimo compas.
    mel = ("E5:0.5 E5 | G5:2 E5:0.5 E5 | G5:2 E5:0.5 G5 | C6:1 B5:1.5 A5:0.5 |"
           " A5:1 G5 D5:0.5 E5 | F5:1 D5 D5:0.5 E5 | F5:2 D5:0.5 F5 |"
           " B5:0.5 A5 G5:1 B5 | C6:2")
    bajo = "R:1 | C4:3 | C4 | C4 | C4:2 G3:1 | G3:3 | G3 | G3:2 G3:1 | C4:2"
    return [dict(bpm=120, voces=[(mel, "melodia"), (bajo, "bajo")])]


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


def main():
    raiz = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    destino = sys.argv[1] if len(sys.argv) > 1 else os.path.join(raiz, "audio", "canciones")
    os.makedirs(destino, exist_ok=True)
    for archivo, titulo, autor, gen in CANCIONES:
        pcm = render_cancion(gen())
        ruta = os.path.join(destino, archivo)
        guardar_mp3(pcm, ruta, titulo, autor)
        print(f"  {archivo:45s} {len(pcm) / FS:5.1f} s  {os.path.getsize(ruta) // 1024:4d} KB")


if __name__ == "__main__":
    main()
