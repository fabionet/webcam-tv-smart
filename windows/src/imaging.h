// Salvataggio PNG tramite GDI+ (presente in Windows da XP in poi).
#pragma once
#include <windows.h>

#include <cstdint>
#include <string>

namespace sv {
bool imagingInit();
void imagingShutdown();
bool savePng(const std::wstring& path, const uint8_t* bgraTopDown, int w, int h, int stride);
bool saveWindowPng(HWND hwnd, const std::wstring& path);   // schermata della finestra (auto-test, guida)
}
