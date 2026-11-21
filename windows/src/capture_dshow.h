// Cattura video con DirectShow (Windows XP -> Windows 11): webcam UVC, schede di acquisizione
// e sintonizzatori TV analogici WDM. Formati, controlli della webcam, sintonizzatore e crossbar.
#pragma once
#include <dshow.h>

#include <string>
#include <vector>

#include "common.h"

namespace sv {

template <class T>
class ComPtr {
public:
    ComPtr() {}
    ~ComPtr() { reset(); }
    T** operator&() { reset(); return &p_; }
    T* operator->() const { return p_; }
    operator T*() const { return p_; }
    T* get() const { return p_; }
    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }
    void** pv() { reset(); return reinterpret_cast<void**>(&p_); }
private:
    T* p_ = nullptr;
    ComPtr(const ComPtr&);
    ComPtr& operator=(const ComPtr&);
};

struct DsDevice {
    std::wstring name;       // nome descrittivo (es. "HD Pro Webcam C920")
    std::wstring moniker;    // identificativo univoco (display name del moniker)
};

struct DsFormat {
    int capIndex = -1;       // indice per IAMStreamConfig::GetStreamCaps
    std::wstring codec;      // "MJPG", "YUY2", "RGB24"...
    int width = 0, height = 0;
    double fps = 0;
    std::wstring label() const;
};

struct DsControl {
    int kind = 0;            // 0 = IAMVideoProcAmp, 1 = IAMCameraControl
    long prop = 0;
    std::wstring name;
    long min = 0, max = 0, step = 1, def = 0, value = 0;
    bool hasAuto = false, isAuto = false;
};

std::vector<DsDevice> listVideoDevices();
std::vector<std::wstring> listDigitalTuners();      // schede TV digitali (BDA), solo elenco

// Riceve i fotogrammi RGB32. stride < 0 significa immagine dal basso verso l'alto (DIB classica).
typedef void (*DsFrameCallback)(void* ctx, const uint8_t* data, int w, int h, int stride, double t);

class DsCapture {
public:
    DsCapture();
    ~DsCapture();

    bool open(const DsDevice& dev, std::wstring& err);       // crea il grafo e legge formati e controlli
    void close();
    bool isOpen() const { return cap_.get() != nullptr; }
    const DsDevice& device() const { return dev_; }

    const std::vector<DsFormat>& formats() const { return formats_; }
    int bestFormat() const;                                   // indice in formats() consigliato, -1 se vuoto

    bool start(int formatIdx, HWND notifyWnd, UINT notifyMsg, DsFrameCallback cb, void* ctx, std::wstring& err);
    void stop();
    bool running() const { return running_; }
    int width() const { return w_; }
    int height() const { return h_; }
    double nominalFps() const { return fps_; }
    // Da chiamare alla ricezione di notifyMsg: true se il dispositivo è stato perso o il grafo si è interrotto.
    bool pollLost();

    // Controlli della webcam
    std::vector<DsControl> controls();
    bool setControl(int kind, long prop, long value, bool autoMode);
    bool showPropertyPages(HWND parent);

    // Sintonizzatore TV analogico
    bool hasTuner() const { return tuner_.get() != nullptr; }
    bool tunerRange(long& minCh, long& maxCh);
    long tunerChannel();
    bool setTunerChannel(long ch);
    long tunerCountry();
    bool setTunerCountry(long code);
    bool tunerIsCable();
    bool setTunerCable(bool cable);
    bool tunerSignal();

    // Crossbar: ingressi video fisici (sintonizzatore, composito, S-Video)
    std::vector<std::wstring> videoInputs();
    int currentVideoInput();
    bool selectVideoInput(int index);

    // uso interno (callback del sample grabber)
    void deliver(const uint8_t* data, long len);

private:
    void readFormats();
    DsDevice dev_;
    ComPtr<IGraphBuilder> graph_;
    ComPtr<ICaptureGraphBuilder2> builder_;
    ComPtr<IBaseFilter> cap_, grabberF_, nullF_;
    ComPtr<IPin> capPin_;                // pin di uscita video della periferica
    ComPtr<IMediaControl> control_;
    ComPtr<IMediaEventEx> events_;
    ComPtr<IAMStreamConfig> config_;
    ComPtr<IAMVideoProcAmp> procAmp_;
    ComPtr<IAMCameraControl> camCtl_;
    ComPtr<IAMTVTuner> tuner_;
    ComPtr<IAMCrossbar> xbar_;
    std::vector<DsFormat> formats_;
    std::vector<long> xbarInputs_;      // indici dei pin d'ingresso video del crossbar
    long xbarOut_ = -1;
    struct Callback;
    Callback* cb_ = nullptr;
    DsFrameCallback frameCb_ = nullptr;
    void* frameCtx_ = nullptr;
    volatile bool running_ = false;
    bool rendered_ = false;
    int w_ = 0, h_ = 0, stride_ = 0, bpp_ = 4;
    std::vector<uint8_t> conv_;          // conversione RGB24 -> RGB32 (ripiego)
    double fps_ = 0;
};

} // namespace sv
