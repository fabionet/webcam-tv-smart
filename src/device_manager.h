#pragma once
#include <gst/gst.h>

#include <functional>
#include <string>
#include <vector>

#include "dvb.h"
#include "v4l2_device.h"

enum class SourceKind { Test, Webcam, Dvb, Uri };

struct VideoSource {
    SourceKind kind = SourceKind::Test;
    std::string id;          // chiave stabile: "test", "/dev/video0", "dvb:0:0", "uri"
    std::string name;
    V4l2Info info;           // solo Webcam
    std::vector<VideoFormat> formats;
    DvbAdapter dvb;          // solo Dvb
    std::string label() const;
};

struct AudioDevice {
    std::string name;
    std::string id;          // proprietà "device"/"target-object" o nome
    GstDevice* device = nullptr;   // riferimento posseduto
    bool isSink = false;
};

struct OutputDevice {
    std::string path;        // /dev/videoN (v4l2loopback o uscita hardware)
    std::string name;
    bool isLoopback = false;
};

// Enumera sorgenti video (V4L2 + DVB), dispositivi audio (via GstDeviceMonitor)
// e uscite video V4L2, con notifica hot-plug.
class DeviceManager {
public:
    DeviceManager();
    ~DeviceManager();

    void refresh();

    const std::vector<VideoSource>& videoSources() const { return video_; }
    const std::vector<AudioDevice>& audioSources() const { return audioSrc_; }
    const std::vector<AudioDevice>& audioSinks() const { return audioSink_; }
    const std::vector<OutputDevice>& videoOutputs() const { return outputs_; }

    // Indice della sorgente audio che "appartiene" alla webcam (es. microfono della C920), -1 se nessuna.
    int findMatchingAudioSource(const std::string& videoName) const;

    std::function<void()> onDevicesChanged;   // chiamata nel thread principale

private:
    void scanV4l2();
    void scanDvb();
    void scanAudio();
    static gboolean onMonitorMessage(GstBus* bus, GstMessage* msg, gpointer self);
    static gboolean refreshIdle(gpointer self);

    GstDeviceMonitor* monitor_ = nullptr;
    guint refreshPending_ = 0;
    std::vector<VideoSource> video_;
    std::vector<AudioDevice> audioSrc_, audioSink_;
    std::vector<OutputDevice> outputs_;
};
