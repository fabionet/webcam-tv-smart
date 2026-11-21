#include "settings.h"

#include <shlobj.h>
#include <windows.h>

#include "common.h"

namespace sv {

static const wchar_t* kSection = L"SmartVision";

void Settings::init(const std::wstring& dirOverride) {
    if (!dirOverride.empty()) {
        dir_ = dirOverride;
    } else {
        wchar_t buf[MAX_PATH] = L"";
        if (SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr, 0, buf) != S_OK) GetTempPathW(MAX_PATH, buf);
        dir_ = std::wstring(buf) + L"\\WebCamTVSmartVision";
    }
    ensureDir(dir_);
    path_ = dir_ + L"\\settings.ini";
}

std::wstring Settings::get(const wchar_t* key, const std::wstring& def) const {
    wchar_t buf[1024];
    GetPrivateProfileStringW(kSection, key, def.c_str(), buf, 1024, path_.c_str());
    return buf;
}

int Settings::getInt(const wchar_t* key, int def) const {
    return (int)GetPrivateProfileIntW(kSection, key, def, path_.c_str());
}

void Settings::set(const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(kSection, key, value.c_str(), path_.c_str());
}

void Settings::setInt(const wchar_t* key, int value) { set(key, format(L"%d", value)); }

} // namespace sv
