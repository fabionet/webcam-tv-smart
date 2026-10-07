#include "v4l2_device.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <map>

namespace {

int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

std::string fourccToString(uint32_t f) {
    char s[5] = { char(f & 0xff), char((f >> 8) & 0xff), char((f >> 16) & 0xff), char((f >> 24) & 0xff), 0 };
    return s;
}

// Mappa fourcc V4L2 -> (media type, formato GStreamer)
const std::map<uint32_t, std::pair<const char*, const char*>>& fourccMap() {
    static const std::map<uint32_t, std::pair<const char*, const char*>> m = {
        { V4L2_PIX_FMT_MJPEG, { "image/jpeg", "" } },
        { V4L2_PIX_FMT_JPEG,  { "image/jpeg", "" } },
        { V4L2_PIX_FMT_H264,  { "video/x-h264", "" } },
        { V4L2_PIX_FMT_YUYV,  { "video/x-raw", "YUY2" } },
        { V4L2_PIX_FMT_UYVY,  { "video/x-raw", "UYVY" } },
        { V4L2_PIX_FMT_YVYU,  { "video/x-raw", "YVYU" } },
        { V4L2_PIX_FMT_NV12,  { "video/x-raw", "NV12" } },
        { V4L2_PIX_FMT_NV21,  { "video/x-raw", "NV21" } },
        { V4L2_PIX_FMT_YUV420,{ "video/x-raw", "I420" } },
        { V4L2_PIX_FMT_YVU420,{ "video/x-raw", "YV12" } },
        { V4L2_PIX_FMT_RGB24, { "video/x-raw", "RGB" } },
        { V4L2_PIX_FMT_BGR24, { "video/x-raw", "BGR" } },
        { V4L2_PIX_FMT_RGB32, { "video/x-raw", "xRGB" } },
        { V4L2_PIX_FMT_BGR32, { "video/x-raw", "BGRx" } },
        { V4L2_PIX_FMT_GREY,  { "video/x-raw", "GRAY8" } },
        { V4L2_PIX_FMT_RGB565,{ "video/x-raw", "RGB16" } },
    };
    return m;
}

} // namespace

std::string VideoFormat::label() const {
    std::string s = fourcc + " " + std::to_string(width) + "x" + std::to_string(height);
    if (fpsNum > 0) {
        double fps = double(fpsNum) / double(fpsDen ? fpsDen : 1);
        char b[32];
        snprintf(b, sizeof b, " @ %.4g fps", fps);
        s += b;
    }
    return s;
}

std::string VideoFormat::capsString() const {
    std::string s = mediaType;
    if (!gstFormat.empty()) s += ",format=" + gstFormat;
    s += ",width=" + std::to_string(width) + ",height=" + std::to_string(height);
    if (fpsNum > 0) s += ",framerate=" + std::to_string(fpsNum) + "/" + std::to_string(fpsDen ? fpsDen : 1);
    return s;
}

std::string VideoFormat::key() const {
    return fourcc + ":" + std::to_string(width) + "x" + std::to_string(height) + "@" +
           std::to_string(fpsNum) + "/" + std::to_string(fpsDen);
}

V4l2Device::~V4l2Device() { close(); }

bool V4l2Device::open(const std::string& path) {
    close();
    fd_ = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) return false;
    path_ = path;
    return true;
}

void V4l2Device::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    path_.clear();
}

bool V4l2Device::queryInfo(const std::string& path, V4l2Info& out) {
    int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    v4l2_capability cap {};
    bool ok = xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
    if (ok) {
        uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
        out.path = path;
        out.driver = reinterpret_cast<const char*>(cap.driver);
        out.card = reinterpret_cast<const char*>(cap.card);
        out.busInfo = reinterpret_cast<const char*>(cap.bus_info);
        out.isCapture = caps & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE);
        out.isOutput = caps & (V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_VIDEO_OUTPUT_MPLANE);
        out.isMetadata = (caps & V4L2_CAP_META_CAPTURE) && !out.isCapture;
        out.isLoopback = out.driver.find("loopback") != std::string::npos;
    }
    ::close(fd);
    return ok;
}

std::vector<VideoFormat> V4l2Device::enumerateFormats() const {
    std::vector<VideoFormat> out;
    if (fd_ < 0) return out;
    const auto& map = fourccMap();
    for (uint32_t i = 0;; ++i) {
        v4l2_fmtdesc fd {};
        fd.index = i;
        fd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(fd_, VIDIOC_ENUM_FMT, &fd) != 0) break;
        auto it = map.find(fd.pixelformat);
        if (it == map.end()) continue;
        for (uint32_t j = 0;; ++j) {
            v4l2_frmsizeenum fs {};
            fs.index = j;
            fs.pixel_format = fd.pixelformat;
            if (xioctl(fd_, VIDIOC_ENUM_FRAMESIZES, &fs) != 0) break;
            int w, h;
            if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                w = fs.discrete.width; h = fs.discrete.height;
            } else {
                // Formato a passo variabile: proponiamo solo la dimensione massima.
                w = fs.stepwise.max_width; h = fs.stepwise.max_height;
            }
            bool anyRate = false;
            for (uint32_t k = 0;; ++k) {
                v4l2_frmivalenum fi {};
                fi.index = k;
                fi.pixel_format = fd.pixelformat;
                fi.width = w; fi.height = h;
                if (xioctl(fd_, VIDIOC_ENUM_FRAMEINTERVALS, &fi) != 0) break;
                if (fi.type != V4L2_FRMIVAL_TYPE_DISCRETE) break;
                VideoFormat f;
                f.fourcc = fourccToString(fd.pixelformat);
                f.mediaType = it->second.first;
                f.gstFormat = it->second.second;
                f.width = w; f.height = h;
                f.fpsNum = fi.discrete.denominator;
                f.fpsDen = fi.discrete.numerator;
                out.push_back(f);
                anyRate = true;
            }
            if (!anyRate) {
                VideoFormat f;
                f.fourcc = fourccToString(fd.pixelformat);
                f.mediaType = it->second.first;
                f.gstFormat = it->second.second;
                f.width = w; f.height = h;
                out.push_back(f);
            }
            if (fs.type != V4L2_FRMSIZE_TYPE_DISCRETE) break;
        }
    }
    return out;
}

std::vector<V4l2Control> V4l2Device::enumerateControls() const {
    std::vector<V4l2Control> out;
    if (fd_ < 0) return out;
    v4l2_query_ext_ctrl q {};
    q.id = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
    while (xioctl(fd_, VIDIOC_QUERY_EXT_CTRL, &q) == 0) {
        uint32_t id = q.id;
        if (!(q.flags & V4L2_CTRL_FLAG_DISABLED) && q.type != V4L2_CTRL_TYPE_CTRL_CLASS) {
            V4l2Control c;
            c.id = id;
            c.name = q.name;
            c.min = q.minimum; c.max = q.maximum; c.step = q.step ? q.step : 1; c.def = q.default_value;
            c.inactive = q.flags & V4L2_CTRL_FLAG_INACTIVE;
            c.readOnly = q.flags & V4L2_CTRL_FLAG_READ_ONLY;
            switch (q.type) {
                case V4L2_CTRL_TYPE_INTEGER:
                case V4L2_CTRL_TYPE_INTEGER64: c.type = V4l2Control::Integer; break;
                case V4L2_CTRL_TYPE_BOOLEAN: c.type = V4l2Control::Boolean; break;
                case V4L2_CTRL_TYPE_MENU: c.type = V4l2Control::Menu; break;
                case V4L2_CTRL_TYPE_INTEGER_MENU: c.type = V4l2Control::IntegerMenu; break;
                case V4L2_CTRL_TYPE_BUTTON: c.type = V4l2Control::Button; break;
                default: c.type = V4l2Control::Unknown; break;
            }
            if (c.type == V4l2Control::Menu || c.type == V4l2Control::IntegerMenu) {
                for (int64_t i = c.min; i <= c.max; ++i) {
                    v4l2_querymenu m {};
                    m.id = id; m.index = uint32_t(i);
                    if (xioctl(fd_, VIDIOC_QUERYMENU, &m) != 0) continue;
                    std::string label = c.type == V4l2Control::Menu
                        ? std::string(reinterpret_cast<const char*>(m.name))
                        : std::to_string(m.value);
                    c.menuItems.emplace_back(i, label);
                }
            }
            if (c.type != V4l2Control::Unknown && c.type != V4l2Control::Button) getControl(id, c.value);
            if (c.type != V4l2Control::Unknown) out.push_back(c);
        }
        q.id = id | V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
    }
    return out;
}

bool V4l2Device::getControl(uint32_t id, int64_t& value) const {
    if (fd_ < 0) return false;
    v4l2_control c {};
    c.id = id;
    if (xioctl(fd_, VIDIOC_G_CTRL, &c) == 0) {   // controlli a 32 bit (la quasi totalità)
        value = c.value;
        return true;
    }
    v4l2_ext_control ec {};                      // controlli INTEGER64
    ec.id = id;
    v4l2_ext_controls ecs {};
    ecs.which = V4L2_CTRL_WHICH_CUR_VAL;
    ecs.count = 1;
    ecs.controls = &ec;
    if (xioctl(fd_, VIDIOC_G_EXT_CTRLS, &ecs) != 0) return false;
    value = ec.value64;
    return true;
}

bool V4l2Device::setControl(uint32_t id, int64_t value) {
    if (fd_ < 0) return false;
    v4l2_control c {};
    c.id = id;
    c.value = int32_t(value);
    if (xioctl(fd_, VIDIOC_S_CTRL, &c) == 0) return true;
    v4l2_ext_control ec {};
    ec.id = id;
    ec.value64 = value;
    v4l2_ext_controls ecs {};
    ecs.which = V4L2_CTRL_WHICH_CUR_VAL;
    ecs.count = 1;
    ecs.controls = &ec;
    return xioctl(fd_, VIDIOC_S_EXT_CTRLS, &ecs) == 0;
}

int V4l2Device::resetControls() {
    int n = 0;
    auto ctrls = enumerateControls();
    // Prima i controlli "auto" (menu/booleani), poi i valori manuali, così i valori
    // non vengono rifiutati perché il controllo è temporaneamente inattivo.
    for (auto& c : ctrls)
        if (!c.readOnly && (c.type == V4l2Control::Boolean || c.type == V4l2Control::Menu) && setControl(c.id, c.def)) ++n;
    for (auto& c : ctrls)
        if (!c.readOnly && c.type == V4l2Control::Integer && setControl(c.id, c.def)) ++n;
    return n;
}
