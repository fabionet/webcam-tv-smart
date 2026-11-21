// Sorgente di prova: barre colore con orologio e riquadro in movimento, tono a 440 Hz.
#pragma once
#include <cstdint>

namespace sv {
// bgra: buffer w*h*4 dall'alto verso il basso. frameNo e seconds animano l'immagine.
void renderTestPattern(uint8_t* bgra, int w, int h, long frameNo, double seconds);
// Genera "frames" campioni stereo a 16 bit di un tono a 440 Hz; phase viene aggiornata.
void renderTestTone(int16_t* stereo, int frames, int rate, double& phase);
}
