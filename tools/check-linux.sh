#!/usr/bin/env bash
set -euo pipefail
# Run inside a native ARM64 Ubuntu 22.04 container. Build output stays out of source.
test "$(uname -m)" = aarch64
cmake -S . -B /tmp/deskpet-cmake -DCMAKE_BUILD_TYPE=Debug -DDESKPET_LINUX=ON -DDESKPET_SDL=ON
cmake --build /tmp/deskpet-cmake -j 2
python3 tests/swift_live.py /tmp/deskpet-cmake/deskpet-hardware swift /tmp/deskpet-swift
export DESKPET_SWIFT_DEMO=/tmp/deskpet-swift/debug/deskpet-swift-demo
ctest --test-dir /tmp/deskpet-cmake --output-on-failure
python3 tests/visual.py /tmp/deskpet-cmake/deskpet-hardware /tmp/deskpet-preview
/tmp/deskpet-cmake/deskpet-hardware doctor
