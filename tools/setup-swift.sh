#!/usr/bin/env bash
set -euo pipefail
. /etc/os-release
[[ ${ID:-} == ubuntu && ${VERSION_ID:-} == 22.04 ]] || { echo 'Ubuntu 22.04 required' >&2; exit 2; }
arch=$(uname -m)
case "$arch" in
  aarch64) platform=ubuntu2204-aarch64; suffix=ubuntu22.04-aarch64 ;;
  x86_64) platform=ubuntu2204; suffix=ubuntu22.04 ;;
  *) echo 'Unsupported architecture' >&2; exit 2 ;;
esac
version=6.0.3
name="swift-${version}-RELEASE-${suffix}"
destination="$HOME/.local/share/deskpet/toolchains"
mkdir -p "$destination"
if [[ ! -x "$destination/$name/usr/bin/swift" ]]; then
  sudo apt-get install -y ca-certificates curl libcurl4-openssl-dev libedit2 libsqlite3-0 libxml2 libz3-4 libncurses6 libpython3.10 libatomic1 tzdata
  scratch=$(mktemp -d)
  trap 'rm -rf -- "$scratch"' EXIT
  curl -fL --retry 2 "https://download.swift.org/swift-${version}-release/$platform/swift-${version}-RELEASE/$name.tar.gz" -o "$scratch/swift.tar.gz"
  tar xzf "$scratch/swift.tar.gz" -C "$destination"
  sha256sum "$scratch/swift.tar.gz" > "$destination/$name.archive.sha256"
fi
"$destination/$name/usr/bin/swift" --version
printf 'export PATH="%s/%s/usr/bin:$PATH"\n' "$destination" "$name" > "$destination/env.sh"
echo "Run: source \"$destination/env.sh\""
