#!/bin/bash
#
# Proyecto I - Robot Aspiradora Autonomo
# CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
#
# Graba robot-image en una microSD y verifica que quedo identica a la imagen.
#
#   ./scripts/grabar-sd.sh                 # busca la tarjeta y pregunta cual es
#   ./scripts/grabar-sd.sh /dev/mmcblk0    # va directo a ese dispositivo
#   IMG=otra.wic.bz2 ./scripts/grabar-sd.sh
#
# Escribir en el disco equivocado destruye el sistema del host, asi que el
# script solo acepta dispositivos extraibles de menos de 128 GB, nunca uno con
# particiones montadas en / o /boot, y pide confirmacion escrita antes de tocar
# nada. Aun asi: lea el resumen que imprime antes de contestar.
#
set -euo pipefail

DEPLOY="${DEPLOY:-$HOME/yocto/poky/build/tmp/deploy/images/raspberrypi4-64}"
IMG="${IMG:-$DEPLOY/robot-image-raspberrypi4-64.rootfs.wic.bz2}"

# El script se vuelve a lanzar con sudo despues de que el usuario confirma; la
# segunda vuelta llega con esta bandera y ya no vuelve a preguntar.
CONFIRMADO=0
if [ "${1:-}" = "--confirmado" ]; then
    CONFIRMADO=1
    shift
fi

rojo()  { printf '\033[31m%s\033[0m\n' "$*"; }
verde() { printf '\033[32m%s\033[0m\n' "$*"; }
abortar() { rojo "ABORTA: $*"; exit 1; }

# ── La imagen ────────────────────────────────────────────────────────────────
[ -f "$IMG" ] || abortar "no encuentro la imagen: $IMG
Compilela primero:  cd ~/yocto/poky && source oe-init-build-env build && bitbake robot-image"

# El .wic.bz2 del deploy es un enlace a la ultima imagen compilada.
IMG_REAL=$(readlink -f "$IMG")

# ── El dispositivo ───────────────────────────────────────────────────────────
if [ $# -ge 1 ]; then
    DEV="$1"
else
    # Candidatos: discos extraibles o de lector de tarjetas, sin particion de sistema.
    mapfile -t CAND < <(lsblk -dno NAME,RM,TRAN,SIZE | awk '$2==1 || $3=="mmc" {print "/dev/"$1}')
    [ ${#CAND[@]} -gt 0 ] || abortar "no veo ninguna tarjeta conectada. Conectela y reintente, o pase el dispositivo como argumento."
    if [ ${#CAND[@]} -eq 1 ]; then
        DEV="${CAND[0]}"
    else
        echo "Hay varias tarjetas conectadas:"
        lsblk -dno NAME,SIZE,TRAN,MODEL "${CAND[@]}" | sed 's/^/  /'
        abortar "pase el dispositivo como argumento, por ejemplo: $0 ${CAND[0]}"
    fi
fi

[ -b "$DEV" ] || abortar "$DEV no es un dispositivo de bloques"
if [[ "$DEV" =~ p[0-9]+$ || "$DEV" =~ ^/dev/sd[a-z][0-9]+$ ]]; then
    abortar "$DEV parece una particion. Se graba el disco completo, por ejemplo /dev/mmcblk0"
fi

NOMBRE=$(basename "$DEV")
TAM_BYTES=$(( $(cat "/sys/block/$NOMBRE/size") * 512 ))
TAM_LEGIBLE=$(lsblk -dno SIZE "$DEV" | xargs)
EXTRAIBLE=$(cat "/sys/block/$NOMBRE/removable" 2>/dev/null || echo 0)
TRANSPORTE=$(lsblk -dno TRAN "$DEV")

# Nunca el disco del sistema.
for punto in / /boot /boot/efi /home; do
    origen=$(findmnt -no SOURCE "$punto" 2>/dev/null || true)
    case "$origen" in
        "$DEV"*) abortar "$DEV tiene montado $punto. Es el disco del sistema." ;;
    esac
done
[ "$EXTRAIBLE" = "1" ] || [ "$TRANSPORTE" = "mmc" ] || \
    abortar "$DEV no es extraible ni viene de un lector de tarjetas"
[ "$TAM_BYTES" -lt 137438953472 ] || abortar "$DEV mide $TAM_LEGIBLE: demasiado grande para ser la microSD"

# ── Confirmacion ─────────────────────────────────────────────────────────────
if [ "$CONFIRMADO" -eq 0 ]; then
echo
echo "  Imagen:      $(basename "$IMG_REAL")"
echo "               $(date -r "$IMG_REAL" '+%Y-%m-%d %H:%M')  ·  $(du -h "$IMG_REAL" | cut -f1) comprimida"
MODELO=$(lsblk -dno MODEL "$DEV" | xargs)
echo "  Destino:     $DEV  ($TAM_LEGIBLE${MODELO:+, $MODELO})"
echo "  Contenido actual:"
lsblk -no NAME,SIZE,FSTYPE,LABEL "$DEV" | sed 's/^/               /'
echo
rojo "Todo lo que haya en $DEV se borra y no se puede recuperar."
read -r -p "Escriba 'grabar' para continuar: " respuesta
[ "$respuesta" = "grabar" ] || abortar "cancelado por el usuario"
fi

# ── A partir de aqui hace falta root ─────────────────────────────────────────
if [ "$(id -u)" -ne 0 ]; then
    echo "Se necesita sudo para escribir en $DEV"
    exec sudo -E "$0" --confirmado "$DEV"
fi

# ── Desmontar ────────────────────────────────────────────────────────────────
for part in $(lsblk -lno NAME "$DEV" | tail -n +2); do
    if findmnt -S "/dev/$part" >/dev/null 2>&1; then
        echo "Desmontando /dev/$part"
        umount "/dev/$part" || abortar "no pude desmontar /dev/$part. Cierre la ventana de Archivos (nautilus -q) y reintente."
    fi
done

# ── Grabar ───────────────────────────────────────────────────────────────────
echo
echo "== Grabando"
bzcat "$IMG_REAL" | dd of="$DEV" bs=4M iflag=fullblock oflag=direct conv=fsync status=progress
sync

# ── Verificar ────────────────────────────────────────────────────────────────
echo
echo "== Verificando byte a byte"
N=$(bzcat "$IMG_REAL" | wc -c)
if cmp -n "$N" <(bzcat "$IMG_REAL") "$DEV"; then
    verde "VERIFICACION OK: $N bytes identicos"
else
    abortar "la tarjeta no coincide con la imagen. No la use: repita el grabado o cambie de tarjeta."
fi

blockdev --rereadpt "$DEV" 2>/dev/null || true
sleep 2
echo
lsblk -o NAME,SIZE,FSTYPE,LABEL "$DEV"
verde "LISTO: ya puede sacar la tarjeta"
