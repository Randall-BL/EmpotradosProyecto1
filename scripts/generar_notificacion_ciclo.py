#!/usr/bin/env python3
#
# Proyecto I - Robot Aspiradora Autonomo
# CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
#
# Genera audio/notify_cycle_end.mp3, el aviso de fin del ciclo de limpieza:
# un arpegio ascendente de do mayor que cierra en un acorde, de ~2 s. Usa el
# mismo sintetizador y codificador que generar_canciones.py.
#
#   ./scripts/generar_notificacion_ciclo.py
#

# Rutas.
import os
# sys.path, para importar el modulo vecino.
import sys

# Desvanecimiento y normalizacion.
import numpy as np

# Agrega esta carpeta a la ruta de busqueda, para poder importar
# generar_canciones.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from generar_canciones import FS, guardar_mp3, render_voz, TIMBRES  # noqa: E402


def main():
    # Raiz del repositorio y archivo de salida.
    raiz = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ruta = os.path.join(raiz, "audio", "notify_cycle_end.mp3")

    # Tempo y duracion total, en segundos.
    bpm, total = 150, 2.4
    # Tres voces: el arpegio ascendente y dos notas largas que forman el
    # acorde.
    voces = [
        ("C5:0.5 E5 G5 C6:3", "staccato"),
        ("R:1.5 E5:3", "melodia"),
        ("R:1.5 G4:3", "bajo"),
    ]
    # Mezcla las voces y recorta a la duracion.
    audio = sum(render_voz(v, TIMBRES[t], bpm, 0, total) for v, t in voces)[:int(total * FS)]
    # Desvanecer el final: es un aviso, no se repite en bucle.
    cola = int(0.3 * FS)
    audio[-cola:] *= np.linspace(1.0, 0.0, cola)
    # Normaliza el pico al 89 % de la escala.
    audio *= 0.89 / max(np.max(np.abs(audio)), 1e-9)
    # Codifica y escribe el MP3.
    guardar_mp3((audio * 32767).astype(np.int16), ruta, "Fin de ciclo", "Robot aspiradora")
    print(f"  {os.path.relpath(ruta, raiz)}  {total:.1f} s  {os.path.getsize(ruta) // 1024} KB")


if __name__ == "__main__":
    main()
