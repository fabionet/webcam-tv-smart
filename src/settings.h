#pragma once
#include <glib.h>
#include <string>

// Persistenza semplice in ~/.config/webcam-tv-smart-vision/settings.ini (GKeyFile).
class Settings {
public:
    Settings();
    ~Settings();

    std::string getString(const char* key, const std::string& def) const;
    int getInt(const char* key, int def) const;
    bool getBool(const char* key, bool def) const;
    double getDouble(const char* key, double def) const;

    void set(const char* key, const std::string& v);
    void set(const char* key, const char* v) { set(key, std::string(v)); }
    void set(const char* key, int v);
    void set(const char* key, bool v);
    void set(const char* key, double v);

    void save();
    const std::string& configDir() const { return dir_; }

private:
    static constexpr const char* GROUP = "SmartVision";
    GKeyFile* kf_ = nullptr;
    std::string dir_, path_;
};
