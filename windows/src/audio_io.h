// Audio con le API waveIn/waveOut (da Windows XP a Windows 11): cattura a 16 bit stereo e ponte verso un'uscita.
#pragma once
#include "common.h"

#include <mmsystem.h>

#include <string>
#include <vector>

namespace sv {

struct AudioDeviceInfo {
    int id = -1;                 // indice waveIn/waveOut
    std::wstring name;
};

std::vector<AudioDeviceInfo> listAudioInputs();
std::vector<AudioDeviceInfo> listAudioOutputs();

typedef void (*AudioCallback)(void* ctx, const int16_t* stereo, int frames, double tEnd);

class AudioIn {
public:
    ~AudioIn() { close(); }
    bool open(int deviceId /* -1 = predefinito */, AudioCallback cb, void* ctx);
    void close();
    bool isOpen() const { return h_ != nullptr; }
    int rate() const { return rate_; }
private:
    static DWORD WINAPI threadProc(LPVOID self);
    void run();
    HWAVEIN h_ = nullptr;
    HANDLE event_ = nullptr, thread_ = nullptr;
    volatile bool stopping_ = false;
    int rate_ = 48000, channels_ = 2;
    WAVEHDR hdr_[4];
    std::vector<char> buf_[4];
    std::vector<int16_t> conv_;
    AudioCallback cb_ = nullptr;
    void* ctx_ = nullptr;
};

class AudioOut {
public:
    ~AudioOut() { close(); }
    bool open(int deviceId /* -1 = predefinito */, int rate);
    void close();
    bool isOpen() const { return h_ != nullptr; }
    void write(const int16_t* stereo, int frames);     // non bloccante: scarta se la coda è piena
    void setVolume(float v) { volume_ = v; }
    void setMuted(bool m) { muted_ = m; }
private:
    enum { kBuffers = 12, kMaxFrames = 4096 };
    HWAVEOUT h_ = nullptr;
    WAVEHDR hdr_[kBuffers];
    std::vector<int16_t> buf_[kBuffers];
    bool used_[kBuffers];
    Lock lock_;
    volatile float volume_ = 1.0f;
    volatile bool muted_ = false;
};

} // namespace sv
