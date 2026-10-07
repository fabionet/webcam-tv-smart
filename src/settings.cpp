#include "settings.h"
#include <glib/gstdio.h>

Settings::Settings() {
    dir_ = std::string(g_get_user_config_dir()) + "/webcam-tv-smart-vision";
    g_mkdir_with_parents(dir_.c_str(), 0700);
    path_ = dir_ + "/settings.ini";
    kf_ = g_key_file_new();
    g_key_file_load_from_file(kf_, path_.c_str(), G_KEY_FILE_NONE, nullptr);
}

Settings::~Settings() {
    save();
    g_key_file_free(kf_);
}

std::string Settings::getString(const char* key, const std::string& def) const {
    gchar* v = g_key_file_get_string(kf_, GROUP, key, nullptr);
    if (!v) return def;
    std::string r(v);
    g_free(v);
    return r;
}

int Settings::getInt(const char* key, int def) const {
    GError* err = nullptr;
    int v = g_key_file_get_integer(kf_, GROUP, key, &err);
    if (err) { g_error_free(err); return def; }
    return v;
}

bool Settings::getBool(const char* key, bool def) const {
    GError* err = nullptr;
    gboolean v = g_key_file_get_boolean(kf_, GROUP, key, &err);
    if (err) { g_error_free(err); return def; }
    return v;
}

double Settings::getDouble(const char* key, double def) const {
    GError* err = nullptr;
    double v = g_key_file_get_double(kf_, GROUP, key, &err);
    if (err) { g_error_free(err); return def; }
    return v;
}

void Settings::set(const char* key, const std::string& v) { g_key_file_set_string(kf_, GROUP, key, v.c_str()); }
void Settings::set(const char* key, int v) { g_key_file_set_integer(kf_, GROUP, key, v); }
void Settings::set(const char* key, bool v) { g_key_file_set_boolean(kf_, GROUP, key, v); }
void Settings::set(const char* key, double v) { g_key_file_set_double(kf_, GROUP, key, v); }

void Settings::save() {
    g_key_file_save_to_file(kf_, path_.c_str(), nullptr);
}
