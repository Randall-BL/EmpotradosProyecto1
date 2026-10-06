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

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from generar_canciones import FS, guardar_mp3, render_voz, TIMBRES  # noqa: E402


def main():
    raiz = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ruta = os.path.join(raiz, "audio", "notify_cycle_end.mp3")

    bpm, total = 150, 2.4
    voces = [
        ("C5:0.5 E5 G5 C6:3", "staccato"),
        ("R:1.5 E5:3", "melodia"),
        ("R:1.5 G4:3", "bajo"),
    ]
    audio = sum(render_voz(v, TIMBRES[t], bpm, 0, total) for v, t in voces)[:int(total * FS)]
    # Desvanecer el final: es un aviso, no se repite en bucle.
    cola = int(0.3 * FS)
    audio[-cola:] *= np.linspace(1.0, 0.0, cola)
    audio *= 0.89 / max(np.max(np.abs(audio)), 1e-9)
    guardar_mp3((audio * 32767).astype(np.int16), ruta, "Fin de ciclo", "Robot aspiradora")
    print(f"  {os.path.relpath(ruta, raiz)}  {total:.1f} s  {os.path.getsize(ruta) // 1024} KB")


if __name__ == "__main__":
    main()
