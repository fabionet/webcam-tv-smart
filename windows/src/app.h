// Finestra principale dell'edizione Windows (Win32 puro: funziona da Windows XP a Windows 11).
#pragma once
#include "engine.h"
#include "settings.h"

#include <commctrl.h>

namespace sv {

// Messaggi e timer
enum { WM_APP_FRAME = WM_APP + 1, WM_APP_GRAPH = WM_APP + 2, WM_APP_EXTCLOSED = WM_APP + 3 };
enum { TIMER_UI = 1, TIMER_DEVCHANGE = 2, TIMER_SELFTEST = 3, TIMER_TVSCAN = 4 };

// Identificativi dei controlli
enum {
    IDC_NOTE = 901, IDC_HEADING = 902,
    IDC_SRC_DEVBAR = 1001, IDC_SRC_REFRESH, IDC_SRC_COMBO, IDC_SRC_FORMAT, IDC_SRC_AUDIO, IDC_SRC_GAIN, IDC_SRC_ORIENT, IDC_SRC_START, IDC_SRC_STOP,
    IDC_CAM_INFO = 1101, IDC_CAM_PRESET_AUTO, IDC_CAM_PRESET_50HZ, IDC_CAM_PRESET_FOCUS, IDC_CAM_PROPS, IDC_CAM_EMPTY,
    IDC_CAM_SLIDER = 1200, IDC_CAM_AUTO = 1300, IDC_CAM_VALUE = 1400, IDC_CAM_LABEL = 1450,
    IDC_TV_INFO = 1501, IDC_TV_COUNTRY, IDC_TV_COUNTRY_SET, IDC_TV_RF, IDC_TV_CHANNEL, IDC_TV_CHSPIN, IDC_TV_GO, IDC_TV_PREV, IDC_TV_NEXT,
    IDC_TV_XBAR, IDC_TV_SCAN, IDC_TV_FOUND, IDC_TV_SIGNAL,
    IDC_REC_MP4 = 1601, IDC_REC_OGG, IDC_REC_ENCINFO, IDC_REC_VKBPS, IDC_REC_VSPIN, IDC_REC_AKBPS, IDC_REC_ASPIN, IDC_REC_AUDIO,
    IDC_REC_FOLDER, IDC_REC_BROWSE, IDC_REC_PREFIX, IDC_REC_BUTTON, IDC_REC_LAST, IDC_REC_OPEN,
    IDC_OUT_EXT = 1701, IDC_OUT_POS, IDC_OUT_ADEV, IDC_OUT_AON, IDC_OUT_AVOL, IDC_OUT_AMUTE,
    IDC_TB_START = 1801, IDC_TB_SNAP, IDC_TB_REC, IDC_TB_INFO,
    IDC_PH_REFRESH = 1901, IDC_PH_START,
    IDM_SNAPSHOT = 2001, IDM_RECORD, IDM_OPENFOLDER, IDM_EXIT, IDM_STARTSTOP, IDM_REFRESH, IDM_FULLSCREEN, IDM_EXTWINDOW,
    IDM_GUIDE, IDM_DIAG, IDM_ABOUT, IDM_ESCAPE
};

enum { PAGE_SOURCE, PAGE_WEBCAM, PAGE_TV, PAGE_REC, PAGE_OUT, PAGE_COUNT };

class App {
public:
    static App& get();

    bool create(HINSTANCE inst, int show, const std::wstring& selfTestDir, bool selfTestDevice);
    int run();

    // ------------------------------------------------------------ finestre
    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr, video_ = nullptr, tab_ = nullptr, status_ = nullptr, ext_ = nullptr;
    HWND tbStart_ = nullptr, tbSnap_ = nullptr, tbRec_ = nullptr, tbInfo_ = nullptr;
    HWND phRefresh_ = nullptr, phStart_ = nullptr;
    HWND pages_[PAGE_COUNT] = {};
    HACCEL accel_ = nullptr;
    HMENU menu_ = nullptr;
    HFONT font_ = nullptr, fontBold_ = nullptr, fontBig_ = nullptr;
    int dpi_ = 96;
    int S(int v) const { return MulDiv(v, dpi_, 96); }

    // --------------------------------------------------------------- stato
    Engine engine_;
    Settings settings_;
    std::vector<DsDevice> devices_;
    std::vector<AudioDeviceInfo> audioIns_, audioOuts_;
    std::vector<DsControl> camControls_;
    std::vector<HWND> camDynamic_;
    std::wstring placeholderTitle_, placeholderText_, lastFile_, recPath_, statusText_;
    bool fullscreen_ = false, extFullscreen_ = false, suppress_ = false;
    WINDOWPLACEMENT savedPlacement_ = {};
    RECT extSavedRect_ = {};
    int tvScanCh_ = 0, tvScanMax_ = 0;
    bool tvScanning_ = false;
    DWORD lastSecondTick_ = 0;
    float shownLevel_ = -1;

    // auto-test
    std::wstring selfTestDir_;
    bool selfTest_ = false, selfTestDevice_ = false;
    int selfTestStep_ = 0, selfTestErrors_ = 0, exitCode_ = 0;
    DWORD selfTestNext_ = 0;
    void selfTestTick();

    // ------------------------------------------------------- costruzione UI
    void createFonts();
    void createMenu();
    void createChildren();
    void buildSourcePage(HWND page);
    void buildWebcamPage(HWND page);
    void buildTvPage(HWND page);
    void buildRecPage(HWND page);
    void buildOutPage(HWND page);
    HWND mk(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0);
    HWND item(int page, int id) const { return GetDlgItem(pages_[page], id); }
    void setPageContentHeight(HWND page, int logicalHeight);
    void layout();
    void showPage(int index);

    // ----------------------------------------------------------- dispositivi
    void refreshDevices(bool hotplug);
    void fillAudioCombos();
    void fillFormatCombo();
    int selectedDeviceIndex() const;                 // -1 = sorgente di prova
    bool openSelectedDevice(std::wstring& err);
    void onSourceSelected(bool autoStart);
    void startSelected();
    void stopSource(const std::wstring& status);
    void applyAudioInput();
    int resolveAudioDevice() const;
    void updateUi();
    void updateDeviceBar();
    std::wstring sourceName() const;
    std::wstring detectedText() const;
    std::wstring diagnostics();

    // --------------------------------------------------------------- pagine
    void rebuildWebcamControls();
    void onCamSlider(int index);
    void onCamAuto(int index);
    void applyCamPreset(int preset);
    void rebuildTvPage();
    void tvSetChannel(long ch);
    void tvScanStep();

    // ---------------------------------------------------------------- azioni
    void toggleRecording();
    void takeSnapshot();
    std::wstring recFolder() const;
    void openRecFolder();
    void browseFolder();
    void toggleFullscreen();
    void toggleExtWindow(bool on);
    void placeExtWindow();
    void toggleBridge(bool on);
    void showAbout();
    void openGuide();
    void setStatus(const std::wstring& text);
    void showError(const std::wstring& text);
    void saveSettings();

    // ------------------------------------------------------------- messaggi
    LRESULT wndProc(HWND h, UINT m, WPARAM w, LPARAM l);
    LRESULT videoProc(HWND h, UINT m, WPARAM w, LPARAM l);
    LRESULT extProc(HWND h, UINT m, WPARAM w, LPARAM l);
    void onCommand(int id, int code, HWND ctl);
    void onHScroll(HWND ctl);
    void onTimer(UINT_PTR id);
    void paintVideo(HWND h, bool external);
};

// Utilità per i controlli
std::wstring getText(HWND h);
void comboAdd(HWND combo, const std::wstring& text);
int comboSel(HWND combo);
std::wstring comboText(HWND combo);
bool comboSelectText(HWND combo, const std::wstring& text);

} // namespace sv
