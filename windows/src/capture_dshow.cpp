#include "capture_dshow.h"

#include <dvdmedia.h>
#include <ocidl.h>
#include <olectl.h>
#include <qedit.h>

#include <cmath>
#include <cstring>

namespace sv {

namespace {

// GUID definiti qui per non dipendere dalle librerie d'importazione (qedit non è in strmiids).
const GUID kCLSID_SampleGrabber = { 0xC1F400A0, 0x3F08, 0x11d3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };
const GUID kCLSID_NullRenderer = { 0xC1F400A4, 0x3F08, 0x11d3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };
const GUID kIID_ISampleGrabber = { 0x6B652FFF, 0x11FE, 0x4fce, { 0x92, 0xAD, 0x02, 0x66, 0xB5, 0xD7, 0xC7, 0x8F } };
const GUID kIID_ISampleGrabberCB = { 0x0579154A, 0x2B53, 0x4994, { 0xB0, 0xD0, 0xE7, 0x73, 0x14, 0x8E, 0xFF, 0x85 } };
const GUID kBdaNetworkTuner = { 0x71985F48, 0x1CA1, 0x11d3, { 0x9C, 0xC8, 0x00, 0xC0, 0x4F, 0x79, 0x71, 0xE0 } };

void freeMediaType(AM_MEDIA_TYPE* mt) {
    if (!mt) return;
    if (mt->cbFormat) CoTaskMemFree(mt->pbFormat);
    if (mt->pUnk) mt->pUnk->Release();
    CoTaskMemFree(mt);
}

void clearMediaType(AM_MEDIA_TYPE& mt) {
    if (mt.cbFormat) CoTaskMemFree(mt.pbFormat);
    if (mt.pUnk) mt.pUnk->Release();
    memset(&mt, 0, sizeof mt);
}

std::wstring subtypeName(const GUID& g) {
    static const BYTE tail[8] = { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
    if (g == MEDIASUBTYPE_RGB24) return L"RGB24";
    if (g == MEDIASUBTYPE_RGB32) return L"RGB32";
    if (g == MEDIASUBTYPE_RGB565) return L"RGB565";
    if (g == MEDIASUBTYPE_RGB555) return L"RGB555";
    if (g == MEDIASUBTYPE_ARGB32) return L"ARGB32";
    if (g.Data2 == 0x0000 && g.Data3 == 0x0010 && memcmp(g.Data4, tail, 8) == 0) {
        wchar_t s[5];
        for (int i = 0; i < 4; ++i) {
            unsigned c = (g.Data1 >> (8 * i)) & 0xff;
            s[i] = (c >= 32 && c < 127) ? (wchar_t)c : L'?';
        }
        s[4] = 0;
        return s;
    }
    return L"?";
}

bool videoInfo(const AM_MEDIA_TYPE* mt, int& w, int& h, double& fps, bool* bottomUp = nullptr) {
    const BITMAPINFOHEADER* bih = nullptr;
    REFERENCE_TIME avg = 0;
    if (mt->formattype == FORMAT_VideoInfo && mt->cbFormat >= sizeof(VIDEOINFOHEADER)) {
        auto* v = reinterpret_cast<const VIDEOINFOHEADER*>(mt->pbFormat);
        bih = &v->bmiHeader;
        avg = v->AvgTimePerFrame;
    } else if (mt->formattype == FORMAT_VideoInfo2 && mt->cbFormat >= sizeof(VIDEOINFOHEADER2)) {
        auto* v = reinterpret_cast<const VIDEOINFOHEADER2*>(mt->pbFormat);
        bih = &v->bmiHeader;
        avg = v->AvgTimePerFrame;
    }
    if (!bih) return false;
    w = bih->biWidth;
    h = bih->biHeight < 0 ? -bih->biHeight : bih->biHeight;
    if (bottomUp) *bottomUp = bih->biHeight > 0;
    fps = avg > 0 ? 1e7 / double(avg) : 0;
    return w > 0 && h > 0;
}

std::wstring bagString(IPropertyBag* bag, const wchar_t* key) {
    VARIANT v;
    VariantInit(&v);
    std::wstring s;
    if (SUCCEEDED(bag->Read(key, &v, nullptr)) && v.vt == VT_BSTR && v.bstrVal) s = v.bstrVal;
    VariantClear(&v);
    return s;
}

// Enumera una categoria di dispositivi; se "want" non è vuoto restituisce il filtro corrispondente.
void enumCategory(const GUID& category, std::vector<DsDevice>* list, const std::wstring& want, IBaseFilter** found) {
    ComPtr<ICreateDevEnum> de;
    if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, de.pv()))) return;
    ComPtr<IEnumMoniker> en;
    if (de->CreateClassEnumerator(category, &en, 0) != S_OK) return;       // S_FALSE = categoria vuota
    IMoniker* mk = nullptr;
    while (en->Next(1, &mk, nullptr) == S_OK) {
        DsDevice d;
        ComPtr<IPropertyBag> bag;
        if (SUCCEEDED(mk->BindToStorage(nullptr, nullptr, IID_IPropertyBag, bag.pv()))) d.name = bagString(bag, L"FriendlyName");
        LPOLESTR disp = nullptr;
        if (SUCCEEDED(mk->GetDisplayName(nullptr, nullptr, &disp)) && disp) {
            d.moniker = disp;
            CoTaskMemFree(disp);
        }
        if (d.name.empty()) d.name = L"Dispositivo video";
        if (list) list->push_back(d);
        if (found && !*found && !want.empty() && d.moniker == want) mk->BindToObject(nullptr, nullptr, IID_IBaseFilter, reinterpret_cast<void**>(found));
        mk->Release();
    }
}

const wchar_t* procAmpName(long p) {
    static const wchar_t* n[] = { L"Luminosità", L"Contrasto", L"Tonalità", L"Saturazione", L"Nitidezza", L"Gamma",
                                  L"Colore", L"Bilanc. bianco", L"Controluce", L"Guadagno" };
    if (p >= 0 && p <= 9) return n[p];
    if (p == 13) return L"Rete 50/60 Hz";
    return L"";
}

const wchar_t* camCtlName(long p) {
    static const wchar_t* n[] = { L"Pan", L"Tilt", L"Rotazione", L"Zoom", L"Esposizione", L"Diaframma", L"Messa a fuoco" };
    return (p >= 0 && p <= 6) ? n[p] : L"";
}

const wchar_t* physName(long type) {
    switch (type) {
        case PhysConn_Video_Tuner: return L"Sintonizzatore TV";
        case PhysConn_Video_Composite: return L"Video composito";
        case PhysConn_Video_SVideo: return L"S-Video";
        case PhysConn_Video_RGB: return L"RGB";
        case PhysConn_Video_YRYBY: return L"Component (YPbPr)";
        case PhysConn_Video_SerialDigital: return L"SDI";
        case PhysConn_Video_ParallelDigital: return L"Digitale parallelo";
        case PhysConn_Video_SCSI: return L"SCSI";
        case PhysConn_Video_AUX: return L"AUX";
        case PhysConn_Video_1394: return L"FireWire";
        case PhysConn_Video_USB: return L"USB";
        case PhysConn_Video_SCART: return L"SCART";
        default: return L"Ingresso video";
    }
}

} // namespace

std::wstring DsFormat::label() const {
    if (fps > 0) return format(L"%ls %dx%d @ %.4g fps", codec.c_str(), width, height, fps);
    return format(L"%ls %dx%d", codec.c_str(), width, height);
}

std::vector<DsDevice> listVideoDevices() {
    std::vector<DsDevice> out;
    enumCategory(CLSID_VideoInputDeviceCategory, &out, L"", nullptr);
    return out;
}

std::vector<std::wstring> listDigitalTuners() {
    std::vector<DsDevice> devs;
    enumCategory(kBdaNetworkTuner, &devs, L"", nullptr);
    std::vector<std::wstring> out;
    for (auto& d : devs) out.push_back(d.name);
    return out;
}

// Callback del sample grabber: gira nel thread di streaming di DirectShow.
struct DsCapture::Callback : public ISampleGrabberCB {
    DsCapture* owner;
    explicit Callback(DsCapture* o) : owner(o) {}
    virtual ~Callback() {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == kIID_ISampleGrabberCB) { *ppv = static_cast<ISampleGrabberCB*>(this); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }      // vita gestita da DsCapture
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP SampleCB(double, IMediaSample*) override { return S_OK; }
    STDMETHODIMP BufferCB(double, BYTE* buffer, LONG len) override {
        owner->deliver(buffer, len);
        return S_OK;
    }
};

DsCapture::DsCapture() { cb_ = new Callback(this); }

DsCapture::~DsCapture() {
    close();
    delete cb_;
}

bool DsCapture::open(const DsDevice& dev, std::wstring& err) {
    close();
    dev_ = dev;
    IBaseFilter* f = nullptr;
    enumCategory(CLSID_VideoInputDeviceCategory, nullptr, dev.moniker, &f);
    if (!f) { err = L"Periferica non trovata: " + dev.name + L"\nÈ stata scollegata?"; return false; }
    *(&cap_) = f;
    HRESULT hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, graph_.pv());
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr, CLSCTX_INPROC_SERVER, IID_ICaptureGraphBuilder2, builder_.pv());
    if (SUCCEEDED(hr)) hr = builder_->SetFiltergraph(graph_);
    if (SUCCEEDED(hr)) hr = graph_->AddFilter(cap_, L"Cattura");
    if (FAILED(hr)) { err = format(L"DirectShow non disponibile (errore 0x%08lX).", (unsigned long)hr); close(); return false; }
    graph_->QueryInterface(IID_IMediaControl, control_.pv());
    graph_->QueryInterface(IID_IMediaEventEx, events_.pv());

    if (FAILED(builder_->FindInterface(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, cap_, IID_IAMStreamConfig, config_.pv())))
        builder_->FindInterface(&PIN_CATEGORY_PREVIEW, &MEDIATYPE_Video, cap_, IID_IAMStreamConfig, config_.pv());
    cap_->QueryInterface(IID_IAMVideoProcAmp, procAmp_.pv());
    cap_->QueryInterface(IID_IAMCameraControl, camCtl_.pv());
    // Cerca a monte sintonizzatore e crossbar (schede TV analogiche): il builder aggiunge i filtri necessari.
    builder_->FindInterface(&LOOK_UPSTREAM_ONLY, nullptr, cap_, IID_IAMTVTuner, tuner_.pv());
    builder_->FindInterface(&LOOK_UPSTREAM_ONLY, nullptr, cap_, IID_IAMCrossbar, xbar_.pv());
    xbarInputs_.clear();
    xbarOut_ = -1;
    if (xbar_.get()) {
        long nOut = 0, nIn = 0;
        if (SUCCEEDED(xbar_->get_PinCounts(&nOut, &nIn))) {
            for (long i = 0; i < nOut; ++i) {
                long rel = 0, type = 0;
                if (SUCCEEDED(xbar_->get_CrossbarPinInfo(FALSE, i, &rel, &type)) && type == PhysConn_Video_VideoDecoder) { xbarOut_ = i; break; }
            }
            if (xbarOut_ < 0 && nOut > 0) xbarOut_ = 0;
            for (long i = 0; i < nIn; ++i) {
                long rel = 0, type = 0;
                if (SUCCEEDED(xbar_->get_CrossbarPinInfo(TRUE, i, &rel, &type)) && type < PhysConn_Audio_Tuner &&
                    xbar_->CanRoute(xbarOut_, i) == S_OK)
                    xbarInputs_.push_back(i);
            }
        }
    }
    readFormats();
    return true;
}

void DsCapture::readFormats() {
    formats_.clear();
    if (!config_.get()) return;
    int count = 0, size = 0;
    if (FAILED(config_->GetNumberOfCapabilities(&count, &size)) || size <= 0) return;
    std::vector<BYTE> scc((size_t)size);
    for (int i = 0; i < count; ++i) {
        AM_MEDIA_TYPE* mt = nullptr;
        if (config_->GetStreamCaps(i, &mt, scc.data()) != S_OK || !mt) continue;
        DsFormat f;
        f.capIndex = i;
        if (mt->majortype == MEDIATYPE_Video && videoInfo(mt, f.width, f.height, f.fps)) {
            f.codec = subtypeName(mt->subtype);
            f.fps = std::floor(f.fps * 100 + 0.5) / 100;
            bool dup = false;
            for (auto& o : formats_) dup = dup || (o.codec == f.codec && o.width == f.width && o.height == f.height && o.fps == f.fps);
            if (!dup) formats_.push_back(f);
        }
        freeMediaType(mt);
    }
}

int DsCapture::bestFormat() const {
    int best = -1, bestScore = -100000;
    for (size_t i = 0; i < formats_.size(); ++i) {
        const DsFormat& f = formats_[i];
        int score = 0;
        if (f.codec == L"MJPG") score += 100;                 // basso carico USB anche in HD
        else if (f.codec == L"YUY2" || f.codec == L"UYVY") score += 60;
        else if (f.codec == L"RGB24" || f.codec == L"RGB32") score += 50;
        else if (f.codec == L"NV12" || f.codec == L"I420" || f.codec == L"YV12") score += 40;
        else if (f.codec == L"H264" || f.codec == L"HEVC") score -= 200;   // richiede un decodificatore installato
        if (f.width == 1280 && f.height == 720) score += 50;
        else if (f.width == 1920 && f.height == 1080) score += 30;
        else if (f.width == 640 && f.height == 480) score += 25;
        else if (f.width > 1920) score -= 40;
        if (f.fps >= 29 && f.fps <= 31) score += 20;
        else if (f.fps >= 24 && f.fps < 29) score += 10;
        if (score > bestScore) { bestScore = score; best = (int)i; }
    }
    return best;
}

bool DsCapture::start(int formatIdx, HWND notifyWnd, UINT notifyMsg, DsFrameCallback cb, void* ctx, std::wstring& err) {
    if (!isOpen()) { err = L"Nessuna periferica aperta."; return false; }
    if (rendered_) {                                           // il grafo va ricostruito per cambiare formato
        DsDevice d = dev_;
        if (!open(d, err)) return false;
    }
    frameCb_ = cb;
    frameCtx_ = ctx;
    fps_ = 0;
    if (config_.get() && formatIdx >= 0 && formatIdx < (int)formats_.size()) {
        int count = 0, size = 0;
        config_->GetNumberOfCapabilities(&count, &size);
        std::vector<BYTE> scc((size_t)(size > 0 ? size : 1));
        AM_MEDIA_TYPE* mt = nullptr;
        if (config_->GetStreamCaps(formats_[formatIdx].capIndex, &mt, scc.data()) == S_OK && mt) {
            config_->SetFormat(mt);
            fps_ = formats_[formatIdx].fps;
            freeMediaType(mt);
        }
    }
    HRESULT hr = CoCreateInstance(kCLSID_SampleGrabber, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter, grabberF_.pv());
    if (FAILED(hr)) { err = L"Componente DirectShow \"Sample Grabber\" (qedit.dll) non disponibile."; return false; }
    hr = CoCreateInstance(kCLSID_NullRenderer, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter, nullF_.pv());
    if (FAILED(hr)) { err = L"Componente DirectShow \"Null Renderer\" non disponibile."; return false; }
    ComPtr<ISampleGrabber> grabber;
    grabberF_->QueryInterface(kIID_ISampleGrabber, grabber.pv());
    if (!grabber.get()) { err = L"Interfaccia ISampleGrabber non disponibile."; return false; }
    AM_MEDIA_TYPE want;
    memset(&want, 0, sizeof want);
    want.majortype = MEDIATYPE_Video;
    want.subtype = MEDIASUBTYPE_RGB32;                         // DirectShow inserisce i decodificatori necessari
    grabber->SetMediaType(&want);
    grabber->SetBufferSamples(FALSE);
    grabber->SetOneShot(FALSE);
    graph_->AddFilter(grabberF_, L"Grabber");
    graph_->AddFilter(nullF_, L"Null");
    rendered_ = true;
    hr = builder_->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, cap_, grabberF_, nullF_);
    if (FAILED(hr)) hr = builder_->RenderStream(&PIN_CATEGORY_PREVIEW, &MEDIATYPE_Video, cap_, grabberF_, nullF_);
    if (FAILED(hr)) hr = builder_->RenderStream(nullptr, &MEDIATYPE_Video, cap_, grabberF_, nullF_);
    if (FAILED(hr)) {
        err = format(L"Impossibile collegare la periferica (errore 0x%08lX).\nIl formato scelto potrebbe richiedere un decodificatore non installato: prova un altro formato (MJPG o YUY2).", (unsigned long)hr);
        return false;
    }
    AM_MEDIA_TYPE mt;
    memset(&mt, 0, sizeof mt);
    bool bottomUp = true;
    double fps = 0;
    if (FAILED(grabber->GetConnectedMediaType(&mt)) || !videoInfo(&mt, w_, h_, fps, &bottomUp)) {
        clearMediaType(mt);
        err = L"Formato video non riconosciuto.";
        return false;
    }
    clearMediaType(mt);
    if (fps_ <= 0) fps_ = fps;
    stride_ = bottomUp ? -(w_ * 4) : w_ * 4;
    grabber->SetCallback(cb_, 1);                              // 1 = BufferCB
    if (events_.get() && notifyWnd) events_->SetNotifyWindow((OAHWND)notifyWnd, (long)notifyMsg, 0);
    running_ = true;
    hr = control_->Run();
    if (FAILED(hr)) {
        running_ = false;
        err = format(L"La periferica non si avvia (errore 0x%08lX).\nPotrebbe essere in uso da un'altra applicazione.", (unsigned long)hr);
        return false;
    }
    return true;
}

void DsCapture::stop() {
    running_ = false;
    if (events_.get()) events_->SetNotifyWindow(0, 0, 0);
    if (control_.get() && rendered_) control_->Stop();
}

void DsCapture::close() {
    stop();
    frameCb_ = nullptr;
    xbar_.reset(); tuner_.reset(); camCtl_.reset(); procAmp_.reset(); config_.reset();
    events_.reset(); control_.reset();
    nullF_.reset(); grabberF_.reset(); cap_.reset();
    builder_.reset(); graph_.reset();
    formats_.clear();
    xbarInputs_.clear();
    rendered_ = false;
    w_ = h_ = 0;
}

void DsCapture::deliver(const uint8_t* data, long len) {
    if (!running_ || !frameCb_ || len < (long)w_ * h_ * 4) return;
    frameCb_(frameCtx_, data, w_, h_, stride_, nowSeconds());
}

bool DsCapture::pollLost() {
    bool lost = false;
    if (!events_.get()) return false;
    long code = 0;
    LONG_PTR p1 = 0, p2 = 0;
    while (events_->GetEvent(&code, &p1, &p2, 0) == S_OK) {
        if (code == EC_DEVICE_LOST || code == EC_ERRORABORT) lost = true;
        events_->FreeEventParams(code, p1, p2);
    }
    return lost;
}

std::vector<DsControl> DsCapture::controls() {
    std::vector<DsControl> out;
    if (procAmp_.get()) {
        static const long props[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 13 };
        for (long p : props) {
            DsControl c;
            long caps = 0, flags = 0;
            if (FAILED(procAmp_->GetRange(p, &c.min, &c.max, &c.step, &c.def, &caps)) || c.max <= c.min) continue;
            if (FAILED(procAmp_->Get(p, &c.value, &flags))) continue;
            c.kind = 0; c.prop = p; c.name = procAmpName(p);
            c.hasAuto = (caps & VideoProcAmp_Flags_Auto) != 0;
            c.isAuto = (flags & VideoProcAmp_Flags_Auto) != 0;
            if (c.step <= 0) c.step = 1;
            out.push_back(c);
        }
    }
    if (camCtl_.get()) {
        static const long props[] = { 3, 6, 4, 0, 1, 2, 5 };  // zoom, fuoco, esposizione, pan, tilt, rotazione, diaframma
        for (long p : props) {
            DsControl c;
            long caps = 0, flags = 0;
            if (FAILED(camCtl_->GetRange(p, &c.min, &c.max, &c.step, &c.def, &caps)) || c.max <= c.min) continue;
            if (FAILED(camCtl_->Get(p, &c.value, &flags))) continue;
            c.kind = 1; c.prop = p; c.name = camCtlName(p);
            c.hasAuto = (caps & CameraControl_Flags_Auto) != 0;
            c.isAuto = (flags & CameraControl_Flags_Auto) != 0;
            if (c.step <= 0) c.step = 1;
            out.push_back(c);
        }
    }
    return out;
}

bool DsCapture::setControl(int kind, long prop, long value, bool autoMode) {
    if (kind == 0 && procAmp_.get()) return SUCCEEDED(procAmp_->Set(prop, value, autoMode ? VideoProcAmp_Flags_Auto : VideoProcAmp_Flags_Manual));
    if (kind == 1 && camCtl_.get()) return SUCCEEDED(camCtl_->Set(prop, value, autoMode ? CameraControl_Flags_Auto : CameraControl_Flags_Manual));
    return false;
}

bool DsCapture::showPropertyPages(HWND parent) {
    if (!cap_.get()) return false;
    ComPtr<ISpecifyPropertyPages> pages;
    if (FAILED(cap_->QueryInterface(IID_ISpecifyPropertyPages, pages.pv())) || !pages.get()) return false;
    CAUUID ca;
    ca.cElems = 0;
    ca.pElems = nullptr;
    if (FAILED(pages->GetPages(&ca)) || !ca.cElems) return false;
    IUnknown* unk = cap_.get();
    HRESULT hr = OleCreatePropertyFrame(parent, 30, 30, dev_.name.c_str(), 1, &unk, ca.cElems, ca.pElems, 0, 0, nullptr);
    CoTaskMemFree(ca.pElems);
    return SUCCEEDED(hr);
}

bool DsCapture::tunerRange(long& minCh, long& maxCh) { return tuner_.get() && SUCCEEDED(tuner_->ChannelMinMax(&minCh, &maxCh)); }

long DsCapture::tunerChannel() {
    long ch = 0, v = 0, a = 0;
    if (tuner_.get()) tuner_->get_Channel(&ch, &v, &a);
    return ch;
}

bool DsCapture::setTunerChannel(long ch) { return tuner_.get() && SUCCEEDED(tuner_->put_Channel(ch, AMTUNER_SUBCHAN_DEFAULT, AMTUNER_SUBCHAN_DEFAULT)); }

long DsCapture::tunerCountry() {
    long c = 0;
    if (tuner_.get()) tuner_->get_CountryCode(&c);
    return c;
}

bool DsCapture::setTunerCountry(long code) { return tuner_.get() && SUCCEEDED(tuner_->put_CountryCode(code)); }

bool DsCapture::tunerIsCable() {
    TunerInputType t = TunerInputAntenna;
    if (tuner_.get()) tuner_->get_InputType(0, &t);
    return t == TunerInputCable;
}

bool DsCapture::setTunerCable(bool cable) { return tuner_.get() && SUCCEEDED(tuner_->put_InputType(0, cable ? TunerInputCable : TunerInputAntenna)); }

bool DsCapture::tunerSignal() {
    long s = 0;
    return tuner_.get() && SUCCEEDED(tuner_->SignalPresent(&s)) && s == AMTUNER_SIGNALPRESENT;
}

std::vector<std::wstring> DsCapture::videoInputs() {
    std::vector<std::wstring> out;
    if (!xbar_.get()) return out;
    for (long in : xbarInputs_) {
        long rel = 0, type = 0;
        xbar_->get_CrossbarPinInfo(TRUE, in, &rel, &type);
        out.push_back(physName(type));
    }
    return out;
}

int DsCapture::currentVideoInput() {
    long in = -1;
    if (!xbar_.get() || xbarOut_ < 0 || FAILED(xbar_->get_IsRoutedTo(xbarOut_, &in))) return -1;
    for (size_t i = 0; i < xbarInputs_.size(); ++i)
        if (xbarInputs_[i] == in) return (int)i;
    return -1;
}

bool DsCapture::selectVideoInput(int index) {
    if (!xbar_.get() || index < 0 || index >= (int)xbarInputs_.size() || xbarOut_ < 0) return false;
    long in = xbarInputs_[index];
    if (FAILED(xbar_->Route(xbarOut_, in))) return false;
    // Instrada anche l'audio abbinato all'ingresso, se il crossbar lo prevede.
    long relIn = -1, type = 0, nOut = 0, nIn = 0;
    xbar_->get_CrossbarPinInfo(TRUE, in, &relIn, &type);
    xbar_->get_PinCounts(&nOut, &nIn);
    if (relIn >= 0) {
        for (long o = 0; o < nOut; ++o) {
            long rel = 0, otype = 0;
            if (SUCCEEDED(xbar_->get_CrossbarPinInfo(FALSE, o, &rel, &otype)) && otype >= PhysConn_Audio_Tuner && xbar_->CanRoute(o, relIn) == S_OK) {
                xbar_->Route(o, relIn);
                break;
            }
        }
    }
    return true;
}

} // namespace sv
