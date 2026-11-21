#include "recorder.h"

#include <cmath>
#include <cstring>

namespace sv {

void bgraToI420(const uint8_t* bgra, int stride, int cropX, int cropY, int w, int h,
                uint8_t* y, uint8_t* u, uint8_t* v, bool bt709) {
    // Coefficienti in virgola fissa (x256), gamma limitata 16-235.
    const int yr = bt709 ? 47 : 66, yg = bt709 ? 157 : 129, yb = bt709 ? 16 : 25;
    const int ur = bt709 ? -26 : -38, ug = bt709 ? -87 : -74, ub = 112;
    const int vr = 112, vg = bt709 ? -102 : -94, vb = bt709 ? -10 : -18;
    for (int j = 0; j < h; j += 2) {
        const uint8_t* s0 = bgra + (size_t)(cropY + j) * stride + (size_t)cropX * 4;
        const uint8_t* s1 = s0 + stride;
        uint8_t* y0 = y + (size_t)j * w;
        uint8_t* y1 = y0 + w;
        uint8_t* uu = u + (size_t)(j / 2) * (w / 2);
        uint8_t* vv = v + (size_t)(j / 2) * (w / 2);
        for (int i = 0; i < w; i += 2) {
            int b00 = s0[0], g00 = s0[1], r00 = s0[2], b01 = s0[4], g01 = s0[5], r01 = s0[6];
            int b10 = s1[0], g10 = s1[1], r10 = s1[2], b11 = s1[4], g11 = s1[5], r11 = s1[6];
            y0[0] = uint8_t(((yr * r00 + yg * g00 + yb * b00 + 128) >> 8) + 16);
            y0[1] = uint8_t(((yr * r01 + yg * g01 + yb * b01 + 128) >> 8) + 16);
            y1[0] = uint8_t(((yr * r10 + yg * g10 + yb * b10 + 128) >> 8) + 16);
            y1[1] = uint8_t(((yr * r11 + yg * g11 + yb * b11 + 128) >> 8) + 16);
            int r = (r00 + r01 + r10 + r11 + 2) >> 2, g = (g00 + g01 + g10 + g11 + 2) >> 2, b = (b00 + b01 + b10 + b11 + 2) >> 2;
            *uu++ = uint8_t(((ur * r + ug * g + ub * b + 128) >> 8) + 128);
            *vv++ = uint8_t(((vr * r + vg * g + vb * b + 128) >> 8) + 128);
            s0 += 8; s1 += 8; y0 += 2; y1 += 2;
        }
    }
}

Recorder::Recorder() { wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); }

Recorder::~Recorder() {
    stop();
    CloseHandle(wake_);
}

bool Recorder::start(const std::wstring& path, int srcW, int srcH, const RecOptions& opt, std::wstring& err) {
    if (active_) { err = L"Registrazione già in corso."; return false; }
    if (srcW < 32 || srcH < 32) { err = L"Dimensione video non valida."; return false; }
    opt_ = opt;
    if (opt_.fps < 1 || opt_.fps > 120) opt_.fps = 30;
    srcW_ = srcW; srcH_ = srcH;
    // I codificatori lavorano a macroblocchi di 16x16: ritaglio centrato al multiplo di 16.
    w_ = srcW & ~15; h_ = srcH & ~15;
    cropX_ = ((srcW - w_) / 2) & ~1; cropY_ = ((srcH - h_) / 2) & ~1;
    bt709_ = opt_.format == RecFormat::Mp4 && h_ >= 720;

    writer_ = opt_.format == RecFormat::Mp4 ? createMp4Writer() : createOggWriter();
    if (!writer_->open(path, w_, h_, opt_.fps, opt_.videoKbps, opt_.audio, opt_.audioRate, opt_.audioKbps, err)) {
        delete writer_;
        writer_ = nullptr;
        DeleteFileW(path.c_str());
        return false;
    }
    {
        Guard g(lock_);                                  // eventuali residui di una registrazione precedente
        for (Item* it : queue_) { alignedFree(it->planes); delete it; }
        queue_.clear();
        queuedVideo_ = 0;
    }
    t0_ = -1; lastSlot_ = -1; audioStarted_ = false;
    framesWritten_ = 0; framesDropped_ = 0;
    failed_ = false; stopping_ = false;
    startWall_ = nowSeconds();
    active_ = true;
    thread_ = CreateThread(nullptr, 0, threadProc, this, 0, nullptr);
    SetThreadPriority(thread_, THREAD_PRIORITY_BELOW_NORMAL);
    return true;
}

void Recorder::stop() {
    if (!thread_) return;
    {
        Guard g(lock_);        // dopo questo punto pushVideo/pushAudio non accodano più nulla
        active_ = false;
    }
    stopping_ = true;
    SetEvent(wake_);
    WaitForSingleObject(thread_, INFINITE);
    CloseHandle(thread_);
    thread_ = nullptr;
    for (Item* it : queue_) { alignedFree(it->planes); delete it; }
    queue_.clear();
    if (writer_) {
        writer_->close();
        delete writer_;
        writer_ = nullptr;
    }
}

double Recorder::elapsed() const { return active_ ? nowSeconds() - startWall_ : 0; }

void Recorder::pushVideo(const uint8_t* bgra, int w, int h, int stride, double t) {
    if (!active_ || w != srcW_ || h != srcH_) return;
    if (t0_ < 0) t0_ = t;
    long slot = (long)std::floor((t - t0_) * opt_.fps + 0.5);
    if (slot <= lastSlot_) return;                       // sorgente più veloce del frame rate scelto
    {
        Guard g(lock_);
        if (queuedVideo_ >= 12) {                        // codificatore in ritardo: salta il fotogramma
            InterlockedIncrement(&framesDropped_);
            return;
        }
    }
    Item* it = new Item;
    it->type = 0;
    it->slots = (int)std::min<long>(slot - lastSlot_, (long)(opt_.fps * 5));
    size_t ysz = (size_t)w_ * h_;
    it->planes = (uint8_t*)alignedAlloc(ysz + ysz / 2 + 64);
    if (!it->planes) { delete it; return; }
    bgraToI420(bgra, stride, cropX_, cropY_, w_, h_, it->planes, it->planes + ysz, it->planes + ysz + ysz / 4, bt709_);
    lastSlot_ = slot;
    {
        Guard g(lock_);
        if (!active_) { alignedFree(it->planes); delete it; return; }
        queue_.push_back(it);
        ++queuedVideo_;
    }
    SetEvent(wake_);
}

void Recorder::pushAudio(const int16_t* stereo, int frames, double tEnd) {
    if (!active_ || !opt_.audio || t0_ < 0 || frames <= 0) return;   // l'audio parte col primo fotogramma
    Item* it = new Item;
    it->type = 1;
    if (!audioStarted_) {
        // Allinea l'inizio dell'audio al primo fotogramma: scarta l'eccesso o inserisce silenzio.
        double tStart = tEnd - double(frames) / opt_.audioRate;
        long skew = (long)std::floor((tStart - t0_) * opt_.audioRate + 0.5);
        audioStarted_ = true;
        if (skew < 0) {
            long cut = std::min<long>(-skew, frames);
            stereo += cut * 2;
            frames -= (int)cut;
        } else if (skew > 0) {
            it->pcm.assign((size_t)std::min<long>(skew, opt_.audioRate) * 2, 0);
        }
    }
    it->pcm.insert(it->pcm.end(), stereo, stereo + (size_t)frames * 2);
    {
        Guard g(lock_);
        if (!active_) { delete it; return; }
        queue_.push_back(it);
    }
    SetEvent(wake_);
}

DWORD WINAPI Recorder::threadProc(LPVOID self) {
    static_cast<Recorder*>(self)->run();
    return 0;
}

void Recorder::run() {
    for (;;) {
        Item* it = nullptr;
        {
            Guard g(lock_);
            if (!queue_.empty()) {
                it = queue_.front();
                queue_.pop_front();
                if (it->type == 0) --queuedVideo_;
            }
        }
        if (!it) {
            if (stopping_) break;
            WaitForSingleObject(wake_, 50);
            continue;
        }
        if (!failed_) {
            bool ok;
            if (it->type == 0) {
                size_t ysz = (size_t)w_ * h_;
                VideoPlanes p = { it->planes, it->planes + ysz, it->planes + ysz + ysz / 4, w_, h_ };
                ok = writer_->writeVideo(p, it->slots);
                if (ok) InterlockedIncrement(&framesWritten_);
            } else {
                ok = writer_->writeAudio(it->pcm.data(), (int)(it->pcm.size() / 2));
            }
            if (!ok) failed_ = true;                     // es. disco pieno: l'interfaccia ferma la registrazione
        }
        alignedFree(it->planes);
        delete it;
    }
}

} // namespace sv
