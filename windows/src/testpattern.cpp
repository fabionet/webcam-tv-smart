#include "testpattern.h"

#include <cmath>
#include <cstring>

namespace sv {

namespace {
inline void put(uint8_t* p, uint32_t rgb) { p[0] = uint8_t(rgb); p[1] = uint8_t(rgb >> 8); p[2] = uint8_t(rgb >> 16); p[3] = 255; }

void fillRect(uint8_t* bgra, int w, int h, int x0, int y0, int x1, int y1, uint32_t rgb) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (int y = y0; y < y1; ++y) {
        uint8_t* p = bgra + ((size_t)y * w + x0) * 4;
        for (int x = x0; x < x1; ++x, p += 4) put(p, rgb);
    }
}

// Cifre 3x5 per l'orologio (un bit per pixel, dall'alto).
const uint8_t kDigits[11][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},{0,2,0,2,0}
};

void drawText(uint8_t* bgra, int w, int h, int x, int y, int scale, const char* s, uint32_t rgb) {
    for (; *s; ++s) {
        int d = *s == ':' ? 10 : (*s >= '0' && *s <= '9') ? *s - '0' : -1;
        if (d >= 0)
            for (int r = 0; r < 5; ++r)
                for (int c = 0; c < 3; ++c)
                    if (kDigits[d][r] & (4 >> c)) fillRect(bgra, w, h, x + c * scale, y + r * scale, x + (c + 1) * scale, y + (r + 1) * scale, rgb);
        x += 4 * scale;
    }
}
} // namespace

void renderTestPattern(uint8_t* bgra, int w, int h, long frameNo, double seconds) {
    static const uint32_t bars[7] = { 0xC0C0C0, 0xC0C000, 0x00C0C0, 0x00C000, 0xC000C0, 0xC00000, 0x0000C0 };
    static const uint32_t low[7] = { 0x0000C0, 0x131313, 0xC000C0, 0x131313, 0x00C0C0, 0x131313, 0xC0C0C0 };
    int hBars = h * 2 / 3, hLow = h * 3 / 4;
    for (int i = 0; i < 7; ++i) {
        int x0 = w * i / 7, x1 = w * (i + 1) / 7;
        fillRect(bgra, w, h, x0, 0, x1, hBars, bars[i]);
        fillRect(bgra, w, h, x0, hBars, x1, hLow, low[i]);
    }
    // fascia inferiore: scala di grigi a gradini
    for (int i = 0; i < 16; ++i) {
        uint32_t g = (uint32_t)(i * 17);
        fillRect(bgra, w, h, w * i / 16, hLow, w * (i + 1) / 16, h, (g << 16) | (g << 8) | g);
    }
    // riquadro bianco che scorre: rende visibile il movimento (e i fotogrammi saltati)
    int box = h / 8;
    int span = w - box;
    long pos = (frameNo * (w / 160 + 1)) % (2 * span);
    if (pos > span) pos = 2 * span - pos;
    fillRect(bgra, w, h, (int)pos, hBars - box - h / 30, (int)pos + box, hBars - h / 30, 0xFFFFFF);
    // orologio hh:mm:ss:ff
    int total = (int)seconds;
    char buf[32];
    int ff = (int)((seconds - total) * 100);
    buf[0] = char('0' + (total / 3600) % 10); buf[1] = ':';
    buf[2] = char('0' + (total / 60 % 60) / 10); buf[3] = char('0' + (total / 60 % 60) % 10); buf[4] = ':';
    buf[5] = char('0' + (total % 60) / 10); buf[6] = char('0' + (total % 60) % 10); buf[7] = ':';
    buf[8] = char('0' + ff / 10); buf[9] = char('0' + ff % 10); buf[10] = 0;
    int scale = h / 90 + 1;
    int tw = 10 * 4 * scale, th = 5 * scale;
    int tx = w - tw - scale * 4, ty = h - th - scale * 4;
    fillRect(bgra, w, h, tx - scale * 2, ty - scale * 2, w - scale * 2, h - scale * 2, 0x000000);
    drawText(bgra, w, h, tx, ty, scale, buf, 0xFFFFFF);
}

void renderTestTone(int16_t* stereo, int frames, int rate, double& phase) {
    const double step = 2.0 * 3.14159265358979323846 * 440.0 / rate;
    for (int i = 0; i < frames; ++i) {
        int16_t s = (int16_t)(std::sin(phase) * 1600.0);       // circa -26 dBFS
        stereo[i * 2] = s;
        stereo[i * 2 + 1] = s;
        phase += step;
        if (phase > 6.283185307179586) phase -= 6.283185307179586;
    }
}

} // namespace sv
