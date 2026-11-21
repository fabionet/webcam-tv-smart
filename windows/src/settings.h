// Impostazioni persistenti in %APPDATA%\WebCamTVSmartVision\settings.ini
#pragma once
#include <string>

namespace sv {
class Settings {
public:
    void init(const std::wstring& dirOverride = L"");
    std::wstring get(const wchar_t* key, const std::wstring& def = L"") const;
    int getInt(const wchar_t* key, int def) const;
    void set(const wchar_t* key, const std::wstring& value);
    void setInt(const wchar_t* key, int value);
    const std::wstring& dir() const { return dir_; }
private:
    std::wstring dir_, path_;
};
}
