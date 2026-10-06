# TODO — Proyecto I: Robot Aspiradora Autónomo (Yocto)

**Curso:** CE-1113 Sistemas Empotrados — TEC
**Plataforma:** Raspberry Pi 4 + imagen Linux mínima construida con Yocto
**Fecha de entrega:** 6 de octubre de 2026
**Grupo:** 4 personas

> Leyenda: `[ ]` pendiente · `[~]` en progreso · `[x]` hecho
>
> En las secciones de software, `[x]` significa **implementado y verificado sin la
> Raspberry** (compilación cruzada con BitBake y/o prueba en el simulador `sim/`);
> `[~]` es lo que ya está en código pero **necesita el kit para validarse**. La
> confirmación final de todo el software depende de la demo en hardware.
> **(OBL)** obligatorio · **(OPC)** opcional · **(EVID)** genera evidencia que debe subirse al repositorio

---

## 0. Arranque del proyecto

- [x] Definir roles del grupo (navegación / audio / web+red / Yocto+integración)
- [x] Crear cronograma con hitos hasta el 6 de octubre de 2026
- [ ] Inventariar el kit entregado por el profesor (Raspberry Pi 4, sensores, motores, etc.)
- [x] Definir la arquitectura general del sistema (HW + SW) antes de escribir código
- [x] Acordar convenciones de código (estilo, nombres, estructura de carpetas)

---

## 1. Repositorio y flujo de trabajo Git — 10%

- [x] Inicializar repositorio Git y estructura de directorios del proyecto
- [x] Configurar ramas `main` / `develop` / `feature/*` (Git Flow o GitHub Flow)
- [x] Proteger `main` (merge solo vía Pull Request)
- [x] Adoptar **Conventional Commits** en todo el historial (`feat:`, `fix:`, `docs:`, `chore:`…) — ver `CONTRIBUTING.md`
- [x] Crear **issues** para planeamiento y seguimiento de tareas y bugs
- [x] Vincular commits y PRs a sus issues correspondientes
- [x] Agregar `.gitignore` (build/, tmp/, sstate-cache/, *.o, *.so, artefactos de Yocto)
- [~] Verificar que el historial tenga commits descriptivos y distribuidos en el tiempo (no un único commit final)

---

## 2. Hardware — modelo físico y circuitería

### 2.1 Modelo físico
- [x] Diseñar el chasis (circular o rectangular) — se evalúa funcionalidad **y estética** — carcasa circular de 215 mm impresa en 3D, `modelo-3d/` y `docs/hardware-chasis.md`
- [x] Montar 2 motores DC con sus llantas + rueda loca de apoyo — ruedas impresas de 30 mm y bola de 16 mm
- [x] Montar el radar: HC-SR04 sobre el servo de 180° — `docs/hardware-sensores.md`
- [x] Montar el MPU-6050 plano y firme, con X hacia el frente
- [x] Montar los 4 LEDs indicadores en posición visible
- [x] Montar el parlante y el amplificador PAM8403 con su filtro RC — `docs/hardware-sensores.md`
- [x] Montar la fuente de alimentación portátil a bordo
- [ ] Asegurar el cableado (ruteo limpio, sin cables sueltos que estorben el movimiento)

### 2.2 Seguridad eléctrica — **LECTURA OBLIGATORIA DEL ENUNCIADO**

> El incumplimiento puede dañar permanentemente el equipo prestado por el curso; la responsabilidad es del grupo.

- [x] **Aislamiento galvánico u óptico obligatorio** entre la lógica (3.3 V) y la etapa de potencia
  - [x] Optoacopladores (PC817 / 4N25) en las señales PWM, **o**
  - [ ] Driver con aislamiento integrado (L298N con optos adicionales, DRV8871) — **verificar** que la entrada opere a 3.3/5 V y que la potencia tenga alimentación independiente
- [x] **Tierras separadas**: GND de lógica y GND de potencia unidas únicamente a través del aislador
- [x] Fuente portátil: power bank USB para la Raspberry Pi y dos baterías alcalinas de 9 V en paralelo para los motores — `docs/hardware-alimentacion.md`
- [x] **Protección de la celda de Li-Ion** (sobredescarga / sobrecarga / cortocircuito) — la trae integrada el power bank; las alcalinas no llevan BMS
- [x] 5 V regulados para la Raspberry Pi — los entrega el power bank por USB-C
- [x] **Dos rieles de alimentación independientes**: lógica (Pi, servo, sensores, LEDs, audio) desde el power bank y potencia (motores) desde las baterías de 9 V
- [x] Servo del radar alimentado de los 5 V de la Raspberry Pi, con la señal directa del GPIO 25 — `docs/hardware-aislamiento.md`
- [x] Probar cada riel con multímetro **antes** de conectar la Raspberry Pi — todas las verificaciones pasaron (sección 5.3 del DI)
- [x] Documentar el diagrama eléctrico completo (para el README y el documento DI) — `documentación/hardware-diagramas.pdf`

---

## 3. Sistema operativo mínimo con Yocto — (OBL)

- [x] Preparar el host de desarrollo (Ubuntu/Debian) con las dependencias de Yocto — documentado en `docs/yocto-setup.md` y ejecutado (la imagen se construyó)
- [x] Clonar Poky + `meta-raspberrypi` + `meta-openembedded` (rama `scarthgap`) — clonadas y en uso en el build
- [x] Configurar `local.conf` con `MACHINE = "raspberrypi4-64"` — versionado como `meta-robot/conf/local.conf.sample`
- [x] Construir la imagen base y arrancarla exitosamente en la Raspberry Pi 4 — imagen construida (rootfs 129 MB) y arrancada en la Pi: `docs/evidencias/ejecucion-target.md`
- [x] Generar el **Toolchain-SDK** para desarrollo cruzado ARM (`bitbake -c populate_sdk`) — instalador de 264 MB generado en `tmp/deploy/sdk`; documentado en `docs/sdk.md`
- [x] Verificar que **todo** el software se compila en el host y se ejecuta en el target — compilación cruzada y ejecución en la Pi verificadas: `docs/evidencias/`
- [x] Incluir **únicamente** los paquetes estrictamente necesarios (servidor web, bibliotecas de audio, decodificador MP3, GPIO)
- [x] Documentar y **justificar cada paquete** agregado más allá de la imagen mínima base — `docs/paquetes.md`

### 3.1 Capa y receta propia — (OBL) (EVID)
- [x] Crear la capa **`meta-robot/`** con su `conf/layer.conf`
- [x] Escribir al menos **una receta BitBake `.bb`** que integre la biblioteca dinámica y/o el servidor web
  - [x] `SRC_URI` que descargue o copie las fuentes
  - [x] Invocar CMake/Autotools con la toolchain de Yocto (`inherit cmake` / `autotools`)
  - [x] Instalar los artefactos en la imagen (`do_install`)
  - [x] Declarar **todas** las dependencias (`DEPENDS` / `RDEPENDS`)
  - [x] Instalar y habilitar la unidad systemd (`inherit systemd`, `SYSTEMD_SERVICE`)
- [~] **Validar la reproducibilidad desde cero**: `bitbake <imagen>` produce la imagen — reproduce la imagen sin pasos manuales; falta validarla en una **máquina limpia**
- [x] **(EVID)** Commitear el directorio `meta-robot/` con la receta y el `layer.conf`
- [x] **(EVID)** Guardar el fragmento de `log.do_compile` que confirme la compilación cruzada exitosa — en `docs/evidencias/`
- [x] **(EVID)** Capturas de pantalla o salida de terminal del binario ejecutándose en el target — `docs/evidencias/ejecucion-target.md`

---

## 4. Biblioteca dinámica de control (`.so`) — (OBL)

- [x] Definir la API pública de la biblioteca (header + versionado)
- [x] Configurar el build con **CMake o Autotools** y compilación cruzada ARM
- [x] Generar el **paquete estándar de código abierto** correspondiente
- [x] Implementar el módulo de **motores (PWM)**: avance, retroceso, giro izquierda/derecha, detención, velocidad por motor — con control diferencial (`motores_set`); L298N siempre habilitado (jumpers `ENA`/`ENB`) y la inversión del PC817 compensada. **A velocidad fija, sin PWM**: desviación acordada con el profesor (`MOTOR_VELOCIDAD_VARIABLE` en 0, en `lib_motors.h`; ver `docs/hardware-aislamiento.md`)
- [x] Implementar el módulo de **sensores de proximidad (GPIO)**: lectura en tiempo real — radar con servo en `lib_radar` + `lib_servo`
- [x] Implementar el módulo del **MPU-6050 (I2C)**: aceleración de avance y giro, con calibración — `lib_imu`
- [x] Implementar el módulo de **LEDs (GPIO)**: control de los 4 indicadores
- [x] Implementar el módulo de **reproducción de audio**
- [x] Manejo de errores y liberación segura de los recursos GPIO (init/cleanup)
- [x] **Verificar que el servidor web use exclusivamente esta biblioteca** para tocar hardware — se eliminó `robot_hardware.c`; `readelf` confirma que el servidor ya no enlaza pigpio
- [x] Documentar la API completa (para el README) — `docs/api-librobot.md`
- [x] Programa de prueba independiente que ejercite cada función de la biblioteca — `sim/prueba_librobot`, 47/47 comprobaciones

---

## 5. Navegación autónoma — (OBL)

- [~] Implementar el **modo autónomo** con al menos un algoritmo de cobertura reactiva — reactivo con rebote implementado y probado en el simulador; falta **prueba de campo**
- [x] Comportamiento ante obstáculo: **detenerse → retroceder → cambiar de dirección** automáticamente
- [x] Detección **frontal y lateral**: radar con un HC-SR04 sobre un servo de 180°, 7 direcciones (mont. físico en §2)
- [x] Confirmado con el profesor: el radar y el MPU-6050 bastan para "**al menos dos sensores**" (desviación acordada); no se monta un 2.º HC-SR04
- [x] Lectura y procesamiento de sensores **en tiempo real** — ~10 lecturas/s, frente cada ~0.6 s; `docs/navegacion-radar.md`
- [x] **Velocidad con el MPU-6050** y **tiempo antes de chocar**, actualizado en cada lectura frontal y proyectado entre lecturas — probado en el simulador
- [x] Evasión: retroceder, barrido completo y giro en lazo cerrado hacia el lado más libre — 2 min en el simulador, 14 evasiones y ningún choque
- [x] **Control diferencial** de los 2 motores DC vía PWM (giros con radio variable) — `motores_set` / `motores_curva` implementados y probados en el simulador; en el robot la velocidad es fija, sin PWM, por desviación acordada con el profesor: gira sobre su eje, una llanta adelante y la otra atrás
- [ ] Calibrar velocidades, umbrales (distancia y tiempo de choque), pulsos del servo y montaje del MPU — `docs/odometria.md`
- [x] **4 LEDs indicadores**: (1) modo autónomo activo, (2) modo manual activo, (3) alerta de obstáculo detectado, (4) sistema encendido — control en código
- [x] Implementar la **odometría** (insumo necesario para el mapa) — `lib_odom` con MPU-6050 + modelo de motores: 5 % de error de posición y 0.4° de rumbo en el simulador; **calibrar en campo**
- [ ] Prueba de campo: navegar un área definida sin colisiones ni bloqueos

---

## 6. Reproducción de audio MP3 — (OBL)

- [x] Reproducir archivos **MP3 almacenados localmente** en el sistema de archivos
- [x] Reproducción **concurrente con la navegación** (proceso o hilo independiente)
- [~] Salida de audio física: PWM por GPIO 18 (`audremap`) → filtro RC → **PAM8403** → parlante, mezclado a mono — configurado en la imagen; falta montar y probar
- [x] Control desde la interfaz: **seleccionar canción de una lista**, reproducir, pausar, detener
- [x] **Control de volumen** desde la interfaz
- [x] **Retroalimentación sonora** (audios cortos) en los 4 eventos:
  - [x] Inicio del sistema
  - [x] Inicio del modo autónomo
  - [x] Obstáculo detectado
  - [x] Cambio a modo manual
- [x] Definir y probar la política de mezcla/prioridad para que los sonidos de evento no interrumpan indefinidamente la música — el evento pausa la música y la reanuda

---

## 7. Control remoto — servidor web / app móvil — (OBL)

- [x] Levantar el servidor web sobre WiFi/Bluetooth, accesible desde celular o PC — activo en la Raspberry Pi 4, puerto 8080: `docs/evidencias/ejecucion-target.md`
- [x] **Pantalla de login** con al menos un usuario registrado y un protocolo de seguridad mínimo — SHA-256 + sesiones
- [x] Conmutación **modo autónomo ↔ modo manual**
- [x] En modo manual, los controles direccionales comandan directamente los motores
- [x] **Panel de control** que muestre y/o provea:
  - [x] Indicador del modo activo (autónomo/manual)
  - [x] Controles direccionales (modo manual)
  - [x] Lecturas de los sensores de proximidad **en tiempo real** — radar semicircular, velocidad del MPU y tiempo de choque
  - [x] Control completo de audio (lista, reproducir/pausar/detener, volumen)
  - [x] Estado de los LEDs
  - [x] Visualización del mapa de recorrido en tiempo real
- [x] Bloquear los controles manuales mientras el robot está en modo autónomo (y viceversa)

### 7.1 Mapa de recorrido — (OBL)
- [x] Construir un mapa incremental a partir de los **sensores de proximidad + la odometría** — cada lectura del radar con la pose al medir
- [x] Representarlo como **grilla 2D** con 3 estados por celda: visitada / obstáculo detectado / desconocida — celdas de **30 cm**
- [x] Transmitir el mapa al cliente y **actualizarlo en tiempo real** conforme el robot avanza
- [x] Renderizar el mapa en la interfaz web/móvil

### 7.2 Ejecución automática — (OBL)
- [x] Crear la unidad **systemd propia** (`.service`) para la aplicación del servidor
- [x] Arranque automático al energizar el sistema — `WantedBy=multi-user.target`, habilitado desde la receta
- [x] `Restart=on-failure` configurado y probado — verificado con systemd + simulador (`docs/evidencias/systemd-reinicio.md`)
- [x] **Sin interfaz gráfica local** en el sistema embebido
- [x] Habilitar el servicio desde la receta Yocto (no manualmente en el target)

---

## 8. Eficiencia de recursos — (OBL) (EVID)

- [x] Medir el **tamaño del sistema de archivos raíz (rootfs)** — **129 MB** sobre la imagen construida
- [~] Medir el **tiempo de arranque** desde el reset hasta que el servicio de control queda operativo — 17 s medidos el 17 de setiembre; falta repetirlo sobre la imagen final
- [ ] Medir el **uso de memoria RAM** en operación normal
- [ ] Medir el **uso de CPU** en operación normal
- [ ] Escenario de medición: navegación autónoma + audio + servidor web **simultáneos**
- [ ] Justificar cualquier desviación del presupuesto de referencia:
  - [x] rootfs ≤ **200 MB** — 129 MB, dentro del presupuesto
  - [ ] tiempo de arranque ≤ **15 s**
- [x] Documentar las herramientas y el método de medición usados en cada caso (`systemd-analyze`, `du`, `free`, `top`/`htop`, `perf`, lectura directa de `/proc`) — `docs/metricas.md`
- [ ] Incluir la tabla de métricas final en el README

---

## 9. Requerimientos opcionales — (OPC)

- [x] Detección de desnivel / caída con sensores IR orientados hacia abajo + detención preventiva del robot (software; falta montar los sensores, ver `docs/opcionales.md`)
- [x] Notificación de fin de ciclo de limpieza (audio + mensaje en la interfaz), con tiempo o área configurable
- [x] Playlist persistente, almacenada en disco y editable desde la interfaz web

---

## 10. Documentación de atributos profesionales — 10%

### 10.1 Documento de Diseño (DI) — 5%
- [x] **DI1** — Identificación de necesidades y requerimientos del problema complejo de ingeniería, considerando aspectos técnicos, salud y seguridad pública, costos, impacto ambiental y recursos disponibles
- [x] **DI2** — Valoración de alternativas de solución que cumplan las necesidades, considerando múltiples factores (técnicos, económicos, ambientales, sociales)
- [x] **DI3** — Diseño creativo de la alternativa seleccionada, considerando todos los aspectos mencionados
- [~] **DI4** — Validación del diseño final de acuerdo con los requerimientos y las consideraciones de seguridad, costo e impacto — falta la RAM y la CPU en la tabla de eficiencia

### 10.2 Documento de Aprendizaje Continuo (AC) — 5%
- [x] **AC1** — Identificación de necesidades de aprendizaje (conocimientos, habilidades, destrezas o actitudes) en el contexto del proyecto y del cambio tecnológico
- [x] **AC2** — Identificación de tecnologías nuevas y emergentes que contribuyeron con el aprendizaje durante el desarrollo
- [x] **AC3** — Implementación de acciones o estrategias concretas (uso de nuevas tecnologías, organización del tiempo, búsqueda bibliográfica) para solventar las necesidades de aprendizaje
- [x] **AC4** — Evaluación crítica de la eficacia de las estrategias implementadas

---

## 11. README y documentación — 10%

- [x] Instrucciones de **instalación**
- [x] Instrucciones de **compilación** con la toolchain
- [x] Instrucciones de **generación de la imagen Yocto**, incluyendo la receta `.bb` propia y cómo agregar la capa `meta-robot/`
- [x] **Configuración y uso** del sistema (login, modos, audio, mapa)
- [x] **Diagrama de arquitectura de hardware**
- [x] **Diagrama de arquitectura de software**
- [x] **Documentación de la API** de la biblioteca dinámica
- [x] **Evidencias** de la compilación cruzada automatizada (fragmento del log de Yocto + ejecución en el target)
- [~] **Reporte de métricas** de eficiencia de recursos y herramientas utilizadas para medirlas — faltan la RAM y la CPU
- [~] Resultados más relevantes del proyecto con **evidencias de ejecución** (fotos, videos, capturas) — faltan las fotos y el video del robot
- [x] Lista de todo paquete agregado sobre la imagen base + **justificación de cada uno** — `docs/paquetes.md`

---

## 12. Presentación funcional — 70%

- [ ] Ensayo completo de la demo según la rúbrica correspondiente
- [ ] Demostrar navegación autónoma con evasión de obstáculos
- [ ] Demostrar el cambio a modo manual y el control direccional desde la interfaz
- [ ] Demostrar reproducción MP3 + control de volumen concurrente con la navegación
- [ ] Demostrar los 4 sonidos de retroalimentación
- [ ] Demostrar los 4 LEDs indicadores
- [ ] Demostrar el mapa actualizándose en tiempo real
- [ ] Demostrar el arranque automático (reset del sistema en vivo)
- [ ] Estética del modelo físico presentable (rubro evaluado)
- [ ] Plan B ante fallos (batería de repuesto, superficie de prueba controlada, video de respaldo)

---

## 13. Checklist final pre-entrega (6 de octubre de 2026)

- [ ] `bitbake <imagen>` reproduce la imagen desde cero, sin pasos manuales — **verificado en una máquina limpia**
- [ ] Todos los requerimientos obligatorios implementados y probados en el target
- [x] `meta-robot/`, `log.do_compile` y las capturas del target están commiteados
- [ ] README completo con diagramas, API y métricas
- [ ] Documentos DI y AC finalizados y subidos al repositorio
- [ ] Historial Git limpio, con Conventional Commits, ramas e issues cerrados
- [ ] Batería cargada y robot funcionando el día de la presentación
