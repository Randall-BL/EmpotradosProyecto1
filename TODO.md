# TODO — Proyecto I: Robot Aspiradora Autónomo (Yocto)

**Curso:** CE-1113 Sistemas Empotrados — TEC
**Plataforma:** Raspberry Pi 4 + imagen Linux mínima construida con Yocto
**Fecha de entrega:** 6 de octubre de 2026
**Grupo:** 4 personas

> Leyenda: `[ ]` pendiente · `[~]` en progreso · `[x]` hecho
> **(OBL)** obligatorio · **(OPC)** opcional · **(EVID)** genera evidencia que debe subirse al repositorio

---

## 0. Arranque del proyecto

- [ ] Definir roles del grupo (navegación / audio / web+red / Yocto+integración)
- [ ] Crear cronograma con hitos hasta el 6 de octubre de 2026
- [ ] Inventariar el kit entregado por el profesor (Raspberry Pi 4, sensores, motores, etc.)
- [ ] Definir la arquitectura general del sistema (HW + SW) antes de escribir código
- [ ] Acordar convenciones de código (estilo, nombres, estructura de carpetas)

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
- [ ] Diseñar el chasis (circular o rectangular) — se evalúa funcionalidad **y estética**
- [ ] Montar 2 motores DC con sus llantas + rueda loca de apoyo
- [ ] Montar los sensores de proximidad (frontal + lateral)
- [ ] Montar los 4 LEDs indicadores en posición visible
- [ ] Montar el altavoz / salida de audio
- [ ] Montar la fuente de alimentación portátil a bordo
- [ ] Asegurar el cableado (ruteo limpio, sin cables sueltos que estorben el movimiento)

### 2.2 Seguridad eléctrica — **LECTURA OBLIGATORIA DEL ENUNCIADO**

> El incumplimiento puede dañar permanentemente el equipo prestado por el curso; la responsabilidad es del grupo.

- [ ] **Aislamiento galvánico u óptico obligatorio** entre la lógica (3.3 V) y la etapa de potencia
  - [ ] Optoacopladores (PC817 / 4N25) en las señales PWM, **o**
  - [ ] Driver con aislamiento integrado (L298N con optos adicionales, DRV8871) — **verificar** que la entrada opere a 3.3/5 V y que la potencia tenga alimentación independiente
- [ ] **Tierras separadas**: GND de lógica y GND de potencia unidas únicamente a través del aislador
- [ ] Batería Li-Ion 18650 (1S o 2S) o LiPo de modelo RC
- [ ] **Módulo BMS** de protección (sobredescarga / sobrecarga / cortocircuito) — verificar si el pack ya lo incluye
- [ ] Regulador **buck-boost** DC-DC a 5 V con ≥ 3 A para la Raspberry Pi (XL6009/MT3608 elevador; MP2307/LM2596 reductor)
- [ ] **Dos rieles de alimentación independientes**: lógica (Pi, sensores, LEDs, audio) y potencia (motores), regulados por separado
- [ ] Probar cada riel con multímetro **antes** de conectar la Raspberry Pi
- [ ] Documentar el diagrama eléctrico completo (para el README y el documento DI)

---

## 3. Sistema operativo mínimo con Yocto — (OBL)

- [~] Preparar el host de desarrollo (Ubuntu/Debian) con las dependencias de Yocto — documentado en `docs/yocto-setup.md`, falta ejecutarlo
- [~] Clonar Poky + `meta-raspberrypi` + `meta-openembedded` (rama `scarthgap`) — procedimiento documentado
- [x] Configurar `local.conf` con `MACHINE = "raspberrypi4-64"` — versionado como `meta-robot/conf/local.conf.sample`
- [ ] Construir la imagen base y arrancarla exitosamente en la Raspberry Pi 4 — **requiere el kit**
- [~] Generar el **Toolchain-SDK** para desarrollo cruzado ARM (`bitbake -c populate_sdk`) — documentado en `docs/sdk.md`
- [~] Verificar que **todo** el software se compila en el host y se ejecuta en el target — método de verificación documentado
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
- [ ] **Validar la reproducibilidad desde cero**: `bitbake <imagen>` produce la imagen final sin pasos manuales adicionales
- [x] **(EVID)** Commitear el directorio `meta-robot/` con la receta y el `layer.conf`
- [ ] **(EVID)** Guardar el fragmento de `log.do_compile` que confirme la compilación cruzada exitosa
- [ ] **(EVID)** Capturas de pantalla o salida de terminal del binario ejecutándose en el target

---

## 4. Biblioteca dinámica de control (`.so`) — (OBL)

- [ ] Definir la API pública de la biblioteca (header + versionado)
- [ ] Configurar el build con **CMake o Autotools** y compilación cruzada ARM
- [ ] Generar el **paquete estándar de código abierto** correspondiente
- [ ] Implementar el módulo de **motores (PWM)**: avance, retroceso, giro izquierda/derecha, detención, velocidad por motor
- [ ] Implementar el módulo de **sensores de proximidad (GPIO)**: lectura en tiempo real
- [ ] Implementar el módulo de **LEDs (GPIO)**: control de los 4 indicadores
- [ ] Implementar el módulo de **reproducción de audio**
- [ ] Manejo de errores y liberación segura de los recursos GPIO (init/cleanup)
- [ ] **Verificar que el servidor web use exclusivamente esta biblioteca** para tocar hardware (ningún acceso directo a GPIO desde el servidor)
- [ ] Documentar la API completa (para el README)
- [ ] Programa de prueba independiente que ejercite cada función de la biblioteca

---

## 5. Navegación autónoma — (OBL)

- [ ] Implementar el **modo autónomo** con al menos un algoritmo de cobertura reactiva (aleatorio con rebote, espiral o zig-zag)
- [ ] Comportamiento ante obstáculo: **detenerse → retroceder → cambiar de dirección** automáticamente
- [ ] Integrar **≥ 2 sensores de proximidad** (HC-SR04 ultrasónicos o infrarrojos): detección **frontal y lateral**
- [ ] Lectura y procesamiento de sensores **en tiempo real** (frecuencia de muestreo definida y documentada)
- [ ] **Control diferencial** de los 2 motores DC vía PWM (giros con radio variable)
- [ ] Calibrar velocidades y umbrales de distancia
- [ ] **4 LEDs indicadores**: (1) modo autónomo activo, (2) modo manual activo, (3) alerta de obstáculo detectado, (4) sistema encendido
- [ ] Implementar la **odometría** de los motores (insumo necesario para el mapa)
- [ ] Prueba de campo: navegar un área definida sin colisiones ni bloqueos

---

## 6. Reproducción de audio MP3 — (OBL)

- [ ] Reproducir archivos **MP3 almacenados localmente** en el sistema de archivos
- [ ] Reproducción **concurrente con la navegación** (proceso o hilo independiente)
- [ ] Salida de audio física: jack 3.5 mm con pequeño amplificador, **o** DAC externo I2S/I2C con amplificador integrado
- [ ] Control desde la interfaz: **seleccionar canción de una lista**, reproducir, pausar, detener
- [ ] **Control de volumen** desde la interfaz
- [ ] **Retroalimentación sonora** (audios cortos) en los 4 eventos:
  - [ ] Inicio del sistema
  - [ ] Inicio del modo autónomo
  - [ ] Obstáculo detectado
  - [ ] Cambio a modo manual
- [ ] Definir y probar la política de mezcla/prioridad para que los sonidos de evento no interrumpan indefinidamente la música

---

## 7. Control remoto — servidor web / app móvil — (OBL)

- [ ] Levantar el servidor web sobre WiFi/Bluetooth, accesible desde celular o PC
- [ ] **Pantalla de login** con al menos un usuario registrado y un protocolo de seguridad mínimo
- [ ] Conmutación **modo autónomo ↔ modo manual**
- [ ] En modo manual, los controles direccionales comandan directamente los motores
- [ ] **Panel de control** que muestre y/o provea:
  - [ ] Indicador del modo activo (autónomo/manual)
  - [ ] Controles direccionales (modo manual)
  - [ ] Lecturas de los sensores de proximidad **en tiempo real**
  - [ ] Control completo de audio (lista, reproducir/pausar/detener, volumen)
  - [ ] Estado de los LEDs
  - [ ] Visualización del mapa de recorrido en tiempo real
- [ ] Bloquear los controles manuales mientras el robot está en modo autónomo (y viceversa)

### 7.1 Mapa de recorrido — (OBL)
- [ ] Construir un mapa incremental a partir de los **sensores de proximidad + la odometría** de los motores
- [ ] Representarlo como **grilla 2D** con 3 estados por celda: visitada / obstáculo detectado / desconocida
- [ ] Transmitir el mapa al cliente y **actualizarlo en tiempo real** conforme el robot avanza
- [ ] Renderizar el mapa en la interfaz web/móvil

### 7.2 Ejecución automática — (OBL)
- [x] Crear la unidad **systemd propia** (`.service`) para la aplicación del servidor
- [~] Arranque automático al energizar el sistema — configurado, falta probarlo en el target
- [~] `Restart=on-failure` configurado y probado — configurado; procedimiento de prueba en `docs/arranque-automatico.md`
- [x] **Sin interfaz gráfica local** en el sistema embebido
- [x] Habilitar el servicio desde la receta Yocto (no manualmente en el target)

---

## 8. Eficiencia de recursos — (OBL) (EVID)

- [ ] Medir el **tamaño del sistema de archivos raíz (rootfs)**
- [ ] Medir el **tiempo de arranque** desde el reset hasta que el servicio de control queda operativo
- [ ] Medir el **uso de memoria RAM** en operación normal
- [ ] Medir el **uso de CPU** en operación normal
- [ ] Escenario de medición: navegación autónoma + audio + servidor web **simultáneos**
- [ ] Justificar cualquier desviación del presupuesto de referencia:
  - [ ] rootfs ≤ **200 MB**
  - [ ] tiempo de arranque ≤ **15 s**
- [ ] Documentar las herramientas y el método de medición usados en cada caso (`systemd-analyze`, `du`, `free`, `top`/`htop`, `perf`, lectura directa de `/proc`)
- [ ] Incluir la tabla de métricas final en el README

---

## 9. Requerimientos opcionales — (OPC)

- [ ] Detección de desnivel / caída con sensores IR orientados hacia abajo + detención preventiva del robot
- [ ] Notificación de fin de ciclo de limpieza (audio + mensaje en la interfaz), con tiempo o área configurable
- [ ] Playlist persistente, almacenada en disco y editable desde la interfaz web

---

## 10. Documentación de atributos profesionales — 10%

### 10.1 Documento de Diseño (DI) — 5%
- [ ] **DI1** — Identificación de necesidades y requerimientos del problema complejo de ingeniería, considerando aspectos técnicos, salud y seguridad pública, costos, impacto ambiental y recursos disponibles
- [ ] **DI2** — Valoración de alternativas de solución que cumplan las necesidades, considerando múltiples factores (técnicos, económicos, ambientales, sociales)
- [ ] **DI3** — Diseño creativo de la alternativa seleccionada, considerando todos los aspectos mencionados
- [ ] **DI4** — Validación del diseño final de acuerdo con los requerimientos y las consideraciones de seguridad, costo e impacto

### 10.2 Documento de Aprendizaje Continuo (AC) — 5%
- [ ] **AC1** — Identificación de necesidades de aprendizaje (conocimientos, habilidades, destrezas o actitudes) en el contexto del proyecto y del cambio tecnológico
- [ ] **AC2** — Identificación de tecnologías nuevas y emergentes que contribuyeron con el aprendizaje durante el desarrollo
- [ ] **AC3** — Implementación de acciones o estrategias concretas (uso de nuevas tecnologías, organización del tiempo, búsqueda bibliográfica) para solventar las necesidades de aprendizaje
- [ ] **AC4** — Evaluación crítica de la eficacia de las estrategias implementadas

---

## 11. README y documentación — 10%

- [ ] Instrucciones de **instalación**
- [ ] Instrucciones de **compilación** con la toolchain
- [ ] Instrucciones de **generación de la imagen Yocto**, incluyendo la receta `.bb` propia y cómo agregar la capa `meta-robot/`
- [ ] **Configuración y uso** del sistema (login, modos, audio, mapa)
- [ ] **Diagrama de arquitectura de hardware**
- [ ] **Diagrama de arquitectura de software**
- [ ] **Documentación de la API** de la biblioteca dinámica
- [ ] **Evidencias** de la compilación cruzada automatizada (fragmento del log de Yocto + ejecución en el target)
- [ ] **Reporte de métricas** de eficiencia de recursos y herramientas utilizadas para medirlas
- [ ] Resultados más relevantes del proyecto con **evidencias de ejecución** (fotos, videos, capturas)
- [ ] Lista de todo paquete agregado sobre la imagen base + **justificación de cada uno**

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
- [ ] `meta-robot/`, `log.do_compile` y las capturas del target están commiteados
- [ ] README completo con diagramas, API y métricas
- [ ] Documentos DI y AC finalizados y subidos al repositorio
- [ ] Historial Git limpio, con Conventional Commits, ramas e issues cerrados
- [ ] Batería cargada y robot funcionando el día de la presentación
