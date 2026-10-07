#!/bin/sh
# Compila il pacchetto .deb per una distribuzione Debian/Ubuntu diversa da quella in uso,
# dentro un container (docker o podman). Esempio:
#   packaging/build-deb-in-container.sh debian:12
#   packaging/build-deb-in-container.sh ubuntu:22.04
# Il pacchetto finisce in dist/.
set -e
IMAGE="${1:-debian:12}"
cd "$(dirname "$0")/.."
RUNTIME=$(command -v podman || command -v docker) || { echo "Serve docker o podman"; exit 1; }
mkdir -p dist
"$RUNTIME" run --rm -v "$PWD:/src:Z" -w /src "$IMAGE" sh -c '
  export DEBIAN_FRONTEND=noninteractive
  apt-get update && apt-get install -y --no-install-recommends build-essential cmake ninja-build pkg-config file \
    libgtk-3-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
  rm -rf build-container && cmake -S . -B build-container -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build build-container && (cd build-container && cpack -G DEB) && cp build-container/*.deb dist/ && rm -rf build-container
  chown -R '"$(id -u):$(id -g)"' dist'
ls -la dist/*.deb
