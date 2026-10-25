#!/usr/bin/env bash
set -euo pipefail
profile=${1:-board-only}
if [[ $profile != board-only && $profile != real ]]; then echo 'Usage: bash tools/install-jetson.sh [board-only|real]' >&2; exit 2; fi
if [[ $(id -u) -eq 0 ]]; then
  echo '일반 사용자로 실행하세요. 필요한 단계만 sudo를 사용합니다.' >&2
  exit 1
fi
if [[ $(uname -m) != aarch64 || ! -f /etc/nv_tegra_release ]]; then
  echo '이 설치기는 Jetson ARM64용입니다.' >&2
  exit 1
fi
. /etc/os-release
if [[ ${ID:-} != ubuntu || ${VERSION_ID:-} != 22.04 ]]; then
  echo '지원 기준은 JetPack 6.x / Ubuntu 22.04입니다. OS를 자동 변경하지 않습니다.' >&2
  exit 1
fi
task_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
python3 "$task_root/tools/board-doctor.py" --require-supported
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libsdl2-dev libsdl2-ttf-dev \
  libasound2-dev libsoxr-dev i2c-tools alsa-utils fonts-noto-cjk python3 usbutils curl ca-certificates
cmake -S "$task_root" -B "$task_root/build-jetson" -DCMAKE_BUILD_TYPE=Release -DDESKPET_LINUX=ON
cmake --build "$task_root/build-jetson" -j 2
ctest --test-dir "$task_root/build-jetson" --output-on-failure
sudo cmake --install "$task_root/build-jetson"
sudo groupadd -f deskpet-hw
sudo usermod -aG deskpet-hw,audio "$(id -un)"
sudo install -m 0644 "$task_root/deploy/99-deskpet-hardware.rules" /etc/udev/rules.d/99-deskpet-hardware.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=i2c-dev
mkdir -p "$HOME/.config/deskpet-hardware" "$HOME/.config/systemd/user"
if [[ ! -e "$HOME/.config/deskpet-hardware/config.json" ]]; then
  install -m 0600 "$task_root/config/jetson.json" "$HOME/.config/deskpet-hardware/config.json"
fi
if [[ ! -e "$HOME/.config/deskpet-hardware/board-only.json" ]]; then
  install -m 0600 "$task_root/config/board-only.json" "$HOME/.config/deskpet-hardware/board-only.json"
fi
install -m 0644 "$task_root/deploy/deskpet-hardware.service" "$HOME/.config/systemd/user/deskpet-hardware.service"
if [[ -n ${XDG_RUNTIME_DIR:-} ]]; then systemctl --user daemon-reload; fi
echo "설치 완료: $profile. 자동 시작은 활성화하지 않았습니다. 기존 서비스의 활성 상태는 보존합니다."
echo 'Swift 도구는 bash tools/setup-swift.sh로 설치하세요. docs/board-test.md를 따르세요.'
echo '보정 완료 전 귀 출력은 꺼져 있습니다. 실제 구동 순서는 docs/jetson-setup.md를 참고하세요.'
