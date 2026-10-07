#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Formato video esposto da un dispositivo V4L2 (webcam, scheda di acquisizione).
struct VideoFormat {
    std::string fourcc;     // "MJPG", "YUYV", "H264", ...
    std::string mediaType;  // "image/jpeg", "video/x-raw", "video/x-h264"
    std::string gstFormat;  // per video/x-raw: "YUY2", "NV12", ...
    int width = 0, height = 0;
    int fpsNum = 0, fpsDen = 1;

    bool valid() const { return width > 0 && height > 0; }
    std::string label() const;       // "MJPG 1920x1080 @ 30 fps"
    std::string capsString() const;  // caps GStreamer per capsfilter
    std::string key() const;         // chiave stabile per le impostazioni
};

// Controllo V4L2 (luminosità, contrasto, zoom, messa a fuoco, esposizione...).
struct V4l2Control {
    enum Type { Integer, Boolean, Menu, IntegerMenu, Button, Unknown };
    uint32_t id = 0;
    std::string name;
    Type type = Unknown;
    int64_t min = 0, max = 0, step = 1, def = 0, value = 0;
    bool inactive = false, readOnly = false;
    std::vector<std::pair<int64_t, std::string>> menuItems;
};

struct V4l2Info {
    std::string path, driver, card, busInfo;
    bool isCapture = false, isOutput = false, isLoopback = false, isMetadata = false;
};

class V4l2Device {
public:
    V4l2Device() = default;
    ~V4l2Device();
    V4l2Device(const V4l2Device&) = delete;
    V4l2Device& operator=(const V4l2Device&) = delete;

    bool open(const std::string& path);
    void close();
    bool isOpen() const { return fd_ >= 0; }
    const std::string& path() const { return path_; }

    static bool queryInfo(const std::string& path, V4l2Info& out);

    std::vector<VideoFormat> enumerateFormats() const;
    std::vector<V4l2Control> enumerateControls() const;
    bool getControl(uint32_t id, int64_t& value) const;
    bool setControl(uint32_t id, int64_t value);
    // Riporta tutti i controlli scrivibili al valore predefinito del driver.
    int resetControls();

private:
    int fd_ = -1;
    std::string path_;
};
