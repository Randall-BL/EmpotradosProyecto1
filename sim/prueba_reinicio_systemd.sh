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
[Service]
Type=simple
ExecStart=$SIM/robot-server-sim
WorkingDirectory=$SIM/run
Restart=on-failure
RestartSec=2
StartLimitIntervalSec=60
StartLimitBurst=5
[Install]
WantedBy=default.target
UNITEOF

systemctl --user daemon-reload
systemctl --user start robot-server-sim.service
sleep 3
PID1=$(systemctl --user show robot-server-sim -p MainPID --value)
echo "arrancado bajo systemd, PID $PID1; sirve HTTP $(curl -s -o /dev/null -w '%{http_code}' http://localhost:8080/)"

echo "matando el proceso (kill -9) para simular un crash..."
kill -9 "$PID1"; sleep 5

PID2=$(systemctl --user show robot-server-sim -p MainPID --value)
NR=$(systemctl --user show robot-server-sim -p NRestarts --value)
echo "tras el crash: PID $PID2, NRestarts=$NR, sirve HTTP $(curl -s -o /dev/null -w '%{http_code}' http://localhost:8080/)"

systemctl --user stop robot-server-sim.service
rm -f "$UNIT"; systemctl --user daemon-reload

[ "$PID1" != "$PID2" ] && [ "$PID2" != "0" ] && echo "OK: systemd reinicio el servidor solo (PID nuevo)" || { echo "FALLO"; exit 1; }
