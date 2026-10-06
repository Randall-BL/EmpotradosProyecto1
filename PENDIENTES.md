# Pendientes para la entrega

**Entrega:** 6 de octubre de 2026 · **Estado al:** 5 de octubre de 2026

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

Faltan la RAM y la CPU, y hay que repetir el tiempo de arranque: la tabla dice 17 s
(17 de setiembre), pero la captura del 5 de octubre muestra el servidor escuchando a los
24.7 s. Eso ya se corrigió en el código y no se volvió a medir.

- [ ] Grabar la imagen final, encender el robot **en el piso y con espacio libre**, y
      sin reiniciar el servicio correr:
      ```bash
      ./scripts/medir-metricas.sh <ip-del-robot>
      ```
      **No se ha probado contra el robot**: si falla, los comandos de la sección
      *Método* de [`docs/metricas.md`](docs/metricas.md) se pueden correr a mano.
- [ ] Pegar la tabla en `docs/metricas.md` y pasar los valores a la sección 10 del
      README y a la sección 5.4 del Documento de Diseño.
- [ ] Si el arranque baja de 15 s, quitar la justificación de la desviación en esos tres
      lugares.

---

## 2. Calibración del robot — issue #15

El código trae las constantes del simulador, que supone un robot de 30 cm/s. Con los
motores de 60 rpm a 12 V y las ruedas de 30 mm, el real va a unos 5 cm/s.

- [ ] Medir la velocidad a fondo y ponerla en `ODOM_VEL_MAX_CM_S` (`lib/lib_odom.h`).
- [ ] Medir la separación entre ruedas y ponerla en `ODOM_ENTRE_EJES_CM`.
- [ ] Revisar el tope de tiempo de los giros (`GIRO_MS_POR_GRADO`) y el retroceso
      (`RETROCESO_MS`) en `server/src/main.c`: están pensados para un robot más rápido.
- [ ] Calibrar los pulsos del servo y el montaje del MPU-6050
      ([`docs/odometria.md`](docs/odometria.md)).

---

## 3. Evidencias por capturar en el robot — issue #33

- [ ] Fotos: el robot armado (arriba, frente y por dentro) y las tres pestañas del panel
      web. Enlazarlas desde la sección 11 del README.
- [ ] Video corto de la demostración: navegación autónoma, cambio a manual, audio, LEDs
      y mapa. Sirve además como plan B de la presentación.
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

| Issue | Qué falta para cerrarlo |
|---|---|
| #3 Chasis | Cableado y acabado: cerrarlo al subir las fotos del robot |
| #15 Motores | La calibración de la sección 2 |
| #23 MP3 concurrente | Verificar en el robot que el audio no degrade los sensores |
| #30 Métricas | Sección 1 |
| #31 Documento de Diseño | La RAM y la CPU en la sección 5.4 |
| #33 README | Métricas, fotos y video |
| #34 Presentación | Ensayo completo de la demo y plan B: baterías de 9 V de repuesto y video de respaldo |

---

## 6. Antes de entregar

- [ ] El `grep` del inicio no encuentra ninguna marca.
- [ ] Los PDF están recompilados y subidos.
- [ ] Borrar este archivo.
