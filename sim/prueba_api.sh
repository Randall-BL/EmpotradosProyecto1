#!/bin/sh
# Proyecto I - Robot Aspiradora Autonomo — CE-1113, TEC
#
# Ejercita por HTTP los endpoints del servidor corriendo sobre el simulador
# (issues #25-#29), sin la Raspberry. Lanza el servidor, hace login y prueba
# autenticacion, panel, modos, movimiento, audio y el mapa incremental.
#
# Uso:  ./construir_servidor.sh && ./prueba_api.sh
set -e
SIM="$(cd "$(dirname "$0")" && pwd)"
URL=http://localhost:8080
USER=user1; PASS=password1

[ -x "$SIM/robot-server-sim" ] || { echo "Compila primero: ./construir_servidor.sh"; exit 1; }
mkdir -p "$SIM/run" && ( cd "$SIM/run" && ln -sfn ../../server/www www && ln -sfn ../../audio audio )

( cd "$SIM/run" && "$SIM/robot-server-sim" > srv.log 2>&1 ) & SRV=$!
trap 'kill $SRV 2>/dev/null' EXIT
sleep 3
CK=$(mktemp)

echo "== Autenticacion (#25) =="
echo "  GET /                 -> HTTP $(curl -s -o /dev/null -w '%{http_code}' $URL/)"
echo "  login mal password    -> HTTP $(curl -s -o /dev/null -w '%{http_code}' -X POST $URL/api/login -d '{"username":"'$USER'","password":"x"}')"
echo "  status sin sesion     -> HTTP $(curl -s -o /dev/null -w '%{http_code}' $URL/api/status)"
echo "  login correcto        -> HTTP $(curl -s -c $CK -o /dev/null -w '%{http_code}' -X POST $URL/api/login -H 'Content-Type: application/json' -d '{"username":"'$USER'","password":"'$PASS'"}')"

echo "== Panel / modo / movimiento (#26) =="
curl -s -b $CK $URL/api/status | python3 -c "import sys,json;d=json.load(sys.stdin);print('  status:',d['mode'],'| sensores',d['sensors'],'| leds',d['leds'])"
echo "  mode->manual  $(curl -s -b $CK -X POST $URL/api/mode -d '{"mode":"manual"}')"
echo "  move forward  $(curl -s -b $CK -X POST $URL/api/move -d '{"direction":"forward","speed":70}')"

echo "== Audio (#27) =="
echo "  list    $(curl -s -b $CK $URL/api/audio/list)"
echo "  volume  $(curl -s -b $CK -X POST $URL/api/audio/volume -d '{"volume":55}')"
echo "  stop    $(curl -s -b $CK -X POST $URL/api/audio/control -d '{"action":"stop"}')"

echo "== Mapa incremental (#28/#29) =="
curl -s -b $CK -X POST $URL/api/mode -d '{"mode":"autonomous"}' >/dev/null
for i in 0 1 2 3; do
  [ $i -gt 0 ] && sleep 2
  curl -s -b $CK $URL/api/status | python3 -c "import sys,json;m=json.load(sys.stdin)['map'];print(f'  robot=({m[\"robot_x\"]},{m[\"robot_y\"]}) rumbo={m[\"robot_heading\"]} visitadas={m[\"visited\"]} obstaculos={m[\"obstacles\"]}')"
done
rm -f $CK
echo "OK"
