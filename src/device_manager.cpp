#include "device_manager.h"

#include <algorithm>
#include <cctype>

std::string VideoSource::label() const {
    switch (kind) {
        case SourceKind::Test: return "Sorgente di prova (mira + tono)";
        case SourceKind::Webcam: {
            std::string s = name + "  [" + id + "]";
            if (info.isLoopback) s += " (loopback)";
            return s;
        }
        case SourceKind::Dvb: return dvb.label();
        case SourceKind::Uri: return "Flusso di rete / file (URL)…";
    }
    return name;
}

DeviceManager::DeviceManager() {
    monitor_ = gst_device_monitor_new();
    gst_device_monitor_add_filter(monitor_, "Audio/Source", nullptr);
    gst_device_monitor_add_filter(monitor_, "Audio/Sink", nullptr);
    gst_device_monitor_add_filter(monitor_, "Video/Source", nullptr);
    GstBus* bus = gst_device_monitor_get_bus(monitor_);
    gst_bus_add_watch(bus, onMonitorMessage, this);
    gst_object_unref(bus);
    gst_device_monitor_start(monitor_);
    refresh();
}

DeviceManager::~DeviceManager() {
    if (refreshPending_) g_source_remove(refreshPending_);
    GstBus* bus = gst_device_monitor_get_bus(monitor_);
    gst_bus_remove_watch(bus);
    gst_object_unref(bus);
    gst_device_monitor_stop(monitor_);
    gst_object_unref(monitor_);
    for (auto& a : audioSrc_) if (a.device) gst_object_unref(a.device);
    for (auto& a : audioSink_) if (a.device) gst_object_unref(a.device);
}

gboolean DeviceManager::onMonitorMessage(GstBus*, GstMessage* msg, gpointer self) {
    auto* dm = static_cast<DeviceManager*>(self);
    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_DEVICE_ADDED || GST_MESSAGE_TYPE(msg) == GST_MESSAGE_DEVICE_REMOVED) {
        // Raggruppa gli eventi ravvicinati (una webcam genera più nodi /dev/video*).
        if (dm->refreshPending_) g_source_remove(dm->refreshPending_);
        dm->refreshPending_ = g_timeout_add(600, refreshIdle, dm);
    }
    return TRUE;
}

gboolean DeviceManager::refreshIdle(gpointer self) {
    auto* dm = static_cast<DeviceManager*>(self);
    dm->refreshPending_ = 0;
    dm->refresh();
    if (dm->onDevicesChanged) dm->onDevicesChanged();
    return G_SOURCE_REMOVE;
}

void DeviceManager::refresh() {
    video_.clear();
    outputs_.clear();
    VideoSource test;
    test.kind = SourceKind::Test;
    test.id = "test";
    test.name = "Sorgente di prova";
    video_.push_back(test);
    scanV4l2();
    scanDvb();
    VideoSource uri;
    uri.kind = SourceKind::Uri;
    uri.id = "uri";
    uri.name = "URL";
    video_.push_back(uri);
    scanAudio();
}

void DeviceManager::scanV4l2() {
    std::vector<std::string> paths;
    GDir* dir = g_dir_open("/dev", 0, nullptr);
    if (!dir) return;
    const gchar* e;
    while ((e = g_dir_read_name(dir)))
        if (g_str_has_prefix(e, "video")) paths.push_back(std::string("/dev/") + e);
    g_dir_close(dir);
    std::sort(paths.begin(), paths.end(), [](const std::string& a, const std::string& b) {
        return atoi(a.c_str() + 10) < atoi(b.c_str() + 10);
    });
    for (const auto& p : paths) {
        V4l2Info info;
        if (!V4l2Device::queryInfo(p, info)) continue;
        if (info.isOutput || info.isLoopback) {
            outputs_.push_back({ p, info.card + "  [" + p + "]", info.isLoopback });
        }
        if (info.isCapture && !info.isMetadata) {
            V4l2Device dev;
            if (!dev.open(p)) continue;
            VideoSource s;
            s.kind = SourceKind::Webcam;
            s.id = p;
            s.name = info.card;
            s.info = info;
            s.formats = dev.enumerateFormats();
            if (s.formats.empty() && !info.isLoopback) continue;   // nodo senza formati video (es. metadata UVC)
            video_.push_back(s);
        }
    }
}

void DeviceManager::scanDvb() {
    for (const auto& a : dvb::enumerateAdapters()) {
        VideoSource s;
        s.kind = SourceKind::Dvb;
        s.id = "dvb:" + std::to_string(a.adapter) + ":" + std::to_string(a.frontend);
        s.name = a.name;
        s.dvb = a;
        video_.push_back(s);
    }
}

void DeviceManager::scanAudio() {
    for (auto& a : audioSrc_) if (a.device) gst_object_unref(a.device);
    for (auto& a : audioSink_) if (a.device) gst_object_unref(a.device);
    audioSrc_.clear();
    audioSink_.clear();
    GList* devs = gst_device_monitor_get_devices(monitor_);
    for (GList* l = devs; l; l = l->next) {
        auto* d = GST_DEVICE(l->data);
        gchar* cls = gst_device_get_device_class(d);
        gchar* name = gst_device_get_display_name(d);
        bool isSrc = g_str_has_prefix(cls, "Audio/Source");
        bool isSink = g_str_has_prefix(cls, "Audio/Sink");
        if (isSrc || isSink) {
            AudioDevice a;
            a.name = name;
            a.isSink = isSink;
            a.device = GST_DEVICE(gst_object_ref(d));
            GstStructure* props = gst_device_get_properties(d);
            if (props) {
                const gchar* n = gst_structure_get_string(props, "node.name");
                if (!n) n = gst_structure_get_string(props, "device.name");
                if (!n) n = gst_structure_get_string(props, "alsa.card_name");
                a.id = n ? n : a.name;
                gst_structure_free(props);
            } else {
                a.id = a.name;
            }
            (isSink ? audioSink_ : audioSrc_).push_back(a);
        }
        g_free(cls);
        g_free(name);
    }
    g_list_free_full(devs, gst_object_unref);
    auto byName = [](const AudioDevice& x, const AudioDevice& y) { return x.name < y.name; };
    std::sort(audioSrc_.begin(), audioSrc_.end(), byName);
    std::sort(audioSink_.begin(), audioSink_.end(), byName);
}

static std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int DeviceManager::findMatchingAudioSource(const std::string& videoName) const {
    // Confronta i "token" significativi del nome video (es. "C920", "Brio", "Logitech")
    // con i nomi delle sorgenti audio: il microfono USB della webcam porta lo stesso nome.
    std::string v = lower(videoName);
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : v) {
        if (std::isalnum(static_cast<unsigned char>(c))) cur += c;
        else { if (cur.size() >= 3) tokens.push_back(cur); cur.clear(); }
    }
    if (cur.size() >= 3) tokens.push_back(cur);
    static const char* generic[] = { "usb", "camera", "webcam", "video", "audio", "hd", "pro", "device", "the" };
    int best = -1, bestScore = 0;
    for (size_t i = 0; i < audioSrc_.size(); ++i) {
        std::string a = lower(audioSrc_[i].name);
        int score = 0;
        for (const auto& t : tokens) {
            bool isGeneric = false;
            for (auto g : generic) if (t == g) isGeneric = true;
            if (!isGeneric && a.find(t) != std::string::npos) score += int(t.size());
        }
        if (score > bestScore) { bestScore = score; best = int(i); }
    }
    return bestScore >= 4 ? best : -1;
}
