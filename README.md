# WebCam and TV Smart Vision

[![Pacchetti Debian/Ubuntu](https://github.com/fabionet/webcam-tv-smart/actions/workflows/build-deb.yml/badge.svg)](https://github.com/fabionet/webcam-tv-smart/actions/workflows/build-deb.yml)
[![Windows (XP 32 bit + 64 bit)](https://github.com/fabionet/webcam-tv-smart/actions/workflows/build-windows.yml/badge.svg)](https://github.com/fabionet/webcam-tv-smart/actions/workflows/build-windows.yml)

Applicazione GNOME (GTK3 + GStreamer, interamente in C++17) per webcam UVC come la
**Logitech C920 / C920 HD Pro / C922 / Brio 4K**, schede **TV digitali terrestri, via cavo e
satellitari (DVB)** e flussi di rete. Finestra principale 800×600 con header bar in stile GNOME.
Funziona su **Debian 12/13, Ubuntu 22.04/24.04 e derivate** (Linux Mint, Pop!_OS, Zorin, elementary, MX…).
Esiste anche un'**[edizione Windows](#edizione-windows)** portatile per **Windows XP a 32 bit** e **Windows 7, 8, 10, 11 a 64 bit**.

![Finestra principale](docs/img/schermata.png)

📘 **[Guida illustrata in PDF – Linux](docs/Guida-WebCam-TV-Smart-Vision.pdf)** · 📗 **[Guida illustrata in PDF – Windows](docs/Guida-Windows-WebCam-TV-Smart-Vision.pdf)**

## Funzioni

| Area | Cosa fa |
|------|---------|
| Sorgente | Nome della periferica di cattura rilevata (es. *HD Pro Webcam C920*) con tasto **Aggiorna**; selettore webcam V4L2 / adattatori DVB / URL rtsp-http-file / mira di prova; formato-risoluzione-fps; ingresso audio abbinato automaticamente al microfono della webcam; guadagno; specchiatura/rotazione |
| Webcam | Tutti i controlli V4L2 esposti dal driver (luminosità, contrasto, saturazione, zoom, pan/tilt, messa a fuoco, esposizione, bilanciamento del bianco, anti-sfarfallio 50/60 Hz…) più profili rapidi (Auto, 50 Hz, Fuoco fisso) |
| TV e satellite | Elenco canali da `channels.conf` (formato zap o DVBv5), scansione con `dvbv5-scan`, sintonia manuale (DVB-T/T2/C/S/S2, frequenza, banda, symbol rate, polarità) |
| Rec | Registrazione audio+video in **MP4 (H.264 + AAC)** o **OGG (Theora + Vorbis)**, bitrate regolabili, istantanee PNG, cartella e prefisso file |
| Uscite | Ponte del segnale passante verso un **dispositivo video V4L2** (v4l2loopback o uscita hardware), verso una **uscita audio** (HDMI, cuffie, ecc.) e verso una **finestra esterna** da mettere a schermo intero su un secondo monitor/TV |

Se nessuna webcam è collegata l'area video mostra **"Nessuna periferica di cattura rilevata"** con il
tasto **Cerca dispositivi**; appena colleghi la webcam viene riconosciuta e avviata da sola (hot-plug).

Scorciatoie: `Ctrl+R` registra, `Ctrl+S` istantanea, `F11` schermo intero, `Esc` esce dallo schermo intero.

## Installazione dal pacchetto .deb

Scarica dalla pagina [Releases](https://github.com/fabionet/webcam-tv-smart/releases) il pacchetto
per la tua distribuzione e installalo con apt, che risolve da solo le dipendenze:

| Distribuzione (e derivate) | Pacchetto |
|---|---|
| Debian 12 "bookworm" (MX 23, …) | `webcam-tv-smart-vision_<ver>_debian12_amd64.deb` |
| Debian 13 "trixie" | `webcam-tv-smart-vision_<ver>_debian13_amd64.deb` |
| Ubuntu 22.04 (Mint 21, Pop!_OS 22.04, Zorin 17, elementary 7, …) | `webcam-tv-smart-vision_<ver>_ubuntu22.04_amd64.deb` |
| Ubuntu 24.04 (Mint 22, …) | `webcam-tv-smart-vision_<ver>_ubuntu24.04_amd64.deb` |

```bash
sudo apt install ./webcam-tv-smart-vision_*_amd64.deb
```

I pacchetti vengono compilati da GitHub Actions dentro container ufficiali di ogni distribuzione
(`.github/workflows/build-deb.yml`), così le dipendenze (glibc, GTK, GStreamer) sono quelle giuste
per ciascuna. Per produrne uno in locale per un'altra distro: `packaging/build-deb-in-container.sh debian:12`.

## Edizione Windows

![Edizione Windows](docs/img/windows/win-sorgente.png)

Un solo eseguibile **portatile** (circa 2 MB): niente installazione, DLL, .NET o codec. Scarica lo zip dalla
pagina [Releases](https://github.com/fabionet/webcam-tv-smart/releases), estrailo e avvia `WebCamTVSmartVision.exe`.

| Pacchetto | Sistemi | Processore |
|---|---|---|
| `WebCamTVSmartVision-<ver>-winxp-x86-sse2.zip` | Windows **XP SP3** a 32 bit e tutti i successivi (anche a 64 bit) | x86 con SSE e SSE2 (Pentium 4, Pentium M, Athlon 64 o più recenti) |
| `WebCamTVSmartVision-<ver>-win64.zip` | Windows **7, 8, 8.1, 10, 11** a 64 bit | qualsiasi x86-64 |

Che cosa fa: anteprima di webcam UVC (Logitech C920, Brio…), schede di acquisizione e sintonizzatori TV analogici
tramite **DirectShow**; nome della periferica rilevata, tasto *Cerca dispositivi* e riconoscimento a caldo;
registrazione **MP4 (H.264 + AAC)** e **OGG (Theora + Vorbis)** con codificatori inclusi nell'eseguibile; istantanee PNG;
controlli della webcam; ponte del segnale verso un secondo schermo (finestra esterna a schermo intero) e verso
un'uscita audio a scelta. Dettagli nella [guida Windows](docs/Guida-Windows-WebCam-TV-Smart-Vision.pdf).

Limiti rispetto a Linux: le schede TV digitali DVB vengono solo rilevate, non ci sono i flussi di rete né la
webcam virtuale (richiederebbe l'installazione di un driver), e nei file registrati le altezze non multiple di 16
vengono ritagliate di pochi pixel (1080 → 1072).

Verifiche: a ogni versione GitHub Actions compila i due eseguibili con MinGW-w64, controlla che quello a 32 bit
importi solo funzioni presenti in Windows XP SP3 ed esegue l'auto-test completo (registrazioni comprese, più la
cattura DirectShow da una videocamera virtuale) su Windows Server 2022 e 2025. Su Windows XP/7/8 reali e con una
webcam fisica l'edizione Windows non è ancora stata provata: le segnalazioni sono benvenute.

Compilazione da Linux:

```bash
sudo apt install mingw-w64 cmake ninja-build zip curl
windows/build.sh        # scarica le librerie (con verifica SHA-256), compila e crea i due zip in dist/
```

## Driver per la Logitech C920

Non serve alcun driver aggiuntivo: la C920 è una webcam **UVC** (USB Video Class) e il kernel Linux la
gestisce nativamente con due moduli presenti in ogni distribuzione:

- `uvcvideo` → video (`/dev/videoN`, controlli V4L2)
- `snd-usb-audio` → microfono stereo USB (via PipeWire/PulseAudio)

La C920 è una webcam **1080p**; il modello Logitech 4K è la **Brio**, anch'essa UVC e gestita allo stesso modo.
Il pacchetto include una regola udev facoltativa che crea il link stabile `/dev/webcam-c920` e imposta i
valori consigliati (50 Hz, esposizione automatica, autofocus) a ogni collegamento:

```bash
sudo cp /usr/share/webcam-tv-smart-vision/99-logitech-c920-smartvision.rules /etc/udev/rules.d/ && sudo udevadm control --reload
```

## Compilazione dai sorgenti

```bash
sudo apt install build-essential cmake ninja-build pkg-config libgtk-3-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-x gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
  gstreamer1.0-libav gstreamer1.0-gtk3 gstreamer1.0-pipewire \
  v4l-utils dvb-tools v4l2loopback-dkms
```

```bash
git clone https://github.com/fabionet/webcam-tv-smart.git && cd webcam-tv-smart && ./build.sh
```

| Comando | Effetto |
|---|---|
| `./build.sh` | compila in `build/` |
| `./build.sh install` | installa in `/usr/local` (menu GNOME, icona, manuale) |
| `./build.sh deb` | crea il pacchetto `.deb` per la distro in uso in `dist/` |
| `./build/webcam-tv-smart-vision --self-test /tmp/sv` | auto-test senza hardware: mira di prova, MP4 e OGG di ~4 s, istantanea, schermate |

Requisiti minimi: GTK 3.22, GStreamer 1.16, GLib 2.56, compilatore C++17.

## Ponte del segnale (uscite)

1. **Video → V4L2**: nella pagina *Uscite* premi *Crea dispositivo virtuale…* (carica `v4l2loopback`
   con `pkexec`, compare `/dev/video10` "SmartVision Output"), scegli il dispositivo e attiva
   l'interruttore. Browser, OBS e videoconferenze vedranno il segnale come una webcam.
2. **Audio → uscita hardware**: scegli il sink (HDMI, cuffie, altoparlanti) e attiva; volume e muto
   agiscono solo sul ponte, non sulla registrazione.
3. **Finestra esterna**: apre una seconda finestra con lo stesso flusso; trascinala sul monitor o TV
   collegato in HDMI e premi `F11`.

## Struttura del codice

```
src/main.cpp            avvio GtkApplication, opzioni --self-test / --version
src/app_window.*        finestra principale, pagine, azioni, auto-test
src/pipeline.*          pipeline GStreamer: sorgente → tee → anteprima / registrazione / uscite
src/device_manager.*    enumerazione V4L2, DVB, audio (GstDeviceMonitor), hot-plug
src/v4l2_device.*       ioctl V4L2: formati, controlli, informazioni del dispositivo
src/dvb.*               adattatori DVB (FE_GET_INFO), lettura channels.conf, sistemi di trasmissione
src/settings.*          impostazioni persistenti (~/.config/webcam-tv-smart-vision/settings.ini)
data/                   .desktop, icona SVG, metainfo, manuale, changelog, regola udev e script C920
docs/                   guide PDF (Linux e Windows), sorgenti HTML delle guide, schermate
windows/                edizione Windows: sorgenti Win32/DirectShow, toolchain MinGW, script di build
packaging/              script per compilare il .deb in un container di un'altra distro
.github/workflows/      compilazione e pubblicazione dei .deb e degli zip Windows, con auto-test
```

## Licenza

GPL-3.0-or-later — © 2026 FabioNET
