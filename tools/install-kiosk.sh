#!/usr/bin/env bash
# Run deskpet-hardware on a bare Xorg (no GNOME) from boot. Usage:
#   bash tools/install-kiosk.sh            install and enable; takes effect on the next reboot
#   bash tools/install-kiosk.sh --now      install, enable and switch now (ends the running desktop)
#   bash tools/install-kiosk.sh --uninstall  restore the desktop boot and remove the kiosk unit
set -euo pipefail
action=${1:-install}
case $action in install|--now|--uninstall) ;; *) echo 'Usage: bash tools/install-kiosk.sh [--now|--uninstall]' >&2; exit 2;; esac
if [[ $(id -u) -eq 0 ]]; then echo '기기 운영 계정(일반 사용자)으로 실행하세요. 필요한 단계만 sudo를 사용합니다.' >&2; exit 1; fi
user=$(id -un)
unit="deskpet-kiosk@$user.service"
task_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
state=/etc/deskpet-hardware

if [[ $action == --uninstall ]]; then
  sudo systemctl disable "$unit" 2>/dev/null || true
  previous=$(cat "$state/kiosk.previous-target" 2>/dev/null || echo graphical.target)
  sudo systemctl set-default "$previous"
  if [[ -e $state/Xwrapper.config.orig ]]; then sudo cp -p "$state/Xwrapper.config.orig" /etc/X11/Xwrapper.config
  elif [[ -e $state/Xwrapper.config.none ]]; then sudo rm -f /etc/X11/Xwrapper.config; fi
  sudo rm -f /etc/systemd/system/deskpet-kiosk@.service "$state/kiosk.previous-target" "$state/Xwrapper.config.orig" "$state/Xwrapper.config.none"
  sudo systemctl daemon-reload
  echo "키오스크를 해제했습니다. 기본 부팅 대상: $previous. 재부팅하면 데스크탑으로 돌아갑니다."
  echo "지금 바로 데스크탑을 띄우려면: sudo systemctl stop $unit && sudo systemctl isolate $previous"
  exit 0
fi

[[ -x /usr/local/bin/deskpet-hardware ]] || { echo '먼저 bash tools/install-jetson.sh real 로 앱을 설치하세요.' >&2; exit 1; }
config="$HOME/.config/deskpet-hardware/config.json"
python3 - "$config" <<'PY'
import json, sys
c = json.load(open(sys.argv[1]))
assert c['mode'] == 'real', 'Kiosk requires an explicit real configuration'
assert c['audio']['captureDevice'] and c['audio']['playbackDevice'], 'Select explicit ALSA devices'
assert c['servo']['i2cDevice'], 'Select the verified I2C adapter'
assert not c['display'].get('headless', False), 'Kiosk needs display.headless=false'
assert c['display'].get('fullscreen', False), 'Kiosk needs display.fullscreen=true'
PY

sudo apt-get install -y xinit x11-xserver-utils xserver-xorg-legacy
sudo install -d -m 0755 /usr/local/lib/deskpet-hardware "$state"
sudo install -m 0755 "$task_root/deploy/deskpet-xsession" /usr/local/lib/deskpet-hardware/deskpet-xsession
sudo install -m 0644 "$task_root/deploy/deskpet-kiosk@.service" /etc/systemd/system/deskpet-kiosk@.service
if [[ ! -e $HOME/.config/deskpet-hardware/kiosk.env ]]; then
  install -m 0600 "$task_root/deploy/kiosk.env.example" "$HOME/.config/deskpet-hardware/kiosk.env"
fi

# Xorg started from a service (not a display manager) needs the setuid wrapper on Jetson's nvidia driver.
if [[ ! -e $state/Xwrapper.config.orig && ! -e $state/Xwrapper.config.none ]]; then
  if [[ -e /etc/X11/Xwrapper.config ]]; then sudo cp -p /etc/X11/Xwrapper.config "$state/Xwrapper.config.orig"
  else sudo touch "$state/Xwrapper.config.none"; fi
fi
printf 'allowed_users=console\nneeds_root_rights=yes\n' | sudo tee /etc/X11/Xwrapper.config >/dev/null
sudo usermod -aG video,input,tty "$user"

current=$(systemctl get-default)
if [[ $current != multi-user.target && ! -e $state/kiosk.previous-target ]]; then
  echo "$current" | sudo tee "$state/kiosk.previous-target" >/dev/null
fi
# The user service is tied to a desktop session; disable it so a later desktop login cannot start a second copy.
if [[ -n ${XDG_RUNTIME_DIR:-} ]]; then systemctl --user disable deskpet-hardware.service 2>/dev/null || true; fi
sudo systemctl daemon-reload
sudo systemctl set-default multi-user.target
sudo systemctl enable "$unit"

if [[ $action == --now ]]; then
  # Conflicts=display-manager.service stops GDM; --no-block lets this terminal be closed with it.
  echo '데스크탑을 종료하고 키오스크로 전환합니다. 데스크탑에서 실행 중이면 이 창도 함께 닫힙니다.'
  sudo systemctl --no-block start "$unit"
else
  echo "설치 완료. 재부팅하면 데스크탑 없이 DeskPet 화면만 뜹니다: sudo reboot"
fi
echo "로그: journalctl -u $unit -f   해제: bash tools/install-kiosk.sh --uninstall"
