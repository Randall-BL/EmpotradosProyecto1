# `audio/` — Recursos de audio

## Sonidos de evento (versionados)

Los cuatro eventos que exige el enunciado:

| Archivo | Evento |
|---|---|
| `notify_startup.mp3`    | Inicio del sistema |
| `notify_autonomous.mp3` | Inicio del modo autónomo |
| `notify_obstacle.mp3`   | Obstáculo detectado |
| `notify_manual.mp3`     | Cambio a modo manual |

## Playlist (`canciones/`, versionada)

Ocho fragmentos de unos 10 s (≈690 KB en total) de obras de dominio público,
sintetizados con `scripts/generar_canciones.py`: no vienen de ninguna grabación,
así que no tienen derechos de autor y pueden vivir en el repositorio. Hay una
sola excepción, descrita después de la tabla.

| Archivo | Obra |
|---|---|
| `01_Himno_a_la_alegria.mp3`               | Beethoven, Sinfonía n.º 9 |
| `02_Para_Elisa.mp3`                       | Beethoven, WoO 59 |
| `03_Canon_en_Re.mp3`                      | Pachelbel |
| `04_Pequena_serenata_nocturna.mp3`        | Mozart, K. 525 |
| `05_En_la_gruta_del_rey_de_la_montana.mp3`| Grieg, Peer Gynt |
| `06_Minueto_en_Sol.mp3`                   | Petzold (atribuido a Bach) |
| `07_Cancion_de_cuna.mp3`                  | Brahms, Op. 49 n.º 4 |
| `08_Estrellita.mp3`                       | Tradicional |

**Única excepción:** `Ren - Bitter Sweet Symphony (Live).mp3` (≈5.6 MB) es una
grabación real, no sintetizada ni de dominio público. Se versiona solo porque es
la canción favorita del profesor; cualquier otra grabación va en `music/`, que no
se versiona. Al no llevar prefijo numérico queda al final de la lista.

Son cortos para no gastar espacio en la imagen: el reproductor repite la pista
elegida en bucle hasta que se detiene o se elige otra. El prefijo numérico fija
el orden de la lista (`lib_audio_scan` ordena por nombre) y el panel web lo
oculta, junto con los `_`.

Para cambiar o agregar piezas, edite el repertorio del script y regenere:

```bash
./scripts/generar_canciones.py        # requiere numpy y libmp3lame0
```

## Música (`music/`, **no versionada**)

`audio/music/` está excluido del repositorio por peso y por derechos de autor.
Coloque ahí los MP3 que quiera incluir en la imagen; la receta `robot-server_1.0.bb`
los copia, junto con los de `canciones/`, a `/opt/robot/audio/canciones/` del target
durante el build.

## Partición propia en la SD

En la Raspberry `/opt/robot/audio/canciones/` es el punto de montaje de una tercera
partición (`canciones`, ext4 de 180 MiB, ver `meta-robot/wic/robot-sdimage.wks`): la
música no pesa en la partición raíz, y ninguna de las tres pasa de 200 MB. Si
`canciones/` + `music/` no caben en 180 MiB el build falla en `do_image_wic` con
*"Actual rootfs size … is larger than allowed size"*.

La partición se monta de lectura y escritura, así que también se pueden agregar
canciones sin regenerar la imagen:

```bash
scp cancion.mp3 root@<ip>:/opt/robot/audio/canciones/
```

`lib_audio_scan()` las toma en el próximo escaneo (al reiniciar el servidor).

Si el directorio está vacío la imagen se construye igual, solo con las pistas de `canciones/`.
