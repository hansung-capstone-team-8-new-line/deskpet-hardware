# 데스크탑 없이 화면 띄우기 (키오스크 모드)

기본 설치는 GNOME에 로그인한 뒤 user 서비스로 앱을 띄웁니다. 키오스크 모드에서는 GNOME을 띄우지 않고, 부팅하면 **Xorg 하나에 `deskpet-hardware`만** 실행합니다. 윈도우 매니저나 패널, 데스크탑 데몬이 없어서 그만큼 메모리를 아낄 수 있고, 자동 로그인을 설정할 필요도 없습니다.

```
부팅 → multi-user.target → deskpet-kiosk@<계정>.service
        └ xinit → Xorg :0 (vt7, 커서 없음, 절전 끔)
                └ deskpet-xsession → xrandr로 출력 1개만 켬 → deskpet-hardware (전체화면)
```

| 파일 | 설치 위치 | 역할 |
| --- | --- | --- |
| [deploy/deskpet-kiosk@.service](../deploy/deskpet-kiosk@.service) | `/etc/systemd/system/` | 부팅 시 해당 계정으로 PAM 로그인 세션을 열고 xinit 실행 |
| [deploy/deskpet-xsession](../deploy/deskpet-xsession) | `/usr/local/lib/deskpet-hardware/` | 화면보호기 끄기, 사용할 출력만 켜기, 앱 실행 |
| [deploy/kiosk.env.example](../deploy/kiosk.env.example) | `~/.config/deskpet-hardware/kiosk.env` | 출력 이름·해상도·회전 지정(선택) |
| [tools/install-kiosk.sh](../tools/install-kiosk.sh) | — | 설치·전환·해제 |

## 준비

1. [Jetson 설치](jetson-setup.md)를 마치고 `config.json`을 채웁니다(`mode: real`, 오디오 장치, I2C, `display.fullscreen: true`).
2. 데스크탑에서 user 서비스로 한 번 정상 동작하는지 확인합니다. 키오스크는 같은 실행 파일과 같은 설정을 사용합니다.
3. **SSH 접속이 되는지 확인합니다.** 키오스크로 바꾸면 화면에는 DeskPet만 보이므로, 이후 작업은 SSH나 `Ctrl+Alt+F3` 텍스트 콘솔에서 합니다.

## 설치

기기 운영 계정(앱을 실행할 일반 사용자)으로 프로젝트 폴더에서 실행합니다.

```sh
bash tools/install-kiosk.sh          # 설치만 하고 다음 부팅부터 적용
sudo reboot
```

재부팅 없이 바로 바꾸려면 `--now`를 붙입니다. GNOME이 종료되므로 데스크탑 터미널에서 실행했다면 그 창도 함께 닫힙니다.

```sh
bash tools/install-kiosk.sh --now
```

설치기가 하는 일은 다음과 같습니다.

- `xinit`, `x11-xserver-utils`(xrandr·xset), `xserver-xorg-legacy`(Xorg 권한 래퍼)를 설치합니다.
- `/etc/X11/Xwrapper.config`를 `needs_root_rights=yes`로 설정합니다. 원본은 `/etc/deskpet-hardware/`에 백업합니다.
- 계정을 `video`, `input`, `tty` 그룹에 추가합니다.
- 기본 부팅 대상을 `multi-user.target`으로 바꾸고, 이전 값은 해제할 때 쓰도록 저장합니다.
- 데스크탑 user 서비스(`deskpet-hardware.service`)를 비활성화해서 앱이 두 번 뜨지 않게 합니다.
- `deskpet-kiosk@<계정>.service`를 활성화합니다.

## 출력 화면 지정

기본값은 "연결된 첫 번째 출력을 기본 해상도로 켜고, 나머지 출력은 끄기"입니다. 출력이 여러 개 연결되어 있거나 해상도를 고정하고 싶으면 `~/.config/deskpet-hardware/kiosk.env`를 편집합니다.

```sh
DISPLAY=:0 xrandr --query        # SSH에서 키오스크가 실행 중일 때 출력 이름 확인
```

```sh
# ~/.config/deskpet-hardware/kiosk.env
DESKPET_OUTPUT=DP-0
DESKPET_MODE=1024x600
DESKPET_ROTATE=normal
```

회전은 `DESKPET_ROTATE`(xrandr)와 `display.rotation`(앱) 중 **한 곳에서만** 적용합니다. 수정한 뒤에는 서비스를 재시작해야 반영됩니다.

## 운영 명령

SSH에서 실행합니다. `<계정>`은 설치한 사용자 이름입니다.

```sh
systemctl status deskpet-kiosk@<계정>
journalctl -u deskpet-kiosk@<계정> -f          # 앱·xrandr 로그
sudo systemctl restart deskpet-kiosk@<계정>    # 설정 변경 후 재시작
sudo systemctl stop deskpet-kiosk@<계정>       # 일시 중지(재부팅하면 다시 뜸)
cat /var/log/Xorg.0.log | grep -E '\(EE\)|\(WW\)'  # Xorg 오류 (계정에 따라 ~/.local/share/xorg/Xorg.0.log)
```

앱 소켓은 `/run/user/<uid>/deskpet-hardware.sock`입니다. 같은 계정으로 SSH 접속하면 `deskpetctl`이 기본 경로로 그대로 연결됩니다.

```sh
deskpetctl face --emotion happy --seconds 2
```

## 메모리 비교

바꾸기 전후로 같은 상태(앱 대기 화면)에서 측정해 기록합니다.

```sh
free -m
ps -eo rss,comm --sort=-rss | head -15     # 상위 프로세스 RSS(KB)
```

데스크탑 모드에서는 `gnome-shell`, `Xorg`/`Xwayland`, `gsd-*`, `evolution-*`, `tracker-*` 같은 프로세스가 상위에 보입니다. 키오스크 모드에서는 `Xorg`와 `deskpet-hardware`만 남아야 정상입니다.

## 데스크탑으로 되돌리기

```sh
bash tools/install-kiosk.sh --uninstall
sudo reboot
```

해제하면 키오스크 서비스를 끄고, 기본 부팅 대상과 `Xwrapper.config`를 원래대로 되돌립니다. 데스크탑 user 서비스는 자동으로 다시 켜지 않으므로 필요하면 `bash tools/enable-service.sh`를 실행합니다. 잠깐 데스크탑만 확인하고 싶을 때는 해제하지 않고 다음 명령으로 GNOME을 띄울 수 있습니다(재부팅하면 키오스크로 돌아옴).

```sh
sudo systemctl stop deskpet-kiosk@<계정>
sudo systemctl start gdm
```

## 문제 해결

| 증상 | 확인할 내용 |
| --- | --- |
| 화면이 검은 채로 아무것도 안 뜸 | `journalctl -u deskpet-kiosk@<계정>`에서 앱 오류(`DISPLAY_ERROR`, 설정 오류) 확인. 서비스는 실패하면 3초 뒤 재시작 |
| `Only console users are allowed to run the X server` | `Xwrapper.config`가 설치됐는지, 서비스에 `PAMName=login`·`TTYPath=/dev/tty7`가 있는지 확인 |
| `no screens found` / nvidia 오류 | `/etc/X11/xorg.conf`(JetPack 기본, nvidia 드라이버)가 남아 있는지 확인. 설치기는 이 파일을 수정하지 않음 |
| 다른 모니터에 뜸 / 해상도가 다름 | `kiosk.env`에 `DESKPET_OUTPUT`·`DESKPET_MODE` 지정 |
| 터치가 안 됨 | `input` 그룹 반영(재부팅) 후 `DISPLAY=:0 xinput list`에 터치 장치가 보이는지 확인 |
| 소리·귀가 안 됨 | 데스크탑과 같은 원인. `deskpet-hardware doctor`, `audio`·`deskpet-hw` 그룹 확인 |
| 몇 분 뒤 화면이 꺼짐 | `deskpet-xsession`의 `xset`과 Xorg의 `-s 0 -dpms`가 적용됐는지 로그 확인 |

## 검증 상태

설치 스크립트와 서비스 파일은 문법 검사만 했고, Jetson 실기에서는 아직 확인하지 않았습니다. 첫 적용 때는 [실물 검증표](validation.md)의 화면·터치·오디오 항목을 키오스크 모드에서 다시 확인하고, 메모리 측정값을 함께 기록해 주세요.
