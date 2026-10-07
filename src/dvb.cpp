#include "dvb.h"

#include <fcntl.h>
#include <linux/dvb/frontend.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <glib.h>

#include <fstream>

namespace {
const char* delsysName(unsigned v) {
    switch (v) {
        case SYS_DVBT: return "DVB-T";
        case SYS_DVBT2: return "DVB-T2";
        case SYS_DVBS: return "DVB-S";
        case SYS_DVBS2: return "DVB-S2";
        case SYS_DVBC_ANNEX_A: return "DVB-C";
        case SYS_DVBC_ANNEX_B: return "DVB-C/B";
        case SYS_DVBC_ANNEX_C: return "DVB-C/C";
        case SYS_ATSC: return "ATSC";
        case SYS_ISDBT: return "ISDB-T";
        case SYS_DTMB: return "DTMB";
        case SYS_TURBO: return "Turbo";
        default: return nullptr;
    }
}
}

std::string DvbAdapter::label() const {
    std::string s = "TV: " + (name.empty() ? "adapter" + std::to_string(adapter) : name);
    if (!delsys.empty()) {
        s += " (";
        for (size_t i = 0; i < delsys.size(); ++i) s += (i ? ", " : "") + delsys[i];
        s += ")";
    }
    return s;
}

std::vector<DvbAdapter> dvb::enumerateAdapters() {
    std::vector<DvbAdapter> out;
    GDir* dir = g_dir_open("/dev/dvb", 0, nullptr);
    if (!dir) return out;
    const gchar* e;
    while ((e = g_dir_read_name(dir))) {
        if (!g_str_has_prefix(e, "adapter")) continue;
        int adapter = atoi(e + 7);
        for (int fe = 0; fe < 4; ++fe) {
            std::string path = "/dev/dvb/" + std::string(e) + "/frontend" + std::to_string(fe);
            int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) break;
            DvbAdapter a;
            a.adapter = adapter;
            a.frontend = fe;
            dvb_frontend_info info {};
            if (ioctl(fd, FE_GET_INFO, &info) == 0) a.name = info.name;
            dtv_property prop {};
            prop.cmd = DTV_ENUM_DELSYS;
            dtv_properties props { 1, &prop };
            if (ioctl(fd, FE_GET_PROPERTY, &props) == 0) {
                for (unsigned i = 0; i < prop.u.buffer.len; ++i)
                    if (const char* n = delsysName(prop.u.buffer.data[i])) a.delsys.push_back(n);
            }
            ::close(fd);
            out.push_back(a);
        }
    }
    g_dir_close(dir);
    return out;
}

std::vector<DvbChannel> dvb::parseChannelsConf(const std::string& path) {
    std::vector<DvbChannel> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {                     // formato DVBv5
            auto end = line.find(']');
            if (end != std::string::npos) out.push_back({ line.substr(1, end - 1) });
        } else {                                  // formato zap/VDR: NOME:...
            auto sep = line.find(':');
            if (sep != std::string::npos && sep > 0) {
                std::string name = line.substr(0, sep);
                // VDR usa "Nome;Provider" come primo campo
                auto semi = name.find(';');
                if (semi != std::string::npos) name = name.substr(0, semi);
                out.push_back({ name });
            }
        }
    }
    return out;
}

std::string dvb::defaultChannelsConfPath() {
    const char* env = g_getenv("GST_DVB_CHANNELS_CONF");
    if (env && *env) return env;
    return std::string(g_get_user_config_dir()) + "/gstreamer-1.0/dvb-channels.conf";
}

std::vector<std::string> dvb::initialScanDirs() {
    std::vector<std::string> out;
    for (const char* d : { "/usr/share/dvbv5", "/usr/share/dvb", "/usr/local/share/dvbv5" })
        if (g_file_test(d, G_FILE_TEST_IS_DIR)) out.push_back(d);
    return out;
}

std::vector<std::pair<std::string, std::string>> dvb::deliverySystems() {
    return {
        { "dvb-t", "DVB-T (terrestre)" },
        { "dvb-t2", "DVB-T2 (terrestre HD)" },
        { "dvb-c-a", "DVB-C (cavo)" },
        { "dvb-s", "DVB-S (satellite)" },
        { "dvb-s2", "DVB-S2 (satellite HD)" },
        { "atsc", "ATSC" },
        { "isdb-t", "ISDB-T" },
    };
}
