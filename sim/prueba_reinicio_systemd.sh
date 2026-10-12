#!/bin/sh
# Proyecto I - Robot Aspiradora Autonomo — CE-1113, TEC
#
# Prueba de la recuperacion ante fallo del servicio (issue #12), sin la
# Raspberry: monta una unidad de usuario systemd que corre robot-server-sim
# con las MISMAS directivas de recuperacion que robot-server.service real
# (Restart=on-failure, RestartSec, StartLimit), lo mata con kill -9 y verifica
# que systemd lo reinicia con un PID nuevo.
#
# Uso:  ./construir_servidor.sh && ./prueba_reinicio_systemd.sh
set -e
SIM="$(cd "$(dirname "$0")" && pwd)"
UNIT=~/.config/systemd/user/robot-server-sim.service

mkdir -p "$(dirname "$UNIT")" "$SIM/run"
( cd "$SIM/run" && ln -sfn ../../server/www www && ln -sfn ../../audio audio )

cat > "$UNIT" <<UNITEOF
[Unit]
Description=Robot server (simulador) - prueba de reinicio automatico
# El limitador de arranques va en [Unit], igual que en robot-server.service:
# puesto en [Service], systemd lo ignora con un aviso.
StartLimitIntervalSec=60
StartLimitBurst=5
[Service]
Type=simple
ExecStart=$SIM/robot-server-sim
WorkingDirectory=$SIM/run
Restart=on-failure
RestartSec=2
[Install]
WantedBy=default.target
UNITEOF

limpiar() {
    systemctl --user stop robot-server-sim.service 2>/dev/null || true
    rm -f "$UNIT"; systemctl --user daemon-reload
}

# Sondea GET / hasta que el servidor responde 200 (maximo 15 s) e imprime el
# ultimo codigo. Un sleep fijo no sirve: robot_init() calibra el MPU y barre el
# radar antes de abrir el puerto, y segun la maquina eso pasa de los 3 s.
esperar_http() {
    i=0
    while [ $i -lt 15 ]; do
        CODE=$(curl -s -o /dev/null -w '%{http_code}' http://localhost:8080/ || true)
        [ "$CODE" = "200" ] && break
        i=$((i + 1)); sleep 1
    done
    echo "$CODE"
}

systemctl --user daemon-reload
systemctl --user start robot-server-sim.service
HTTP1=$(esperar_http)
PID1=$(systemctl --user show robot-server-sim -p MainPID --value)
echo "arrancado bajo systemd, PID $PID1; sirve HTTP $HTTP1"
# Sin esta guarda, un arranque fallido dejaria PID1=0 y kill -9 0 mataria al
# grupo de procesos de esta misma prueba.
[ "$HTTP1" = "200" ] && [ "$PID1" != "0" ] || { echo "FALLO: el servidor no respondio bajo systemd"; limpiar; exit 1; }

echo "matando el proceso (kill -9) para simular un crash..."
kill -9 "$PID1"; sleep 1
HTTP2=$(esperar_http)

PID2=$(systemctl --user show robot-server-sim -p MainPID --value)
NR=$(systemctl --user show robot-server-sim -p NRestarts --value)
echo "tras el crash: PID $PID2, NRestarts=$NR, sirve HTTP $HTTP2"

limpiar

[ "$PID1" != "$PID2" ] && [ "$PID2" != "0" ] && [ "$HTTP2" = "200" ] \
    && echo "OK: systemd reinicio el servidor solo (PID nuevo) y volvio a servir" || { echo "FALLO"; exit 1; }
