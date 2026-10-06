# Modelo 3D de la carcasa

Carcasa circular de 215 mm en dos piezas que cierran a presión, con sus tres ruedas.
El detalle del diseño está en [`../docs/hardware-chasis.md`](../docs/hardware-chasis.md)
y las notas originales, con los ajustes de impresión, en [`LEEME.txt`](LEEME.txt).

| Archivo | Qué es |
|---|---|
| `carcasa_roomba.py` | Modelo paramétrico en CadQuery. `python carcasa_roomba.py` regenera todas las piezas |
| `carcasa_inferior_succion.stl` | Pieza de abajo que lleva el robot, con la campana de succión |
| `boquilla_succion.stl` | Boquilla que entra a presión por debajo |
| `carcasa_inferior.stl` | Variante de la pieza de abajo sin campana; no es la que se usó |
| `carcasa_superior.stl` | Pieza de arriba, con las pestañas de clic |
| `rueda_motriz_x2.stl` | Rueda motriz de 30 mm; se imprimen dos |
| `rueda_apoyo_aro.stl`, `rueda_apoyo_bola_16mm.stl` | Aro y bola de la rueda de apoyo |
| `*.step` | Las mismas piezas para editar en un programa CAD |
| `vistas/` | Imágenes del diseño |

![Carcasa armada](vistas/vista_6_armada.png)
