#!/usr/bin/env bash
set -euo pipefail
config="$HOME/.config/deskpet-hardware/config.json"
python3 - "$config" <<'PY'
import json, sys
c = json.load(open(sys.argv[1]))
assert c['mode'] == 'real', 'Service requires an explicit real configuration'
assert c['audio']['captureDevice'] and c['audio']['playbackDevice'], 'Select explicit ALSA devices'
assert c['servo']['i2cDevice'], 'Select the verified I2C adapter'
assert not c['display'].get('headless', False), 'A graphical session is required'
PY
[[ -n ${DISPLAY:-}${WAYLAND_DISPLAY:-} ]] || { echo 'Run in a graphical login session' >&2; exit 2; }
systemctl --user import-environment DISPLAY XAUTHORITY WAYLAND_DISPLAY
systemctl --user daemon-reload
systemctl --user enable --now deskpet-hardware.service
systemctl --user status deskpet-hardware.service --no-pager
