# Requerimientos opcionales

El enunciado propone tres requerimientos opcionales. Los tres están implementados en la
biblioteca, el servidor y el panel web, y probados sobre el simulador. La detección de
desnivel necesita además dos sensores infrarrojos que hay que montar en el chasis.

| Requerimiento | Dónde vive | Estado |
|---|---|---|
| Detección de desnivel / caída | `lib/lib_caida.{c,h}`, `server/src/main.c` | Software probado en el simulador; falta montar los sensores |
| Notificación de fin de ciclo | `server/src/main.c`, `server/src/api.c`, `audio/notify_cycle_end.mp3` | Probado en el simulador |
| Playlist persistente | `lib/lib_audio.c`, `server/src/api.c`, `server/www/dashboard.html` | Probada en el simulador |

---

## 1. Detección de desnivel

### Hardware

Dos módulos infrarrojos de reflexión (TCRT5000 o FC-51, con comparador LM393) en las
esquinas delanteras del chasis, mirando al piso a 1-2 cm de altura.

| Módulo | GPIO (BCM) | Pin físico | Alimentación |
|---|---|---|---|
| Esquina delantera izquierda | 4 | 7 | 3.3 V (pin 1 o 17) y GND lógica |
| Esquina delantera derecha | 8 | 24 | 3.3 V y GND lógica |

- **A 3.3 V, no a 5 V.** El módulo trae un pull-up a su `VCC`: alimentado a 5 V, su
  salida metería 5 V al GPIO. A 3.3 V la salida entra directo, sin divisor.
- **Con piso debajo** el haz se refleja y la salida queda en bajo; **sobre el borde** no
  vuelve nada y sube (`CAIDA_NIVEL_SIN_PISO` en `lib_caida.h`).
- **Pull-down interno en las dos entradas.** Un módulo desconectado se lee como "hay
  piso": el robot navega igual que sin los sensores, en vez de quedarse frenado.
- **Ajuste.** El potenciómetro del módulo fija el umbral: con el robot sobre el piso de la
  demostración, girarlo hasta que el LED del módulo encienda, y comprobar que se apaga al
  asomar la esquina al borde de una mesa.
- Son entradas: no suman al presupuesto de corriente de los GPIO.

### Comportamiento

`actualizar_estado()` lee los dos sensores en cada pasada, también en medio de las
maniobras. A la velocidad del robot son menos de 3 cm entre dos lecturas, y los
sensores van unos 12 cm por delante del centro. Cada sensor se lee dos veces, separadas
200 µs, y solo cuenta si las dos coinciden: así se filtra el ruido de los motores.

| Modo | Al aparecer el desnivel |
|---|---|
| Autónomo | Frena, suena el aviso de obstáculo, retrocede 0.8 s y gira 120° hacia el lado contrario al sensor que perdió el piso. Con los dos sensores, media vuelta |
| Manual | Frena y suena el aviso. `POST /api/move` con `forward` contesta `409 Desnivel al frente` hasta que se retroceda o se gire |
| Durante un giro | Si una esquina se asoma al borde al girar en el lugar, el giro se corta y el lazo principal se encarga |

El LED rojo de alerta se enciende igual ante un obstáculo o un desnivel. El panel
muestra el estado de cada sensor en *Indicadores de estado*.

### Simulador

El mundo simulado (`sim/mundo.c`) tiene una grada en la esquina sureste. No tiene
altura, así que el HC-SR04 no la ve: solo la detectan los sensores IR simulados
(`sim/pigpio_sim.c`, en GPIO 4 y 8). Si el centro del robot pasa el borde, el simulador
lo cuenta como caída (`[sim] CAIDA`).

---

## 2. Notificación de fin de ciclo

El usuario fija la meta en la pestaña Control: **minutos en modo autónomo** o **metros
cuadrados recorridos**. El área sale del mapa: cada celda de 30 × 30 cm visitada por
primera vez en el ciclo suma 0.09 m².

- El hilo de navegación suma el tiempo con el reloj monotónico, así que también cuentan
  las maniobras de evasión, y el área con las celdas nuevas del mapa.
- Al cumplir la meta: los motores se detienen, suena `notify_cycle_end.mp3` (un arpegio
  de 2.4 s, generado con `scripts/generar_notificacion_ciclo.py`) y el panel muestra
  "Ciclo completado" junto con el mensaje en la barra de estado.
- El robot espera quieto, sin salir del modo autónomo. Por eso el watchdog, que vuelve a
  autónomo cuando no hay sesiones, no lo pone a limpiar otra vez.
- Un ciclo nuevo empieza al pulsar **Nuevo ciclo**, al cambiar la meta o al pedir el modo
  autónomo desde el panel.
- Por defecto el ciclo no tiene límite: el robot se comporta como antes hasta que alguien
  configura una meta. La meta vive en memoria y vuelve a "sin límite" al reiniciar.

---

## 3. Playlist persistente

- Se guarda en `/opt/robot/audio/canciones/playlist.txt`, un nombre de archivo por línea.
  Está en la partición de la música, que es ext4 de lectura y escritura: sobrevive a los
  reinicios y no toca la partición raíz.
- Se guarda por nombre y no por id: los ids salen del orden del escaneo y cambian si se
  agrega una canción.
- Se escribe en `playlist.txt.tmp`, con `fsync`, y se renombra: un corte de energía deja
  la versión vieja o la nueva, nunca una a medias.
- Una canción borrada de la partición se ignora al cargar. Sin archivo, la playlist son
  todas las canciones.
- **En el panel:** `+` en la lista de canciones la agrega; ▲ ▼ la reordenan; ✕ la quita.
  Cada cambio se guarda en el acto. Tocar una pista de la playlist la reproduce desde ahí.
- **Reproducción:** "Reproducir playlist" recorre la lista y vuelve a empezar al final.
  Una canción elegida suelta sigue repitiéndose en bucle, como antes.

---

## 4. Evidencia en el simulador

Servidor compilado para ARM con el SDK y ejecutado con `qemu-aarch64` sobre el
simulador, el 5 de octubre de 2026.

**Desnivel.** Tres minutos en modo autónomo:

```
t=50s  detecciones=1 caidas=0
...
t=180s detecciones=1 caidas=0

[caida] sin piso bajo las dos esquinas
[auto] desnivel (ambos): retrocede y gira +180 grados
```

Una segunda corrida de cuatro minutos dio 28 evasiones de obstáculos, una detección de
desnivel por el sensor izquierdo y, otra vez, ninguna caída:

```
[caida] sin piso bajo la esquina izquierda
[auto] desnivel (izq): retrocede y gira +120 grados
```

**Ciclo de limpieza.** Con una meta de 0.2 min (12 s):

```
{'tipo': 'tiempo', 'meta': 12.0, 'segundos': 12.5, 'area': 0.63, 'progreso': 1.0, 'completo': True, 'completados': 1}
vel tras 2 s: 0.0
[ciclo] ciclo 1 completado: 13 s en autonomo, 0.63 m2 recorridos
[audio] Notificación: notify_cycle_end.mp3
```

Al pedir el modo autónomo otra vez, el ciclo volvió a cero con `completados: 1`.

**Playlist.** Guardada, recargada tras reiniciar el servidor y reproducida en orden:

```
POST /api/audio/playlist {"ids":[3,1,9]}  ->  {"ok":true}
$ cat audio/canciones/playlist.txt
03_Canon_en_Re.mp3
01_Himno_a_la_alegria.mp3
Ren - Bitter Sweet Symphony (Live).mp3
POST /api/audio/playlist {"ids":[3,99]}   ->  {"error":"Pista inexistente o no se pudo guardar"}

(reinicio del servidor)
GET /api/audio/playlist  ->  {"ids":[3,1,9],"pos":-1}
[audio] Escaneo: 9 pista(s) en './audio', playlist de 3

(play_playlist, con ALSA en el dispositivo null)
[audio] Reproduciendo id=3 ...
[audio] Reproduciendo id=1 ...
[audio] Reproduciendo id=9 ...
[audio] Reproduciendo id=3 ...
```

La prueba de la biblioteca (`sim/prueba_librobot.c`) agrega dos comprobaciones de los
sensores de desnivel y pasa las 49.

---

## 5. Pendiente en el robot

- [ ] Montar y cablear los dos módulos IR (sección 1) y ajustar su umbral.
- [ ] Asomar cada esquina al borde de una mesa, en manual y en autónomo, y ver que el
      robot frena y que el panel marca el lado correcto.
- [ ] Ciclo por tiempo de 1 min: el robot se detiene, suena el aviso y aparece el mensaje.
- [ ] Armar una playlist, reiniciar la Raspberry y comprobar que sigue ahí. Escuchar que
      pasa sola a la siguiente canción.
