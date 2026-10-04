# Pendientes para la entrega

**Entrega:** 6 de octubre de 2026 · **Estado al:** 4 de octubre de 2026

El Documento de Diseño, el de Aprendizaje Continuo, el README y el reporte de métricas
se escribieron **dando por completos todos los requerimientos obligatorios**. Donde hace
falta un dato que solo el grupo tiene —una medición, una foto, un nombre— quedó la marca
`PENDIENTE`. Este archivo las reúne, junto con lo que el repositorio todavía contradice.

Para encontrar las marcas:

```bash
grep -rn "PENDIENTE" README.md docs/metricas.md
grep -n "pendiente{" documentación/*.tex
```

Después de editar un `.tex` hay que recompilarlo dos veces y subir también el PDF:

```bash
cd documentación && pdflatex documento-diseno.tex && pdflatex documento-diseno.tex
```

---

## 1. Decisiones que cambian lo que dicen los documentos

Son las tres cosas en que el texto afirma algo que el repositorio hoy no respalda.
Conviene resolverlas primero, porque tocan varios archivos.

- [ ] **Velocidad de los motores por PWM (R3).** El código tiene
      `MOTOR_VELOCIDAD_VARIABLE` en 0 (`lib/lib_motors.h`): los motores van a velocidad
      fija y no hay giros de radio variable, que el enunciado pide. La matriz del DI
      dice "Cumple".
  - Si se recupera la PWM: aplicar una de las correcciones de
    [`docs/hardware-aislamiento.md`](docs/hardware-aislamiento.md#por-ahora-velocidad-fija)
    (PWM a 100 Hz, pull-up de 1 kΩ, o llevar el rango del panel a un ciclo de 150–255),
    poner la constante en 1 y describir lo que se hizo en el hallazgo 3 del DI.
  - Si queda fija: cambiar R3 a "Cumple parcialmente" en la matriz del DI y agregarlo a
    las limitaciones conocidas.
  - En ambos casos: actualizar `docs/hardware-aislamiento.md`, `docs/api-librobot.md`,
    `docs/odometria.md`, `TODO.md` y el issue #15.
- [ ] **"Al menos dos sensores de proximidad" (R2).** El radar usa un solo HC-SR04
      sobre un servo. Confirmar con el profesor que cumple. Si no, montar un segundo
      HC-SR04 fijo al frente en los GPIO 22 (`TRIG`) y 10 (`ECHO`), con su divisor, y
      quitar la limitación del DI.
- [ ] **Paquetes de desarrollo en la imagen de entrega.** El README y
      `docs/paquetes.md` dicen que `ssh-server-openssh`, `openssh-sftp-server` y
      `debug-tweaks` se retiran, pero siguen en `robot-image.bb` y en
      `local.conf.sample`. Retirarlos **después** de medir las métricas (el script usa
      SSH), o cambiar el texto.

---

## 2. Métricas — issue #30

Faltan la RAM y la CPU. El rootfs (129 MB) y el arranque (17 s) ya están, pero el
arranque se midió el 17 de setiembre y hay que repetirlo sobre la imagen final.

- [ ] Grabar la imagen final, encender el robot **en el piso y con espacio libre**, y
      sin reiniciar el servicio correr:
      ```bash
      ./scripts/medir-metricas.sh <ip-del-robot>
      ```
      El script pone el robot en modo autónomo con música durante 60 s e imprime la
      tabla. **No se ha probado contra el robot**: si falla, los comandos de la sección
      *Método* de [`docs/metricas.md`](docs/metricas.md) se pueden correr a mano.
- [ ] Pegar la tabla en `docs/metricas.md` y pasar los valores a la tabla de la sección
      10 del README y a la de la sección 5.4 del DI.
- [ ] Si el arranque ya no es 17 s, corregirlo en esos tres lugares. Si baja de 15 s,
      quitar la justificación de la desviación.
- [ ] Opcional: tiempo completo desde el reset, con un video del robot al energizarlo
      hasta que enciende el LED verde.
- [ ] Marcar la lista de *Estado* al final de `docs/metricas.md`.

---

## 3. Documento de Diseño — issue #31

Archivo: `documentación/documento-diseno.tex`.

| Dónde | Qué falta |
|---|---|
| Portada | Nombres y carnés de los cuatro integrantes |
| 2.5 Costos | El costo real con las facturas. La tabla usa la estimación de 49–99 USD |
| 3 Valoración de alternativas | Revisar las notas de las matrices: se asignaron a partir del razonamiento de `docs/`, no las puso el grupo |
| 5.2 Matriz de cumplimiento | R3, según lo que se decida en la sección 1 |
| 5.3 Validación de la seguridad | Los valores medidos con el multímetro en las siete verificaciones |
| 5.4 Validación de la eficiencia | RAM y CPU |
| 5.5 Costo | Comparar el costo real con la estimación |
| 5.6 Hallazgo 3 | La corrección aplicada a la PWM |

---

## 4. Documento de Aprendizaje Continuo — issue #32

Archivo: `documentación/aprendizaje-continuo.tex`. Habla en nombre del grupo, así que
los cuatro deben leerlo completo.

| Dónde | Qué falta |
|---|---|
| Portada | Nombres y carnés |
| 2.1 Contexto | El punto de partida (qué sabía el grupo al inicio) es una suposición: corregirlo |
| 3 Tecnologías | Si se usaron asistentes de IA u otras tecnologías emergentes, decir para qué y cómo se verificó lo que produjeron |
| 5 Evaluación crítica | Se armó con lo que muestra el repositorio. Confirmar que refleja la experiencia real, en especial "varios Pull Requests se aprobaron sin una revisión a fondo" |

---

## 5. README — issue #33

| Sección | Qué falta |
|---|---|
| 9.2 | `docs/evidencias/binarios-target.md` es del 8 de setiembre y todavía muestra `libpigpiod_if2` enlazada al servidor. Regenerarlo con la imagen final |
| 9.4 | La salida de los comandos en la Raspberry Pi 4 |
| 10 | RAM y CPU |
| 11 | Fotos del robot armado y video de la demostración |

---

## 6. Evidencias por capturar en el robot

- [ ] Guardar en `docs/evidencias/ejecucion-target.md` la salida de:
      ```bash
      uname -a
      systemctl status robot-server
      ls -l /usr/lib/librobot.so*
      journalctl -u robot-server -b | head -n 30
      which gcc make cmake          # los tres deben fallar
      vcgencmd get_throttled        # si esta en la imagen; navegando con audio debe dar 0x0
      ```
- [ ] Regenerar `binarios-target.md` con los comandos de *Cómo reproducirlo* de
      [`docs/evidencias/README.md`](docs/evidencias/README.md).
- [ ] Repetir `sim/construir_arm.sh` y actualizar `ejecucion-cruzada-qemu.md`: la
      captura guardada es de cuando la prueba tenía 18 comprobaciones, hoy son 47.
- [ ] Fotos: el robot armado (arriba, frente y cableado interno) y las tres pestañas
      del panel web.
- [ ] Video corto de la demostración: navegación autónoma, cambio a manual, audio,
      LEDs y mapa. Sirve además como plan B de la presentación.
- [ ] Enlazar todo desde las secciones 9.4 y 11 del README.

---

## 7. Issues abiertos en GitHub

| Issue | Qué falta para cerrarlo |
|---|---|
| #3 Chasis | Todas las casillas: armado, motores, fuente a bordo, cableado y acabado |
| #6 Montaje de sensores, LEDs y audio | Salida de audio y diagrama eléctrico completo. Su comentario describe el diseño viejo (tres sensores y LM386) |
| #15 Motores PWM | Calibrar velocidades; depende de la sección 1 |
| #23 MP3 concurrente | Salida de audio física y verificar que el audio no degrade los sensores |
| #24 Sonidos de evento | Nada: todas las casillas están marcadas, solo cerrarlo |
| #30 Métricas | Sección 2 de este archivo |
| #31, #32, #33 | Secciones 3, 4 y 5 |
| #34 Presentación | Ensayo completo de la demo y plan B: batería de repuesto y video de respaldo |
| #35, #36, #37 Opcionales | No se implementaron: cerrarlos como no planeados |

---

## 8. Orden del repositorio

- [ ] `TODO.md`: marcar lo que ya está hecho. Las secciones 2, 8, 10, 11, 12 y 13
      siguen casi enteras sin marcar.
- [ ] Listas de *Estado* al final de los documentos de `docs/`: varias dicen
      "pendiente: requiere el kit" (`yocto-setup.md`, `sdk.md`, `paquetes.md`,
      `arranque-automatico.md`, los `hardware-*.md` y `evidencias/README.md`).
- [ ] Cerrar los hitos sin issues abiertos (*Sistema base con Yocto* y *Navegación
      autónoma*) y mover las tarjetas del tablero.
- [ ] Asignar responsable a los issues: ninguno tiene.
- [ ] `COMPRAS.md` está sin agregar a Git: subirlo o borrarlo.
- [ ] Abrir el Pull Request de esta rama hacia `develop` y luego hacia `main`, con
      `Closes #31`, `#32` y `#33` cuando corresponda.

---

## 9. Antes de entregar

- [ ] `grep` de la primera sección no encuentra ninguna marca.
- [ ] Los dos PDF están recompilados y subidos.
- [ ] Borrar este archivo.
