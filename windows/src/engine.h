// Motore: sorgente (prova o DirectShow) -> trasformazione -> anteprima, registrazione, istantanee;
// audio (waveIn o tono di prova) -> guadagno, livello, registrazione, ponte verso un'uscita.
#pragma once
#include "audio_io.h"
#include "capture_dshow.h"
#include "recorder.h"

namespace sv {

class Engine {
public:
    enum class Source { None, Test, Device };
    enum { AudioNone = -2, AudioDefault = -1 };       // altrimenti indice waveIn

    Engine();
    ~Engine();

    // frameMsg: inviato (accorpato) quando c'è un nuovo fotogramma; graphMsg: eventi del grafo DirectShow.
    void setNotify(HWND hwnd, UINT frameMsg, UINT graphMsg);
    void frameHandled() { InterlockedExchange(&framePending_, 0); }

    bool startTest(std::wstring& err);
    bool startDevice(int formatIdx, std::wstring& err);   // la periferica va prima aperta con capture().open()
    void stop();
    bool running() const { return source_ != Source::None; }
    Source source() const { return source_; }
    DsCapture& capture() { return capture_; }

    int width() const { return fw_; }
    int height() const { return fh_; }
    double nominalFps() const;
    double measuredFps() const { return measuredFps_; }
    long frameCount() const { return frameCount_; }

    void setTransform(int mode) { transform_ = mode; }   // 0 normale, 1 specchio, 2 capovolta, 3 180°, 4 90° orario, 5 90° antiorario
    // Accesso all'ultimo fotogramma (BGRA dall'alto) per il disegno: sempre in coppia.
    bool lockFrame(const uint8_t*& bgra, int& w, int& h);
    void unlockFrame();
    bool snapshot(const std::wstring& pngPath, std::wstring& err);

    // Audio
    bool setAudioInput(int device);                       // AudioNone, AudioDefault o indice; false se l'apertura fallisce
    bool hasAudio() const;
    int audioRate() const;
    void setGain(float g) { gain_ = g; }
    float level() const { return level_; }                // picco recente 0..1

    bool startBridge(int outDevice, std::wstring& err);
    void stopBridge();
    bool bridgeActive() const { return bridge_.isOpen(); }
    void setBridgeVolume(float v) { bridge_.setVolume(v); }
    void setBridgeMuted(bool m) { bridge_.setMuted(m); }

    // Registrazione
    bool startRecording(const std::wstring& path, RecOptions opt, std::wstring& err);
    void stopRecording();
    Recorder& recorder() { return recorder_; }

private:
    static void dsFrame(void* ctx, const uint8_t* data, int w, int h, int stride, double t);
    static void audioIn(void* ctx, const int16_t* stereo, int frames, double tEnd);
    static DWORD WINAPI testThread(LPVOID self);
    void onFrame(const uint8_t* data, int w, int h, int stride, double t);
    void onAudio(const int16_t* stereo, int frames, double tEnd);
    void runTest();

    HWND hwnd_ = nullptr;
    UINT frameMsg_ = 0, graphMsg_ = 0;
    volatile LONG framePending_ = 0;

    volatile Source source_ = Source::None;
    DsCapture capture_;
    HANDLE testThread_ = nullptr;
    volatile bool testStop_ = false;

    Lock frameLock_;
    std::vector<uint8_t> frame_;
    volatile int fw_ = 0, fh_ = 0;
    volatile int transform_ = 0;
    volatile long frameCount_ = 0;
    volatile double measuredFps_ = 0;
    double fpsWindowStart_ = 0;
    long fpsWindowCount_ = 0;

    AudioIn audioIn_;
    int audioDevice_ = AudioDefault;
    AudioOut bridge_;
    volatile float gain_ = 1.0f, level_ = 0.0f;
    std::vector<int16_t> gainBuf_;

    Recorder recorder_;
};

} // namespace sv
