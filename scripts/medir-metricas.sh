#!/bin/bash
#
# Proyecto I - Robot Aspiradora Autonomo
# CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
#
# Mide las metricas de eficiencia que pide el enunciado sobre la imagen en
# ejecucion: rootfs, tiempo de arranque, RAM y CPU en operacion normal
# (navegacion autonoma + audio + servidor web a la vez). Imprime una tabla en
# Markdown para pegar en docs/metricas.md.
#
#   ./scripts/medir-metricas.sh 192.168.1.50              # la Raspberry Pi
#   SSH_PORT=2222 ./scripts/medir-metricas.sh localhost   # la imagen en QEMU
#   DUR=120 ./scripts/medir-metricas.sh 192.168.1.50      # ventana de 2 min
#
# Se corre desde el host, con el robot RECIEN ARRANCADO: el tiempo de arranque
# sale de los relojes de systemd y del journal de este arranque.
#
# El script pone el robot en modo autonomo: se va a mover. Dejelo en el piso,
# en un area despejada, antes de confirmar. Al terminar lo deja en modo manual,
# detenido y con el audio en silencio.
#
# En el target solo usa lo que ya trae la imagen (BusyBox, systemctl,
# journalctl y /proc): no hace falta agregar systemd-analyze ni ningun paquete.
#
set -euo pipefail

HOST="${1:-}"
SSH_PORT="${SSH_PORT:-22}"
HTTP_PORT="${HTTP_PORT:-8080}"
DUR="${DUR:-60}"
USUARIO="${USUARIO:-user1}"
CLAVE="${CLAVE:-password1}"

if [ -z "$HOST" ]; then
    sed -n '3,23p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
fi
for cmd in curl ssh; do
    command -v "$cmd" >/dev/null || { echo "Falta $cmd en el host." >&2; exit 1; }
done

URL="http://$HOST:$HTTP_PORT"
CK="$(mktemp)"
SONDEO=""

api() { curl -s -m 5 -b "$CK" -H 'Content-Type: application/json' "$@"; }

login() {
    curl -s -m 5 -c "$CK" -o /dev/null -w '%{http_code}' -X POST "$URL/api/login" \
        -H 'Content-Type: application/json' \
        -d "{\"username\":\"$USUARIO\",\"password\":\"$CLAVE\"}"
}

# Manda una orden y la reintenta hasta que el servidor conteste 200. Si la
# sesion ya no vale (401), vuelve a iniciarla.
orden() {
    local ruta="$1" cuerpo="$2" codigo=000
    for _ in 1 2 3 4 5; do
        codigo="$(api -o /dev/null -w '%{http_code}' -X POST "$URL$ruta" -d "$cuerpo" || true)"
        [ "$codigo" = "200" ] && return 0
        [ "$codigo" = "401" ] && login >/dev/null || true
        sleep 1
    done
    echo "AVISO: $ruta $cuerpo no se aplico (ultimo HTTP $codigo)." >&2
    return 1
}

# Pase lo que pase, el robot queda detenido. Se comprueba al final: un robot
# que sigue navegando despues de medir es peligroso.
limpiar() {
    [ -n "$SONDEO" ] && kill "$SONDEO" 2>/dev/null || true
    wait 2>/dev/null || true
    orden /api/audio/control '{"action":"stop"}'   || true
    orden /api/mode          '{"mode":"manual"}'   || true
    orden /api/move          '{"direction":"stop"}' || true
    estado="$(api "$URL/api/status" 2>/dev/null || true)"
    case "$estado" in
        *'"mode":"manual"'*'"status":0'*) echo "Robot en modo manual, detenido y sin musica." >&2 ;;
        *) echo "AVISO: no se pudo confirmar que el robot quedara detenido. Detengalo desde el panel." >&2 ;;
    esac
    rm -f "$CK"
}
trap limpiar EXIT

echo "El robot en $HOST va a navegar en modo autonomo durante $DUR s." >&2
read -r -p "Escriba SI para continuar: " resp
[ "$resp" = "SI" ] || { echo "Cancelado." >&2; exit 1; }

# ── Escenario de operacion normal ────────────────────────────────────────────
codigo="$(login)"
[ "$codigo" = "200" ] || { echo "El login en $URL fallo (HTTP $codigo)." >&2; exit 1; }

api -X POST "$URL/api/mode"          -d '{"mode":"autonomous"}'           >/dev/null
api -X POST "$URL/api/audio/control" -d '{"action":"play","track_id":1}'  >/dev/null

# El panel web consulta /api/status cada 500 ms: se reproduce esa carga.
( while :; do api -o /dev/null "$URL/api/status" || true; sleep 0.5; done ) &
SONDEO=$!

echo "Midiendo..." >&2
echo
echo "### Medicion del $(date '+%Y-%m-%d %H:%M') sobre \`$HOST\`"
echo

# ── Lo que corre en el target ────────────────────────────────────────────────
ssh -p "$SSH_PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o LogLevel=ERROR "root@$HOST" "DUR=$DUR sh -s" <<'TARGET'
set -eu

pid_de()  { systemctl show "$1" -p MainPID --value 2>/dev/null || echo 0; }
# utime + stime del proceso en ticks de reloj. USER_HZ es 100 en Linux: un
# tick por segundo es el 1 % de un nucleo.
ticks()   { [ -r "/proc/$1/stat" ] && sed 's/.*) //' "/proc/$1/stat" | awk '{print $12 + $13}' || echo 0; }
rss_mb()  { awk '/^VmRSS:/ {printf "%.1f", $2 / 1024}' "/proc/$1/status" 2>/dev/null || true; }
# Total e inactivo (idle + iowait) de la primera linea de /proc/stat.
cpu()     { awk 'NR == 1 {t = 0; for (i = 2; i <= 9; i++) t += $i; print t, $5 + $6}' /proc/stat; }
us_a_s()  { awk -v u="${1:-0}" 'BEGIN {printf "%.1f", u / 1000000}'; }
fila()    { printf '| %s | %s | %s |\n' "$1" "$2" "$3"; }

modelo='sin device-tree'
[ -r /proc/device-tree/model ] && modelo="$(tr -d '\0' < /proc/device-tree/model)"
nucleos="$(grep -c '^processor' /proc/cpuinfo)"

echo '| Metrica | Valor | Metodo |'
echo '|---|---|---|'
fila 'Plataforma' "$modelo, $nucleos nucleos, kernel $(uname -r)" '`/proc/device-tree/model`, `uname -r`'

# ── Sistema de archivos ──────────────────────────────────────────────────────
for punto in / /boot /opt/robot/audio/canciones; do
    grep -q " $punto " /proc/mounts || continue
    set -- $(df -k "$punto" | tail -n 1 | awk '{print $(NF-4), $(NF-3)}')
    fila "Particion \`$punto\`" \
         "$(awk -v u="$2" -v t="$1" 'BEGIN {printf "%.0f MB usados de %.0f MB", u / 1024, t / 1024}')" \
         "\`df -k $punto\`"
done

# ── Arranque ─────────────────────────────────────────────────────────────────
# Relojes monotonicos de systemd: microsegundos desde que arranco el kernel.
# No incluyen el firmware de la Raspberry Pi, que corre antes.
t_kernel="$(systemctl show -p UserspaceTimestampMonotonic --value)"
t_fin="$(systemctl show -p FinishTimestampMonotonic --value)"
t_srv="$(systemctl show robot-server -p ActiveEnterTimestampMonotonic --value)"
reinicios="$(systemctl show robot-server -p NRestarts --value)"
# Primera vez en este arranque que el servidor aviso que ya escucha.
t_http="$(journalctl -u robot-server -b -o short-monotonic --no-pager 2>/dev/null \
          | sed -n 's/^\[ *\([0-9.]*\)\].*Servidor escuchando.*/\1/p' | head -n 1)"

fila 'Arranque: kernel' "$(us_a_s "$t_kernel") s" '`UserspaceTimestampMonotonic`'
fila 'Arranque: `robot-server` activo' "$(us_a_s "$t_srv") s" '`ActiveEnterTimestampMonotonic` de la unidad'
escucha='sin dato'
[ -n "$t_http" ] && escucha="$(awk -v t="$t_http" 'BEGIN {printf "%.1f", t}') s"
fila 'Arranque: servidor escuchando' "$escucha" '`journalctl -o short-monotonic`, primera linea "Servidor escuchando"'
fila 'Arranque: sistema completo' "$(us_a_s "$t_fin") s" '`FinishTimestampMonotonic`'
[ "$reinicios" = "0" ] || echo "> AVISO: robot-server se reinicio $reinicios veces en este arranque; el tiempo de la unidad no es el del arranque. Use la fila del journal." >&2

# ── CPU: dos muestras separadas por DUR segundos ─────────────────────────────
p_srv="$(pid_de robot-server)"
p_pig="$(pid_de pigpiod)"
set -- $(cpu); tot0="$1"; idle0="$2"
s0="$(ticks "$p_srv")"; g0="$(ticks "$p_pig")"
sleep "$DUR"
set -- $(cpu); tot1="$1"; idle1="$2"
s1="$(ticks "$p_srv")"; g1="$(ticks "$p_pig")"

fila "CPU del sistema ($DUR s)" \
     "$(awk -v t0="$tot0" -v t1="$tot1" -v i0="$idle0" -v i1="$idle1" \
        'BEGIN {d = t1 - t0; if (d <= 0) d = 1; printf "%.1f %% de los nucleos", 100 * (d - (i1 - i0)) / d}')" \
     'diferencia de `/proc/stat`'
# El tiempo transcurrido sale de los mismos ticks del sistema (total / nucleos),
# no del valor nominal de DUR: el sleep siempre dura un poco mas.
de_un_nucleo() {
    awk -v a="$1" -v b="$2" -v n="$nucleos" -v d="$((tot1 - tot0))" \
        'BEGIN {if (d <= 0) d = 1; printf "%.1f %% de un nucleo", 100 * (b - a) * n / d}'
}
fila 'CPU de `robot-server`' "$(de_un_nucleo "$s0" "$s1")" \
     'diferencia de `utime + stime` en `/proc/PID/stat`'
[ "$p_pig" != "0" ] && fila 'CPU de `pigpiod`' "$(de_un_nucleo "$g0" "$g1")" 'idem'
fila 'Carga promedio (1 min)' "$(cut -d ' ' -f 1 /proc/loadavg)" '`/proc/loadavg`'

# ── RAM, al final de la ventana ──────────────────────────────────────────────
fila 'RAM en uso' \
     "$(awk '/^MemTotal:/ {t = $2} /^MemAvailable:/ {a = $2} END {printf "%.0f MB de %.0f MB", (t - a) / 1024, t / 1024}' /proc/meminfo)" \
     '`MemTotal - MemAvailable` de `/proc/meminfo`'
rss="$(rss_mb "$p_srv")"
[ -n "$rss" ] && rss="$rss MB" || rss='sin dato'
fila 'RAM de `robot-server` (RSS)' "$rss" '`VmRSS` de `/proc/PID/status`'
[ "$p_pig" != "0" ] && fila 'RAM de `pigpiod` (RSS)' "$(rss_mb "$p_pig") MB" 'idem'

if [ -r /sys/class/thermal/thermal_zone0/temp ]; then
    fila 'Temperatura del SoC' \
         "$(awk '{printf "%.1f C", $1 / 1000}' /sys/class/thermal/thermal_zone0/temp)" \
         '`/sys/class/thermal/thermal_zone0/temp`'
fi
exit 0
TARGET

echo
echo "Listo. Pegue la tabla en docs/metricas.md y en el README." >&2
