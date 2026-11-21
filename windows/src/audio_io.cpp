#include "audio_io.h"

#include <cstring>

namespace sv {

std::vector<AudioDeviceInfo> listAudioInputs() {
    std::vector<AudioDeviceInfo> out;
    UINT n = waveInGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        WAVEINCAPSW caps;
        if (waveInGetDevCapsW(i, &caps, sizeof caps) == MMSYSERR_NOERROR) out.push_back({ (int)i, caps.szPname });
    }
    return out;
}

std::vector<AudioDeviceInfo> listAudioOutputs() {
    std::vector<AudioDeviceInfo> out;
    UINT n = waveOutGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        WAVEOUTCAPSW caps;
        if (waveOutGetDevCapsW(i, &caps, sizeof caps) == MMSYSERR_NOERROR) out.push_back({ (int)i, caps.szPname });
    }
    return out;
}

static WAVEFORMATEX pcmFormat(int rate, int channels) {
    WAVEFORMATEX f;
    memset(&f, 0, sizeof f);
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = (WORD)channels;
    f.nSamplesPerSec = rate;
    f.wBitsPerSample = 16;
    f.nBlockAlign = (WORD)(channels * 2);
    f.nAvgBytesPerSec = rate * f.nBlockAlign;
    return f;
}

bool AudioIn::open(int deviceId, AudioCallback cb, void* ctx) {
    close();
    cb_ = cb; ctx_ = ctx;
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    UINT dev = deviceId < 0 ? WAVE_MAPPER : (UINT)deviceId;
    // Preferenza: 48 kHz stereo; WAVE_MAPPED lascia a Windows la conversione di formato anche su XP.
    static const int tries[4][2] = { { 48000, 2 }, { 44100, 2 }, { 48000, 1 }, { 44100, 1 } };
    for (int pass = 0; pass < 2 && !h_; ++pass) {
        for (int i = 0; i < 4 && !h_; ++i) {
            WAVEFORMATEX f = pcmFormat(tries[i][0], tries[i][1]);
            DWORD flags = CALLBACK_EVENT | (pass == 0 && dev != WAVE_MAPPER ? WAVE_MAPPED : 0);
            if (waveInOpen(&h_, dev, &f, (DWORD_PTR)event_, 0, flags) == MMSYSERR_NOERROR) {
                rate_ = tries[i][0];
                channels_ = tries[i][1];
            } else {
                h_ = nullptr;
            }
        }
    }
    if (!h_) {
        CloseHandle(event_);
        event_ = nullptr;
        return false;
    }
    int frames = rate_ / 50;                              // blocchi da 20 ms
    for (int i = 0; i < 4; ++i) {
        buf_[i].assign((size_t)frames * channels_ * 2, 0);
        memset(&hdr_[i], 0, sizeof(WAVEHDR));
        hdr_[i].lpData = buf_[i].data();
        hdr_[i].dwBufferLength = (DWORD)buf_[i].size();
        waveInPrepareHeader(h_, &hdr_[i], sizeof(WAVEHDR));
        waveInAddBuffer(h_, &hdr_[i], sizeof(WAVEHDR));
    }
    stopping_ = false;
    thread_ = CreateThread(nullptr, 0, threadProc, this, 0, nullptr);
    SetThreadPriority(thread_, THREAD_PRIORITY_ABOVE_NORMAL);
    waveInStart(h_);
    return true;
}

void AudioIn::close() {
    if (!h_) return;
    stopping_ = true;
    waveInReset(h_);
    SetEvent(event_);
    WaitForSingleObject(thread_, 3000);
    CloseHandle(thread_);
    thread_ = nullptr;
    for (int i = 0; i < 4; ++i) waveInUnprepareHeader(h_, &hdr_[i], sizeof(WAVEHDR));
    waveInClose(h_);
    h_ = nullptr;
    CloseHandle(event_);
    event_ = nullptr;
}

DWORD WINAPI AudioIn::threadProc(LPVOID self) {
    static_cast<AudioIn*>(self)->run();
    return 0;
}

void AudioIn::run() {
    while (!stopping_) {
        WaitForSingleObject(event_, 200);
        for (int i = 0; i < 4 && !stopping_; ++i) {
            WAVEHDR& h = hdr_[i];
            if (!(h.dwFlags & WHDR_DONE)) continue;
            int frames = (int)(h.dwBytesRecorded / (channels_ * 2));
            if (frames > 0 && cb_) {
                const int16_t* src = reinterpret_cast<const int16_t*>(h.lpData);
                if (channels_ == 1) {                     // mono -> stereo
                    conv_.resize((size_t)frames * 2);
                    for (int k = 0; k < frames; ++k) conv_[k * 2] = conv_[k * 2 + 1] = src[k];
                    src = conv_.data();
                }
                cb_(ctx_, src, frames, nowSeconds());
            }
            h.dwFlags &= ~WHDR_DONE;
            h.dwBytesRecorded = 0;
            if (!stopping_) waveInAddBuffer(h_, &h, sizeof(WAVEHDR));
        }
    }
}

bool AudioOut::open(int deviceId, int rate) {
    close();
    WAVEFORMATEX f = pcmFormat(rate, 2);
    UINT dev = deviceId < 0 ? WAVE_MAPPER : (UINT)deviceId;
    if (waveOutOpen(&h_, dev, &f, 0, 0, CALLBACK_NULL | (dev != WAVE_MAPPER ? WAVE_MAPPED : 0)) != MMSYSERR_NOERROR &&
        waveOutOpen(&h_, dev, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        h_ = nullptr;
        return false;
    }
    for (int i = 0; i < kBuffers; ++i) {
        buf_[i].assign(kMaxFrames * 2, 0);
        memset(&hdr_[i], 0, sizeof(WAVEHDR));
        used_[i] = false;
    }
    return true;
}

void AudioOut::close() {
    Guard g(lock_);
    if (!h_) return;
    waveOutReset(h_);
    for (int i = 0; i < kBuffers; ++i)
        if (used_[i]) waveOutUnprepareHeader(h_, &hdr_[i], sizeof(WAVEHDR));
    waveOutClose(h_);
    h_ = nullptr;
}

void AudioOut::write(const int16_t* stereo, int frames) {
    Guard g(lock_);
    if (!h_ || frames <= 0) return;
    if (frames > kMaxFrames) frames = kMaxFrames;
    for (int i = 0; i < kBuffers; ++i) {
        if (used_[i]) {
            if (!(hdr_[i].dwFlags & WHDR_DONE)) continue;  // ancora in riproduzione
            waveOutUnprepareHeader(h_, &hdr_[i], sizeof(WAVEHDR));
            used_[i] = false;
        }
        float vol = muted_ ? 0.0f : volume_;
        int16_t* dst = buf_[i].data();
        for (int k = 0; k < frames * 2; ++k) {
            float s = stereo[k] * vol;
            dst[k] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        }
        memset(&hdr_[i], 0, sizeof(WAVEHDR));
        hdr_[i].lpData = reinterpret_cast<char*>(dst);
        hdr_[i].dwBufferLength = (DWORD)(frames * 4);
        waveOutPrepareHeader(h_, &hdr_[i], sizeof(WAVEHDR));
        waveOutWrite(h_, &hdr_[i], sizeof(WAVEHDR));
        used_[i] = true;
        return;
    }
    // tutti i buffer occupati: il blocco viene scartato per non accumulare ritardo
}

} // namespace sv
