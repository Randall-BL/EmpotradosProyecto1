# -*- coding: utf-8 -*-
"""
Carcasa tipo Roomba en dos piezas (inferior + superior) y sus tres ruedas, paramétrica, para impresión 3D.
Versión simple y liviana: solo los espacios de cada componente, sin tornillos.
  - Las dos piezas se unen a presión con 4 pestañas de clic.
  - Dos ruedas motrices que entran a presión en el eje en D del motor.
  - Tercera rueda de apoyo: bola de 16 mm en un asiento de la pieza inferior, sujeta por un aro a presión.
Requiere CadQuery (pip install cadquery).  Uso:  python carcasa_roomba.py

Ejes: X = izquierda/derecha, Y = atrás(-)/frente(+), Z = arriba.  Z = 0 es la cara de abajo del piso.
Todas las medidas en mm.
"""
import os
import sys
from math import pi, sqrt
import cadquery as cq

OUT = os.path.dirname(os.path.abspath(__file__))

# ===================== PARÁMETROS =====================
# --- carcasa (espesores bajos para que pese poco) ---
D = 215.0            # diámetro exterior (la cama de la impresora mide 249 x 219 mm)
R = D / 2
WALL = 1.6           # espesor de pared de la pieza superior
FLOOR = 2.0          # espesor del piso
LID = 1.6            # espesor de la tapa
H = 45.0             # altura total de la carcasa (sin contar la separación al suelo)
FIT = 0.2            # holgura entre las dos piezas
BW_T = 1.6           # espesor de la pared interna de la pieza inferior
BW_TOP = 18.0        # hasta qué altura sube esa pared

# --- cierre a presión (clic) ---
TAB_ANGLES = (45, 135, 225, 315)   # dónde van las 4 pestañas
TAB_L, TAB_H = 20.0, 6.0           # largo y alto de cada pestaña flexible
TAB_Z = 11.0                       # altura del centro de la pestaña
BUMP_OUT = 1.0                     # cuánto sobresale el botón de clic
KEY_ANGLE = 90                     # guía al frente: la tapa solo entra en una posición
NOTCH_ANGLES = (0, 180)            # muescas para los dedos (para abrir)

# --- motores: micro motorreductor AD12672. No llevan soporte: se apoyan sobre el piso, junto a la ranura ---
MOTOR_L, MOTOR_W, MOTOR_H = 26.0, 12.0, 10.0   # cuerpo: largo (a lo largo del eje), ancho, alto
SHAFT_D, SHAFT_FLAT, SHAFT_L = 3.0, 2.5, 10.0  # eje en D: diámetro, medida sobre el plano, largo
MOTOR_X = 85.5       # posición en X (+/-) de la cara del motor de donde sale el eje (referencia)
WHEEL_Y = 12.0       # eje de las ruedas en Y (adelantado: la rueda de apoyo va atrás)

# --- ruedas motrices (impresas) ---
WHEEL_D = 30.0       # diámetro
WHEEL_W = 8.0        # ancho de la banda de rodadura
HUB_D = 9.0          # diámetro del cubo
HUB_L = 10.0         # largo del cubo (igual al eje)
BORE_FIT = 0.1       # holgura del agujero en D (entra a presión)
WHEEL_GAP = 3.0      # holgura adelante y atrás de la rueda en la ranura del piso

# --- rueda de apoyo: bola de 16 mm (canica o impresa) ---
BALL_D = 16.0
CASTER_Y = -84.0
RING_H = 4.0         # alto del aro que sujeta la bola por debajo
RING_R = 18.0        # radio exterior del aro
PIN_R_POS = 14.5     # distancia de los dos pines del aro al centro
PIN_D = 4.1          # diámetro de los pines (partidos, entran a presión)
PIN_H = 6.0

# --- ventilador de succión 30 x 30 x 12 mm (AD32862) ---
FAN = 30.0
FAN_T = 12.0
FAN_Y = 84.5
FAN_INTAKE_D = 28.0
FAN_FRAME_H = 8.0

# --- variante "succión" de la pieza inferior: campana sobre una boca ancha + boquilla cerca del suelo ---
HOOD_HALF_W = 36.0             # medio ancho interior de la campana en el piso
HOOD_Y0, HOOD_Y1 = 67.0, 96.0  # de dónde a dónde va la campana en el piso (interior)
HOOD_H = 22.0                  # altura de la campana; el ventilador va encima
MOUTH_DEPTH = 12.0             # fondo de la boca de succión (ranura en el piso)
LIP_H = 4.0                    # reborde detrás de la boca, para que el polvo no se devuelva
NOZZLE_GAP = 2.0               # distancia de la boquilla al suelo
NOZZLE_PIN_X = 43.0            # posición de los dos pines de la boquilla
RPI_SHIFT_SUCCION = -4.0       # en esta variante la Raspberry va 4 mm más atrás, para dejar sitio a los USB

# --- Raspberry Pi 4 (85 x 56 mm, agujeros de 2.7 mm en rectángulo 58 x 49) ---
# Va a la derecha, a lo largo: SD hacia atrás, USB/Ethernet hacia el frente, GPIO hacia la placa perforada
RPI_X_PORTS = 59.0   # borde de USB-C/HDMI (hacia la derecha, +X)
RPI_Y_SD = -48.0     # borde de la tarjeta SD (hacia atrás)
RPI_STANDOFF_H = 6.0

# --- placa perforada 76 x 125 mm ---
# Va a la izquierda, a lo largo. Queda elevada: el motor izquierdo pasa por debajo
PB_X, PB_Y, PB_T = 76.0, 125.0, 1.6
PB_CX, PB_CY = -38.7, 0.0   # centro de la placa
PB_LEDGE_H = 11.0    # altura a la que queda apoyada la placa sobre el piso
PB_RIM = 2.0         # cuánto sobresale cada esquina de apoyo por fuera de la placa

# --- botón de encendido (pulsador de panel) ---
BUTTON_D = 16.2      # diámetro del agujero (botón de 16 mm)
BUTTON_POS = (-45.0, 80.0)

# ===================== derivados =====================
AXLE_Z = FLOOR + MOTOR_H / 2                 # eje del motor, con el motor apoyado sobre el piso
CLEAR = WHEEL_D / 2 - AXLE_Z                 # separación entre el piso de la carcasa y el suelo
BALL_ZC = BALL_D / 2 - CLEAR                 # altura del centro de la bola
R_IN = R - WALL                              # radio interior de la pieza superior
R_BW = R_IN - FIT                            # radio exterior de la pared de la pieza inferior
MOUTH_Y1 = HOOD_Y1 - 1.0                     # la boca queda al frente de la campana
MOUTH_Y0 = MOUTH_Y1 - MOUTH_DEPTH
MOUTH_YC = (MOUTH_Y0 + MOUTH_Y1) / 2


def rpi_y_sd(succion):
    return RPI_Y_SD + (RPI_SHIFT_SUCCION if succion else 0.0)


def rpi_holes(succion):
    return [(RPI_X_PORTS - v, rpi_y_sd(succion) + u) for u in (3.5, 61.5) for v in (3.5, 52.5)]
BUMP_R = 2.4                                 # radio de la base del botón de clic
BUMP_Y = TAB_L / 2 - 4.0                     # posición del botón a lo largo de la pestaña
CONE_K = BALL_D / 2 * sqrt(2)                # cono a 45° tangente a la bola: r = CONE_K - (z - BALL_ZC)
SLOT_X0, SLOT_X1 = MOTOR_X + 1.0, MOTOR_X + SHAFT_L + 2.0   # ranura de cada rueda motriz


def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))


def cyl(x, y, z0, z1, r):
    return cq.Workplane("XY").circle(r).extrude(z1 - z0).translate((x, y, z0))


def rot(obj, angle):
    """Gira alrededor del eje Z una forma construida sobre el eje +X."""
    return obj.rotate((0, 0, 0), (0, 0, 1), angle)


def revolucion(puntos):
    """Sólido de revolución alrededor del eje Z a partir de un perfil (r, z)."""
    return cq.Workplane("XZ").polyline(puntos).close().revolve(360, (0, 0, 0), (0, 1, 0))


# ===================== PIEZA INFERIOR =====================
def _tronco(w0, d0, w1, d1, h):
    """Tronco de pirámide: rectángulo w0 x d0 abajo, w1 x d1 arriba (corrido hacia el ventilador)."""
    yb = (HOOD_Y0 + HOOD_Y1) / 2
    t = cq.Workplane("XY").rect(w0, d0).workplane(offset=h).center(0, FAN_Y - yb).rect(w1, d1).loft(combine=True)
    return t.translate((0, yb, FLOOR))


def pieza_inferior(succion=False):
    # piso + pared interna (la pieza superior baja por fuera de esta pared y se apoya en el borde del piso)
    p = cq.Workplane("XY").circle(R).extrude(FLOOR)
    p = p.edges("<Z").chamfer(1.0)
    pared = cq.Workplane("XY").circle(R_BW).circle(R_BW - BW_T).extrude(BW_TOP - FLOOR).translate((0, 0, FLOOR))
    pared = pared.faces(">Z").edges("%CIRCLE").edges(cq.selectors.RadiusNthSelector(1)).chamfer(0.6)
    p = p.union(pared)

    # rueda de apoyo: asiento cónico de la bola (parte del piso) y dos casquillos para los pines del aro
    r0 = CONE_K - (FLOOR - BALL_ZC)                 # radio interior del cono a la altura del piso
    z_tip = BALL_ZC + CONE_K - 1.5                  # donde el cono se cierra (queda un plano de 3 mm)
    asiento = revolucion([(r0, FLOOR - 0.1), (1.5, z_tip), (0, z_tip), (0, z_tip + 1.6),
                          (1.5 + 0.66, z_tip + 1.6), (r0 + 2.26, FLOOR - 0.1)]).translate((0, CASTER_Y, 0))
    p = p.union(asiento)
    for sg in (1, -1):
        p = p.union(cyl(sg * PIN_R_POS, CASTER_Y, FLOOR, FLOOR + PIN_H, 4.0))

    # ventilador: marco donde entra a presión
    a = FAN / 2 + 0.15
    z_fan = FLOOR + (HOOD_H if succion else 0.0)
    marco = box(-a - 1.6, a + 1.6, FAN_Y - a - 1.6, FAN_Y + a + 1.6, z_fan, z_fan + FAN_FRAME_H)
    marco = marco.cut(box(-a, a, FAN_Y - a, FAN_Y + a, z_fan, z_fan + FAN_FRAME_H + 1))
    marco = marco.cut(box(a - 6.5, a - 2.0, FAN_Y - a - 3, FAN_Y - a + 1, z_fan, z_fan + FAN_FRAME_H + 1))  # paso del cable
    p = p.union(marco)
    if succion:
        # campana: del ancho de la boca en el piso se va cerrando a 45° hasta el ventilador
        w, d = 2 * HOOD_HALF_W, HOOD_Y1 - HOOD_Y0
        hueco_campana = _tronco(w, d, FAN - 2.0, FAN - 2.0, HOOD_H)
        campana = _tronco(w + 3.2, d + 3.2, 2 * a + 3.2, 2 * a + 3.2, HOOD_H).cut(hueco_campana)
        p = p.union(campana)
        # reborde detrás de la boca
        p = p.union(box(-HOOD_HALF_W, HOOD_HALF_W, MOUTH_Y0 - 1.6, MOUTH_Y0, FLOOR, FLOOR + LIP_H)
                    .intersect(hueco_campana))
        # casquillos para los pines de la boquilla
        for sg in (1, -1):
            p = p.union(cyl(sg * NOZZLE_PIN_X, MOUTH_YC, FLOOR, FLOOR + PIN_H, 4.0))

    # Raspberry Pi: 4 postes con espiga que entran en los agujeros de la placa
    for (x, y) in rpi_holes(succion):
        p = p.union(cyl(x, y, FLOOR, FLOOR + RPI_STANDOFF_H, 3.0))
        p = p.union(cyl(x, y, FLOOR + RPI_STANDOFF_H, FLOOR + RPI_STANDOFF_H + 3.0, 1.2)
                    .faces(">Z").chamfer(0.4))

    # placa perforada: 4 esquinas donde se apoya y queda encajada
    px, py = PB_X / 2 + 0.3, PB_Y / 2 + 0.3
    z_ledge = FLOOR + PB_LEDGE_H
    cuna = None
    for sx in (1, -1):
        for sy in (1, -1):
            xa, xb = sorted((PB_CX + sx * (px - 6), PB_CX + sx * (px + PB_RIM)))
            ya, yb = sorted((PB_CY + sy * (py - 6), PB_CY + sy * (py + PB_RIM)))
            b = box(xa, xb, ya, yb, FLOOR, z_ledge + PB_T + 1.5)
            cuna = b if cuna is None else cuna.union(b)
    cuna = cuna.cut(box(PB_CX - px, PB_CX + px, PB_CY - py, PB_CY + py, z_ledge, z_ledge + 20))
    p = p.union(cuna)

    # ----- cortes -----
    # ranuras de las ruedas motrices
    for sg in (1, -1):
        xa, xb = sorted((sg * SLOT_X0, sg * SLOT_X1))
        slot = (cq.Workplane("XY").rect(xb - xa, WHEEL_D + 2 * WHEEL_GAP).extrude(FLOOR + 2)
                .edges("|Z").fillet(2.0)
                .translate(((xa + xb) / 2, WHEEL_Y, -1)))
        p = p.cut(slot)
    # rueda de apoyo: paso de la bola por el piso y agujeros de los pines del aro
    p = p.cut(cyl(0, CASTER_Y, -1, FLOOR, r0))
    for sg in (1, -1):
        p = p.cut(cyl(sg * PIN_R_POS, CASTER_Y, -1, FLOOR + PIN_H + 1, PIN_D / 2 - 0.05))
    # entrada de aire del ventilador
    if succion:
        p = p.cut(box(-HOOD_HALF_W + 1, HOOD_HALF_W - 1, MOUTH_Y0, MOUTH_Y1, -1, FLOOR + 0.01))
        for sg in (1, -1):
            p = p.cut(cyl(sg * NOZZLE_PIN_X, MOUTH_YC, -1, FLOOR + PIN_H + 1, PIN_D / 2 - 0.05))
    else:
        p = p.cut(cyl(0, FAN_Y, -1, FLOOR + 1, FAN_INTAKE_D / 2))

    # cierre: agujeros donde hacen clic los botones de las pestañas
    for ang in TAB_ANGLES:
        hueco = (cq.Workplane("YZ").circle(BUMP_R - FIT + 0.1).extrude(BW_T + 2)
                 .translate((R_BW - BW_T - 1, BUMP_Y, TAB_Z)))
        p = p.cut(rot(hueco, ang))
    # cierre: ranura de la guía
    p = p.cut(rot(box(R_BW - BW_T - 1, R_BW + 1, -1.8, 1.8, FLOOR + 2.5, BW_TOP + 1), KEY_ANGLE))
    return p


# ===================== PIEZA SUPERIOR =====================
def pieza_superior():
    p = cq.Workplane("XY").circle(R).extrude(H - FLOOR).translate((0, 0, FLOOR))
    p = p.edges(">Z").chamfer(1.2)
    p = p.cut(cyl(0, 0, FLOOR - 1, H - LID, R_IN))

    # guía interna (entra en la ranura de la pieza inferior)
    p = p.union(rot(box(R_BW - BW_T + 0.2, R_IN + 0.3, -1.5, 1.5, FLOOR + 4.0, H - LID), KEY_ANGLE))

    for ang in TAB_ANGLES:
        # pestaña flexible: dos cortes horizontales y uno vertical en el extremo libre
        z0, z1 = TAB_Z - TAB_H / 2, TAB_Z + TAB_H / 2
        corte = box(R - 5, R + 1, -TAB_L / 2, TAB_L / 2 + 1.2, z0 - 1.2, z0)
        corte = corte.union(box(R - 5, R + 1, -TAB_L / 2, TAB_L / 2 + 1.2, z1, z1 + 1.2))
        corte = corte.union(box(R - 5, R + 1, TAB_L / 2, TAB_L / 2 + 1.2, z0 - 1.2, z1 + 1.2))
        p = p.cut(rot(corte, ang))
        # botón de clic en la cara interna de la pestaña
        x_face = sqrt(R_IN ** 2 - BUMP_Y ** 2)
        cono = cq.Workplane(obj=cq.Solid.makeCone(BUMP_R, BUMP_R - BUMP_OUT, BUMP_OUT + 0.3,
                                                  pnt=cq.Vector(x_face + 0.3, BUMP_Y, TAB_Z),
                                                  dir=cq.Vector(-1, 0, 0)))
        p = p.union(rot(cono, ang))

    # muescas para los dedos en el borde inferior
    for ang in NOTCH_ANGLES:
        p = p.cut(rot(box(R_IN - 0.05, R + 1, -9, 9, FLOOR - 1, FLOOR + 6.0), ang))

    # agujero del botón de encendido
    p = p.cut(cyl(BUTTON_POS[0], BUTTON_POS[1], H - LID - 1, H + 1, BUTTON_D / 2))
    # rejilla de salida de aire sobre el ventilador
    for i in range(-2, 3):
        p = p.cut(box(i * 5.5 - 1.5, i * 5.5 + 1.5, FAN_Y - 12, FAN_Y + 12, H - LID - 1, H + 1))
    return p


# ===================== RUEDAS =====================
def rueda_motriz():
    """Rueda con cubo para eje en D. Se modela como se imprime: cara exterior sobre la cama, cubo hacia arriba."""
    rw = WHEEL_D / 2
    w = cq.Workplane("XY").circle(rw).extrude(WHEEL_W)
    w = w.edges("%CIRCLE").chamfer(0.6)
    # canal en V para una liga o un O-ring (da agarre)
    zc = WHEEL_W / 2
    w = w.cut(revolucion([(rw + 0.5, zc - 1.6), (rw - 1.1, zc), (rw + 0.5, zc + 1.6)]))
    # rebaje para que pese menos
    w = w.cut(cq.Workplane("XY").circle(rw - 3.0).circle(HUB_D / 2 + 1.0).extrude(WHEEL_W).translate((0, 0, 3.0)))
    # cubo
    w = w.union(cyl(0, 0, 0, HUB_L, HUB_D / 2))
    # agujero en D, pasante
    rb = SHAFT_D / 2 + BORE_FIT
    flat = SHAFT_FLAT - SHAFT_D / 2 + BORE_FIT       # distancia del centro al plano
    bore = cyl(0, 0, -1, HUB_L + 1, rb).cut(box(flat, rb + 1, -rb - 1, rb + 1, -2, HUB_L + 2))
    return w.cut(bore)


def aro_bola():
    """Aro que sujeta la bola por debajo. Se modela como se imprime: cara de abajo sobre la cama, pines hacia arriba."""
    rb = BALL_D / 2
    r_top = rb + 0.45                                   # holgura con la bola a la altura del piso
    r_bot = sqrt(rb ** 2 - (RING_H + BALL_ZC) ** 2) + 0.37   # abertura por donde asoma la bola (menor que la bola)
    a = revolucion([(r_bot, 0), (RING_R - 1.5, 0), (RING_R, 1.5), (RING_R, RING_H), (r_top, RING_H)])
    for sg in (1, -1):
        pin = cyl(sg * PIN_R_POS, 0, RING_H - 0.1, RING_H + PIN_H, PIN_D / 2).faces(">Z").chamfer(0.5)
        pin = pin.cut(box(sg * PIN_R_POS - 0.5, sg * PIN_R_POS + 0.5, -PIN_D, PIN_D, RING_H + 1.2, RING_H + PIN_H + 1))
        a = a.union(pin)
    return a


def boquilla():
    """Boquilla de succión: va debajo de la boca y deja el aire entrar a NOZZLE_GAP mm del suelo.
    Se modela como se imprime: cara de abajo sobre la cama, pines hacia arriba."""
    h = CLEAR - NOZZLE_GAP
    hw, hd = HOOD_HALF_W - 0.5, MOUTH_DEPTH / 2 + 0.5        # abertura
    b = box(-hw - 2.4, hw + 2.4, -hd - 2.4, hd + 2.4, 0, h)
    b = b.union(box(-NOZZLE_PIN_X - 5, NOZZLE_PIN_X + 5, -5, 5, 0, h))
    b = b.edges("|Z").fillet(1.5).faces("<Z").chamfer(0.8)
    b = b.cut(box(-hw, hw, -hd, hd, -1, h + 1))
    b = b.cut(box(-hw + 5, hw - 5, hd - 1, hd + 4, -1, 2.0))   # el frente queda más alto para que entre la basura
    for sg in (1, -1):
        pin = cyl(sg * NOZZLE_PIN_X, 0, h - 0.1, h + PIN_H, PIN_D / 2).faces(">Z").chamfer(0.5)
        pin = pin.cut(box(sg * NOZZLE_PIN_X - 0.5, sg * NOZZLE_PIN_X + 0.5, -PIN_D, PIN_D, h + 1.2, h + PIN_H + 1))
        b = b.union(pin)
    return b


def exportar_bola(path, n_lat=48, n_lon=96, plano=0.5):
    """Bola de 16 mm con un plano pequeño para que pegue a la cama (mejor usar una canica de 16 mm).
    La malla se escribe directo en STL para que quede cerrada y pareja."""
    import struct
    from math import acos, cos, sin
    rb = BALL_D / 2
    t0 = acos((rb - plano) / rb)                       # ángulo donde empieza el plano
    anillos = [[(rb * sin(t) * cos(2 * pi * j / n_lon), rb * sin(t) * sin(2 * pi * j / n_lon),
                 rb - plano - rb * cos(t)) for j in range(n_lon)]
               for t in (t0 + (pi - t0) * i / n_lat for i in range(n_lat))]
    base, polo = (0.0, 0.0, 0.0), (0.0, 0.0, 2 * rb - plano)
    tris = []
    for j in range(n_lon):
        k = (j + 1) % n_lon
        tris.append((base, anillos[0][k], anillos[0][j]))
        for i in range(n_lat - 1):
            a_, b_, c_, d_ = anillos[i][j], anillos[i][k], anillos[i + 1][k], anillos[i + 1][j]
            tris += [(a_, b_, c_), (a_, c_, d_)]
        tris.append((anillos[-1][j], anillos[-1][k], polo))
    with open(path, "wb") as f:
        f.write(b"bola 16 mm".ljust(80, b" ") + struct.pack("<I", len(tris)))
        for t in tris:
            f.write(struct.pack("<12fH", 0, 0, 0, *t[0], *t[1], *t[2], 0))


# ===================== componentes de referencia (solo para verificar que todo cabe) =====================
def componentes(succion=False):
    c = {}
    rueda = rueda_motriz().rotate((0, 0, 0), (0, 1, 0), -90)      # eje de la rueda sobre X, cubo hacia -X
    for sg, n in ((1, "der"), (-1, "izq")):
        r_ = rueda.translate((MOTOR_X + 0.5 + HUB_L, 0, 0))
        if sg < 0:
            r_ = r_.mirror("YZ")
        c["pieza_rueda_" + n] = r_.translate((0, WHEEL_Y, AXLE_Z))
        xa, xb = sorted((sg * (MOTOR_X - MOTOR_L), sg * MOTOR_X))
        m = box(xa, xb, WHEEL_Y - MOTOR_W / 2, WHEEL_Y + MOTOR_W / 2, FLOOR + 0.05, FLOOR + MOTOR_H)
        c["motor_" + n] = m
    c["pieza_bola"] = cq.Workplane("XY").sphere(BALL_D / 2).translate((0, CASTER_Y, BALL_ZC))
    c["pieza_aro"] = aro_bola().translate((0, CASTER_Y, -RING_H))
    z_fan = FLOOR + (HOOD_H if succion else 0.0)
    c["ventilador"] = box(-FAN / 2, FAN / 2, FAN_Y - FAN / 2, FAN_Y + FAN / 2, z_fan + 0.05, z_fan + FAN_T)
    if succion:
        c["pieza_boquilla"] = boquilla().translate((0, MOUTH_YC, -(CLEAR - NOZZLE_GAP)))
    z0 = FLOOR + RPI_STANDOFF_H
    xp, ys = RPI_X_PORTS, rpi_y_sd(succion)
    rpi = box(xp - 56, xp, ys, ys + 85, z0 + 0.05, z0 + 1.4)
    for (x, y) in rpi_holes(succion):
        rpi = rpi.cut(cyl(x, y, z0 - 1, z0 + 3, 1.35))
    rpi = rpi.union(box(xp - 54, xp - 2, ys + 66, ys + 87, z0 + 1.4, z0 + 17.4))    # USB y Ethernet
    rpi = rpi.union(box(xp - 55, xp - 50, ys + 7, ys + 58, z0 + 4.5, z0 + 10))      # GPIO
    c["raspberry_pi_4"] = rpi
    z0 = FLOOR + PB_LEDGE_H
    c["placa_perforada"] = box(PB_CX - PB_X / 2, PB_CX + PB_X / 2, PB_CY - PB_Y / 2, PB_CY + PB_Y / 2,
                               z0 + 0.05, z0 + PB_T)
    c["boton"] = cyl(BUTTON_POS[0], BUTTON_POS[1], H - LID - 30, H + 3, 7.9)
    return c


def revisar(nombre, inf, sup, comp):
    """Volumen de choque entre carcasas y componentes (debe dar cero)."""
    ok = True
    for nom, pieza in (("inferior", inf), ("superior", sup)):
        for cn, cs in comp.items():
            v = pieza.intersect(cs).val().Volume()
            # intencional: los pines entran con apriete en sus casquillos y la bola toca su asiento
            if v > 0.5 and not (nom == "inferior" and cn in ("pieza_aro", "pieza_bola", "pieza_boquilla") and v < 15):
                ok = False
                print(f"  !! {nom} x {cn}: {v:.1f}")
    names = list(comp)
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            v = comp[a].intersect(comp[b]).val().Volume()
            if v > 0.5:
                ok = False
                print(f"  !! {a} x {b}: {v:.1f}")
    v = inf.intersect(sup).val().Volume()
    if v > 0.5:
        ok = False
        print(f"  !! inferior x superior: {v:.1f}")
    print(f"Interferencias, variante {nombre}: " + ("ninguna" if ok else "REVISAR"))


if __name__ == "__main__":
    sup = pieza_superior()
    inf, inf_s = pieza_inferior(), pieza_inferior(succion=True)
    rueda, aro, boq = rueda_motriz(), aro_bola(), boquilla()
    comp, comp_s = componentes(), componentes(succion=True)
    revisar("normal", inf, sup, comp)
    revisar("succión", inf_s, sup, comp_s)

    # ---- exportar ----
    tol = dict(tolerance=0.03, angularTolerance=0.1)
    cq.exporters.export(inf, os.path.join(OUT, "carcasa_inferior.stl"), **tol)
    cq.exporters.export(inf_s, os.path.join(OUT, "carcasa_inferior_succion.stl"), **tol)
    # la superior se exporta volteada: la tapa queda sobre la cama de impresión
    sup_print = sup.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, 0, H))
    cq.exporters.export(sup_print, os.path.join(OUT, "carcasa_superior.stl"), **tol)
    cq.exporters.export(rueda, os.path.join(OUT, "rueda_motriz_x2.stl"), **tol)
    cq.exporters.export(aro, os.path.join(OUT, "rueda_apoyo_aro.stl"), **tol)
    cq.exporters.export(boq, os.path.join(OUT, "boquilla_succion.stl"), **tol)
    exportar_bola(os.path.join(OUT, "rueda_apoyo_bola_16mm.stl"))
    for nombre, pieza in (("carcasa_inferior", inf), ("carcasa_inferior_succion", inf_s), ("carcasa_superior", sup),
                          ("rueda_motriz", rueda), ("rueda_apoyo_aro", aro), ("boquilla_succion", boq)):
        cq.exporters.export(pieza, os.path.join(OUT, nombre + ".step"))

    # con --preview: piezas en posición de ensamble + componentes, para generar vistas previas
    if "--preview" in sys.argv:
        for carpeta, pieza, cc in (("_preview", inf, comp), ("_preview_succion", inf_s, comp_s)):
            prev = os.path.join(OUT, carpeta)
            os.makedirs(prev, exist_ok=True)
            cq.exporters.export(pieza, os.path.join(prev, "inferior.stl"), **tol)
            cq.exporters.export(sup, os.path.join(prev, "superior.stl"), **tol)
            for cn, cs in cc.items():
                cq.exporters.export(cs, os.path.join(prev, f"comp_{cn}.stl"), tolerance=0.05, angularTolerance=0.15)

    # ---- resumen: medidas, peso y comprobación contra el motor ----
    PLA = 1.24   # g/cm3
    print("\nPiezas (peso si se imprimen macizas en PLA):")
    for nombre, pieza, n in (("carcasa inferior", inf, 1), ("inferior succión", inf_s, 1), ("carcasa superior", sup, 1),
                             ("rueda motriz", rueda, 2), ("aro de la bola", aro, 1), ("boquilla", boq, 1)):
        bb = pieza.val().BoundingBox()
        vol = pieza.val().Volume() / 1000
        print(f"  {nombre:17s} {bb.xlen:6.1f} x {bb.ylen:6.1f} x {bb.zlen:5.1f} mm  {vol * PLA:6.1f} g x{n}"
              f"   válido: {pieza.val().isValid()}  sólidos: {pieza.solids().size()}")
    print(f"\nSeparación del piso al suelo: {CLEAR:.1f} mm.  Altura total sobre el suelo: {H + CLEAR:.1f} mm")
    for volt, rpm, ozin in ((6, 30, 110), (12, 60, 200)):
        print(f"A {volt} V: {pi * WHEEL_D / 10 * rpm / 60:.1f} cm/s, "
              f"fuerza máxima por rueda {ozin * 0.00706155 / (WHEEL_D / 2000):.0f} N")
