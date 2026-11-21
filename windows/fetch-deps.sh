#!/bin/sh
# Scarica e verifica (SHA-256) i sorgenti delle librerie di codifica per l'edizione Windows.
# Risultato: windows/third_party/_src/{libogg,libvorbis,libtheora,vo-aacenc}
set -e
cd "$(dirname "$0")/third_party"
mkdir -p _dl _src
fetch() {  # nome url sha256 cartella-estratta
  name="$1"; url="$2"; sum="$3"; dir="$4"; file="_dl/$(basename "$url")"
  [ "$name" = "vo-aacenc" ] && file="_dl/vo-aacenc-0.1.3.tar.gz"
  if [ ! -d "_src/$name" ]; then
    [ -f "$file" ] || curl -fsSL --retry 3 -o "$file" "$url"
    echo "$sum  $file" | sha256sum -c - >/dev/null || { echo "ERRORE: impronta SHA-256 non valida per $file"; exit 1; }
    rm -rf "_src/$dir" && tar -xf "$file" -C _src && mv "_src/$dir" "_src/$name"
    echo "ok  $name"
  fi
}
fetch libogg    https://downloads.xiph.org/releases/ogg/libogg-1.3.5.tar.xz       c4d91be36fc8e54deae7575241e03f4211eb102afb3fc0775fbbc1b740016705 libogg-1.3.5
fetch libvorbis https://downloads.xiph.org/releases/vorbis/libvorbis-1.3.7.tar.xz b33cc4934322bcbf6efcbacf49e3ca01aadbea4114ec9589d1b1e9d20f72954b libvorbis-1.3.7
fetch libtheora https://downloads.xiph.org/releases/theora/libtheora-1.2.0.tar.xz ebdf77a8f5c0a8f7a9e42323844fa09502b34eb1d1fece7b5f54da41fe2122ec libtheora-1.2.0
fetch vo-aacenc https://github.com/mstorsjo/vo-aacenc/archive/refs/tags/v0.1.3.tar.gz ae38610405f862237f6c70360c406297cc4222abf82e86722c505ba74b53a97d vo-aacenc-0.1.3
