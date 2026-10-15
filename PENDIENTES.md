# Pendientes para la entrega

**Entrega:** 6 de octubre de 2026 · **Estado al:** 15 de octubre de 2026

Lo que falta se resuelve con el robot en la mano. La documentación ya describe el robot
real: velocidad fija y sensores como desviaciones acordadas con el profesor, alimentación
con power bank y baterías de 9 V, y la carcasa impresa en 3D.

Para encontrar las marcas que quedan:

```bash
grep -rn "PENDIENTE" README.md docs/metricas.md
grep -n "pendiente{" documentación/*.tex
```

Después de editar un `.tex` hay que recompilarlo dos veces y subir también el PDF:

```bash
cd documentación && pdflatex documento-diseno.tex && pdflatex documento-diseno.tex
```

---

## 1. Métricas — issue #30

Hecho. Medidas el 5 de octubre de 2026 sobre la imagen final, con el robot armado:
rootfs 129 MB, arranque 19.2 s, RAM 97 MB y CPU 3.6 %.

- [x] Grabar la imagen final, encender el robot **en el piso y con espacio libre**, y
      sin reiniciar el servicio correr `./scripts/medir-metricas.sh <ip-del-robot>`.
- [x] Pegar la tabla en `docs/metricas.md` y pasar los valores a la sección 10 del
      README y a la sección 5.4 del Documento de Diseño.
- [x] Si el arranque baja de 15 s, quitar la justificación de la desviación en esos tres
      lugares. No bajó: la justificación se mantiene.

---

## 2. Calibración del robot — issue #15

El código trae las constantes del simulador, que supone un robot de 30 cm/s. Con los
motores de 60 rpm a 12 V y las ruedas de 30 mm, el real va a unos 5 cm/s.

- [x] Medir la velocidad a fondo y ponerla en `ODOM_VEL_MAX_CM_S` (`lib/lib_odom.h`):
      2 cm/s.
- [x] Medir la separación entre ruedas y ponerla en `ODOM_ENTRE_EJES_CM`: 16 cm. El tope de
      tiempo de los giros sale de esta constante y de la velocidad.
- [x] Tope de tiempo de los giros: ahora se calcula con el modelo de los motores
      (`GIRO_GRADOS_S` en `server/src/main.c`).
- [ ] Revisar el retroceso (`RETROCESO_MS`) en `server/src/main.c`: está pensado para
      un robot más rápido.
- [ ] Calibrar los pulsos del servo y el montaje del MPU-6050
      ([`docs/odometria.md`](docs/odometria.md)).

---

## 3. Evidencias por capturar en el robot — issue #33

- [x] Video de la demostración: `Capturas/demo-evasion.mp4`, enlazado desde la sección
      11 del README. Muestra la evasión en modo autónomo; no muestra el cambio a manual,
      el audio ni el mapa.
- [x] Foto de la primera versión del montaje: `Capturas/montaje-primera-version.jpeg`.
- [ ] Fotos del robot armado (arriba, frente y por dentro) y de las tres pestañas del
      panel web, en `Capturas/` y enlazadas desde la sección 11 del README.
- [ ] Opcional: `kill -9` del servidor en la Raspberry Pi para mostrar el reinicio
      automático en el target; hoy la prueba está hecha sobre el simulador.

---

## 4. Revisar entre los cuatro

- [ ] Documento de Diseño, sección 3: las notas de las matrices de alternativas.
- [ ] Documento de Diseño, sección 2.5: el tipo de cambio de 500 colones por dólar.
- [ ] `docs/hardware-chasis.md`: el material con que se imprimió cada pieza y dónde
      quedaron el radar, el L298N, el power bank y las baterías dentro de la carcasa.

---

## 5. Issues abiertos en GitHub

Solo queda uno; los demás están cerrados.

| Issue | Qué falta para cerrarlo |
|---|---|
| #33 README | Nada obligatorio: el video y la foto ya están en el README. Se cierra al fusionar ese cambio |

---

## 6. Antes de entregar

- [ ] El `grep` del inicio no encuentra ninguna marca.
- [ ] Los PDF están recompilados y subidos.
- [ ] Borrar este archivo.
