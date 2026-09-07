# Guía de contribución

Convenciones acordadas por el grupo para el Proyecto I del curso CE-1113.
Aplican a **todo** el historial del repositorio: es un rubro evaluado (Gestión de
repositorio, 10%).

---

## 1. Modelo de ramas

Se usa **GitHub Flow con rama de integración** (`develop`):

| Rama | Propósito |
|---|---|
| `main` | Solo código estable y presentable. **Protegida**: se actualiza únicamente vía Pull Request. |
| `develop` | Rama de integración. Aquí convergen todas las funcionalidades antes de subir a `main`. |
| `feature/<tema>` | Una rama por issue. Sale de `develop` y vuelve a `develop` por Pull Request. |
| `fix/<tema>` | Corrección de un bug reportado como issue. |
| `docs/<tema>` | Cambios exclusivamente de documentación. |

Nombres de rama en minúscula y con guiones: `feature/meta-robot-layer`,
`fix/pwm-duty-cycle`, `docs/metricas-eficiencia`.

```bash
git switch develop && git pull
git switch -c feature/modulo-motores
# ... trabajo y commits ...
git push -u origin feature/modulo-motores
gh pr create --base develop --title "feat(lib): modulo de motores PWM" --body "Closes #15"
```

### Protección de `main`

`main` tiene activada la regla de protección de rama:

- Prohibido `git push` directo — todo cambio entra por Pull Request.
- Prohibido el force-push y el borrado de la rama.
- Las conversaciones del PR deben resolverse antes de hacer merge.

Si un `git push origin main` es rechazado, **no es un error**: es la protección
funcionando. Abra un PR desde `develop`.

---

## 2. Conventional Commits

Formato obligatorio del mensaje de commit:

```
<tipo>(<ámbito opcional>): <descripción en imperativo, minúscula, sin punto final>

<cuerpo opcional: el porqué del cambio, no el qué>

Refs #<issue>
```

### Tipos

| Tipo | Cuándo usarlo |
|---|---|
| `feat` | Nueva funcionalidad |
| `fix` | Corrección de un defecto |
| `docs` | Solo documentación |
| `chore` | Tareas de mantenimiento, estructura, configuración |
| `refactor` | Reestructuración sin cambio de comportamiento |
| `test` | Pruebas |
| `perf` | Mejoras de rendimiento o de consumo de recursos |
| `build` | Sistema de construcción: CMake, recetas BitBake, SDK |

### Ámbitos del proyecto

`lib`, `server`, `web`, `yocto`, `nav`, `audio`, `hw`, `docs`, `metrics`.

### Ejemplos

```
feat(nav): implementa rebote aleatorio ante obstaculo frontal
fix(lib): libera los recursos GPIO cuando falla la inicializacion del PWM
build(yocto): agrega la receta librobot_1.0.bb con compilacion cruzada
docs(metrics): documenta el metodo de medicion del tiempo de arranque
```

### Reglas

- Un commit = un cambio con sentido propio. Nada de `varios cambios` ni `wip`.
- Descripción de 72 caracteres o menos, en imperativo (`agrega`, no `agregado`).
- Historial **distribuido en el tiempo**: se evalúa que no exista un único commit final.
- Sin acentos ni caracteres especiales en el asunto del commit (compatibilidad de consola).

---

## 3. Vinculación con issues

Todo trabajo nace de un issue. La trazabilidad commit ↔ PR ↔ issue es un rubro evaluado.

- En los **commits**: `Refs #12` al final del cuerpo.
- En los **Pull Requests** hacia `develop`: `Refs #12` (no cierra el issue todavía).
- En el **Pull Request final hacia `main`**: `Closes #12` — GitHub cierra el issue
  automáticamente al hacer merge, porque `main` es la rama por defecto.

Un issue solo se cierra cuando la funcionalidad está **probada en el target**, no cuando
el código está escrito.

### Etiquetas

Los issues usan etiquetas de área (`area:yocto`, `area:lib`, `area:web`, `area:nav`,
`area:audio`, `area:hardware`, `area:metricas`, `area:docs`) y de carácter
(`OBL` obligatorio, `OPC` opcional, `EVID` genera evidencia entregable).

---

## 4. Pull Requests

- Base `develop` para las funcionalidades; base `main` solo para integrar.
- Título con el mismo formato Conventional Commit del cambio principal.
- El cuerpo indica **qué se probó y cómo**; si no se pudo probar en hardware, se dice.
- Al menos una persona del grupo revisa antes del merge.
- Merge con **squash** cuando la rama tiene commits de arreglo intermedios; merge normal
  cuando el historial de la rama ya es limpio.

---

## 5. Convenciones de código

### C (biblioteca y servidor)

- Estándar **C11**, compilado con `-Wall -Wextra`.
- Indentación de **4 espacios**, nunca tabuladores. Llave de apertura en la misma línea.
- Nombres en `snake_case`; constantes y macros en `MAYUSCULAS_CON_GUION_BAJO`.
- Prefijo de módulo en toda función pública: `motors_forward()`, `sensors_read_front()`,
  `leds_set()`, `audio_play()`.
- Las funciones internas del módulo son `static`.
- Un archivo por módulo, con su header. Los headers llevan guarda de inclusión
  (`#ifndef LIB_MOTORS_H`).
- Toda función que pueda fallar devuelve un código de error; el llamador lo verifica.
  Los recursos de GPIO se liberan siempre, incluso en el camino de error.
- Comentarios en español, explicando el **porqué**; el qué debe leerse del código.

### Recetas BitBake

- Un directorio por receta bajo `recipes-<categoría>/<nombre>/`.
- Nombre de archivo `<paquete>_<versión>.bb`; los archivos auxiliares en `files/`.
- Declarar siempre `DEPENDS` (build) y `RDEPENDS` (runtime) de forma explícita.
- Nada de pasos manuales en el target: si algo hay que instalar o habilitar, se hace
  desde la receta.

### Interfaz web

- HTML, CSS y JavaScript sin frameworks ni dependencias externas — el target es un
  sistema embebido con recursos limitados.
- Indentación de 2 espacios. `camelCase` en JavaScript.

---

## 6. Antes de abrir un PR

- [ ] El código compila con el Toolchain-SDK, sin warnings nuevos.
- [ ] Los commits siguen Conventional Commits y referencian su issue.
- [ ] La rama está al día con `develop`.
- [ ] Nada de credenciales, binarios de build ni artefactos de Yocto en el diff.
- [ ] La documentación afectada quedó actualizada en el mismo PR.
