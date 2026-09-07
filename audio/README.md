# `audio/` — Recursos de audio

## Sonidos de evento (versionados)

Los cuatro eventos que exige el enunciado:

| Archivo | Evento |
|---|---|
| `notify_startup.mp3`    | Inicio del sistema |
| `notify_autonomous.mp3` | Inicio del modo autónomo |
| `notify_obstacle.mp3`   | Obstáculo detectado |
| `notify_manual.mp3`     | Cambio a modo manual |

## Música (`music/`, **no versionada**)

`audio/music/` está excluido del repositorio por peso y por derechos de autor.
Coloque ahí los MP3 que quiera incluir en la imagen; la receta `robot-server_1.0.bb`
los copia a `/opt/robot/audio/` del target durante el build.

Si el directorio está vacío la imagen se construye igual, solo que sin playlist.
