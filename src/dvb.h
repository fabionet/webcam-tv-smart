#pragma once
#include <string>
#include <vector>

// Adattatore DVB (scheda TV digitale terrestre / satellitare / cavo) in /dev/dvb/adapterN.
struct DvbAdapter {
    int adapter = 0;
    int frontend = 0;
    std::string name;                  // dal frontend (FE_GET_INFO)
    std::vector<std::string> delsys;   // "DVB-T", "DVB-T2", "DVB-S2", ...
    std::string label() const;
};

struct DvbChannel {
    std::string name;
};

namespace dvb {
std::vector<DvbAdapter> enumerateAdapters();
// Legge un channels.conf sia in formato "zap" (NOME:freq:...) sia in formato DVBv5 ([NOME]).
std::vector<DvbChannel> parseChannelsConf(const std::string& path);
// Percorso predefinito usato da GStreamer (dvbbasebin): ~/.config/gstreamer-1.0/dvb-channels.conf
std::string defaultChannelsConfPath();
// Cartelle con i file di scansione iniziale (dvbv5-scan): /usr/share/dvbv5, /usr/share/dvb
std::vector<std::string> initialScanDirs();
// Valori accettati dalla proprietà "delsys" di dvbsrc, con etichetta.
std::vector<std::pair<std::string, std::string>> deliverySystems();
}
