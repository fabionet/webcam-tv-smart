#!/bin/sh
# Imposta i valori consigliati per la Logitech C920 (e webcam UVC simili) al collegamento.
# Usato dalla regola udev 99-logitech-c920-smartvision.rules; richiede v4l-utils (v4l2-ctl).
# Uso manuale: c920-defaults.sh /dev/video0
DEV="${1:-/dev/video0}"
command -v v4l2-ctl >/dev/null 2>&1 || exit 0
[ -c "$DEV" ] || exit 0
# Anti-sfarfallio 50 Hz (rete elettrica europea), esposizione e bilanciamento automatici,
# autofocus continuo attivo, nitidezza moderata.
v4l2-ctl -d "$DEV" \
  --set-ctrl=power_line_frequency=1 \
  --set-ctrl=auto_exposure=3 \
  --set-ctrl=white_balance_automatic=1 \
  --set-ctrl=focus_automatic_continuous=1 \
  --set-ctrl=sharpness=128 2>/dev/null
exit 0
