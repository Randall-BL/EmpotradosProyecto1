# `server/` — Servidor web embebido

Servidor HTTP en C sobre `libmicrohttpd`. Expone la API REST de control y sirve la
interfaz web. Accede al hardware **exclusivamente** a través de `librobot.so`.

- `src/` — código del servidor: enrutado, API REST, autenticación, estado compartido.
- `www/` — interfaz estática: pantalla de login y panel de control.

Se instala en el target vía la receta `robot-server_1.0.bb` y arranca automáticamente
con la unidad systemd `robot-server.service`.
