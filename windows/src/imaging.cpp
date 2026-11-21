// PNG con stb_image_write: nessuna dipendenza da GDI+ (che su Windows XP richiede un assembly side-by-side).
#include "imaging.h"

#include <vector>

#include "common.h"

extern "C" {
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
}

namespace sv {

bool imagingInit() { return true; }
void imagingShutdown() {}

static void pngWrite(void* ctx, void* data, int size) { static_cast<OutFile*>(ctx)->write(data, (size_t)size); }

bool savePng(const std::wstring& path, const uint8_t* bgra, int w, int h, int stride) {
    if (w <= 0 || h <= 0) return false;
    std::vector<uint8_t> rgb((size_t)w * h * 3);
    for (int y = 0; y < h; ++y) {
        const uint8_t* s = bgra + (size_t)y * stride;
        uint8_t* d = &rgb[(size_t)y * w * 3];
        for (int x = 0; x < w; ++x, s += 4, d += 3) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; }
    }
    OutFile f;
    if (!f.open(path)) return false;
    int ok = stbi_write_png_to_func(pngWrite, &f, w, h, 3, rgb.data(), w * 3);
    bool good = ok && f.ok();
    f.close();
    if (!good) DeleteFileW(path.c_str());
    return good;
}

bool saveWindowPng(HWND hwnd, const std::wstring& path) {
    RECT r;
    GetWindowRect(hwnd, &r);
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return false;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;                       // dall'alto verso il basso
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bm = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (bm && bits) {
        HGDIOBJ old = SelectObject(mem, bm);
        PatBlt(mem, 0, 0, w, h, WHITENESS);
        // 2 = PW_RENDERFULLCONTENT (Windows 8.1+): include il contenuto composto dal DWM; altrimenti modalità classica.
        if (!PrintWindow(hwnd, mem, 2)) PrintWindow(hwnd, mem, 0);
        GdiFlush();
        SelectObject(mem, old);
        ok = savePng(path, static_cast<const uint8_t*>(bits), w, h, w * 4);
    }
    if (bm) DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}

} // namespace sv
