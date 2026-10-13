#!/bin/sh
# Proyecto I - Robot Aspiradora Autonomo — CE-1113, TEC
#
# Ejercita por HTTP los endpoints del servidor corriendo sobre el simulador
# (issues #25-#29), sin la Raspberry. Lanza el servidor, hace login y prueba
# autenticacion, panel, modos, movimiento, audio, radar, velocidad del MPU,
# tiempo de choque y el mapa incremental.
#
# Uso:  ./construir_servidor.sh && ./prueba_api.sh
# Cualquier comando que falle detiene la prueba.
set -e
# Carpeta de este script, en ruta absoluta.
SIM="$(cd "$(dirname "$0")" && pwd)"
# Direccion del servidor bajo prueba.
URL=http://localhost:8080
# Credenciales del usuario de prueba.
USER=user1; PASS=password1

# Sin el binario del servidor no hay nada que probar.
[ -x "$SIM/robot-server-sim" ] || { echo "Compila primero: ./construir_servidor.sh"; exit 1; }
# Carpeta de trabajo, con enlaces a la interfaz web y al audio del
# repositorio.
mkdir -p "$SIM/run" && ( cd "$SIM/run" && ln -sfn ../../server/www www && ln -sfn ../../audio audio )

# Lanza el servidor en segundo plano, con su salida en run/srv.log, y
# guarda su PID.
( cd "$SIM/run" && "$SIM/robot-server-sim" > srv.log 2>&1 ) & SRV=$!
# Pase lo que pase, al salir se detiene el servidor.
trap 'kill $SRV 2>/dev/null' EXIT
# Espera a que el servidor abra el puerto en vez de dormir un tiempo fijo:
# antes de escuchar, el servidor calibra el MPU y lee las pistas de audio, y
# sobre un disco lento (p. ej. /mnt/c en WSL) eso pasa de los 3 s.
# Hasta 15 intentos, uno por segundo.
i=0
until curl -s -o /dev/null $URL/; do
    i=$((i + 1))
    [ $i -le 15 ] || { echo "El servidor no respondio en 15 s; ver $SIM/run/srv.log"; exit 1; }
    sleep 1
done
# Archivo temporal para la cookie de sesion.
CK=$(mktemp)

echo "== Autenticacion (#25) =="
# La raiz es publica: se espera 200.
echo "  GET /                 -> HTTP $(curl -s -o /dev/null -w '%{http_code}' $URL/)"
# Clave incorrecta: se espera 401.
echo "  login mal password    -> HTTP $(curl -s -o /dev/null -w '%{http_code}' -X POST $URL/api/login -d '{"username":"'$USER'","password":"x"}')"
# Sin cookie de sesion: se espera 401.
echo "  status sin sesion     -> HTTP $(curl -s -o /dev/null -w '%{http_code}' $URL/api/status)"
# Credenciales correctas: se espera 200, y la cookie queda en el archivo.
echo "  login correcto        -> HTTP $(curl -s -c $CK -o /dev/null -w '%{http_code}' -X POST $URL/api/login -H 'Content-Type: application/json' -d '{"username":"'$USER'","password":"'$PASS'"}')"

echo "== Panel / modo / movimiento (#26) =="
# Estado con la sesion abierta: modo, sensores y LEDs.
curl -s -b $CK $URL/api/status | python3 -c "import sys,json;d=json.load(sys.stdin);print('  status:',d['mode'],'| sensores',d['sensors'],'| leds',d['leds'])"
# Radar, velocidad, tiempo de choque y origen de la velocidad.
curl -s -b $CK $URL/api/status | python3 -c "import sys,json;d=json.load(sys.stdin);r=d['radar'];m=d['movimiento'];print('  radar: servo',r['angulo'],'|',' '.join(f\"{l['a']}:{l['d']:.0f}\" for l in r['lecturas']),'| velocidad',m['velocidad'],'cm/s | choque en',m['ttc'],'s | imu',m['imu'])"
# Pasa a modo manual y manda un avance.
echo "  mode->manual  $(curl -s -b $CK -X POST $URL/api/mode -d '{"mode":"manual"}')"
echo "  move forward  $(curl -s -b $CK -X POST $URL/api/move -d '{"direction":"forward","speed":70}')"

echo "== Audio (#27) =="
# Lista de pistas, volumen y detener.
echo "  list    $(curl -s -b $CK $URL/api/audio/list)"
echo "  volume  $(curl -s -b $CK -X POST $URL/api/audio/volume -d '{"volume":55}')"
echo "  stop    $(curl -s -b $CK -X POST $URL/api/audio/control -d '{"action":"stop"}')"

echo "== Mapa incremental (#28/#29) =="
# En modo autonomo, cuatro lecturas del mapa separadas por 2 s: las celdas
# visitadas tienen que crecer.
curl -s -b $CK -X POST $URL/api/mode -d '{"mode":"autonomous"}' >/dev/null
for i in 0 1 2 3; do
  [ $i -gt 0 ] && sleep 2
  curl -s -b $CK $URL/api/status | python3 -c "import sys,json;m=json.load(sys.stdin)['map'];print(f'  celda={m[\"celda_cm\"]} cm robot=({m[\"robot_x\"]},{m[\"robot_y\"]}) rumbo={m[\"robot_heading\"]} visitadas={m[\"visited\"]} obstaculos={m[\"obstacles\"]}')"
done
# Borra la cookie y da la prueba por terminada.
rm -f $CK
echo "OK"
