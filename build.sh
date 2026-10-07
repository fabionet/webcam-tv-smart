#!/bin/sh
# Compila e (facoltativamente) installa WebCam and TV Smart Vision.
#   ./build.sh            -> compila in ./build
#   ./build.sh install    -> compila e installa in /usr/local (richiede sudo)
#   ./build.sh deb        -> crea il pacchetto Debian in dist/
set -e
cd "$(dirname "$0")"
if [ "$1" = "deb" ]; then
  cmake -S . -B build-deb -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build build-deb -j"$(nproc)"
  (cd build-deb && cpack -G DEB)
  mkdir -p dist && cp build-deb/*.deb dist/
  echo "Pacchetto: $(ls dist/*.deb)"
  echo "Installazione: sudo apt install ./dist/$(basename "$(ls dist/*.deb)")"
  exit 0
fi
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
if [ "$1" = "install" ]; then
  sudo cmake --install build
  sudo gtk-update-icon-cache -f /usr/local/share/icons/hicolor 2>/dev/null || true
  sudo update-desktop-database /usr/local/share/applications 2>/dev/null || true
  echo "Installato: avvia 'WebCam and TV Smart Vision' dal menu di GNOME o con 'webcam-tv-smart-vision'."
else
  echo "Eseguibile: ./build/webcam-tv-smart-vision"
fi
