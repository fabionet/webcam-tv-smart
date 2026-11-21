#!/bin/sh
# Compila l'edizione Windows da Linux con MinGW-w64 e crea i due pacchetti .zip in ../dist/
#   Dipendenze (Debian/Ubuntu): sudo apt install mingw-w64 cmake ninja-build zip curl
#   ./build.sh           -> entrambe le varianti
#   ./build.sh xp|x64    -> una sola
set -e
cd "$(dirname "$0")"
./fetch-deps.sh
VER=$(sed -n 's/^project(.*VERSION \([0-9.]*\).*/\1/p' ../CMakeLists.txt)
mkdir -p ../dist
build() {  # nome-cartella toolchain etichetta
  cmake -S . -B "build-$1" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="toolchains/$2" >/dev/null
  cmake --build "build-$1"
  [ "$1" = "xp" ] && ./check-xp-imports.sh build-xp/WebCamTVSmartVision.exe
  PKG="WebCamTVSmartVision-$VER-$3"
  rm -rf "build-$1/$PKG" && mkdir -p "build-$1/$PKG"
  cp "build-$1/WebCamTVSmartVision.exe" LEGGIMI.txt LICENZE-TERZE-PARTI.txt ../data/copyright "build-$1/$PKG/"
  mv "build-$1/$PKG/copyright" "build-$1/$PKG/LICENZA.txt"
  [ -f ../docs/Guida-Windows-WebCam-TV-Smart-Vision.pdf ] && cp ../docs/Guida-Windows-WebCam-TV-Smart-Vision.pdf "build-$1/$PKG/"
  (cd "build-$1" && rm -f "../../dist/$PKG.zip" && zip -q -r -9 "../../dist/$PKG.zip" "$PKG")
  echo "Pacchetto: dist/$PKG.zip"
}
case "${1:-all}" in
  xp)  build xp  mingw-i686-xp.cmake winxp-x86-sse2 ;;
  x64) build x64 mingw-x86_64.cmake  win64 ;;
  *)   build xp  mingw-i686-xp.cmake winxp-x86-sse2; build x64 mingw-x86_64.cmake win64 ;;
esac
