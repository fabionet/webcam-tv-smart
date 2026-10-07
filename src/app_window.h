#pragma once
#include <gtk/gtk.h>

#include <map>
#include <string>
#include <vector>

#include "device_manager.h"
#include "pipeline.h"
#include "settings.h"
#include "v4l2_device.h"

// Finestra principale "WebCam and TV Smart Vision" (GTK3, stile GNOME con header bar).
class AppWindow {
public:
    explicit AppWindow(GtkApplication* app);
    ~AppWindow();
    GtkWidget* widget() const { return window_; }
    // Auto-test: sorgente di prova, registrazione MP4 e OGG, istantanea, poi uscita.
    void runSelfTest(const std::string& outDir);

private:
    // Costruzione UI
    void buildUi();
    GtkWidget* buildSourcePage();
    GtkWidget* buildWebcamPage();
    GtkWidget* buildTvPage();
    GtkWidget* buildRecordPage();
    GtkWidget* buildOutputPage();
    void addAction(const char* name, void (AppWindow::*fn)());
    static GtkWidget* row(GtkWidget* box, const char* label, GtkWidget* w);
    static GtkWidget* heading(GtkWidget* box, const char* text);
    static GtkWidget* compactCombo();            // GtkComboBoxText con testo ellissato (non allarga la barra laterale)
    static GtkWidget* note(GtkWidget* box, const char* text);   // testo descrittivo a capo automatico

    // Popolamento elenchi
    void populateSources();
    void populateFormats();
    void populateAudioSources();
    void populateOutputs();
    void populateChannels();
    void onDevicesChanged();
    void updateDeviceStatus();          // nome periferica di cattura + segnaposto "non rilevata"
    std::string detectedDevicesText() const;

    // Sorgente e pipeline
    const VideoSource* currentSource() const;
    void onSourceChanged(bool autoStart = true);
    void startSource();
    void stopSource();
    void restartIfRunning();
    void updateSubtitle();

    // Controlli webcam (V4L2)
    void rebuildWebcamControls();
    void refreshWebcamControlValues();
    void onWebcamControlChanged(uint32_t id, int64_t value);
    void applyPreset(int preset);
    bool setControlByName(const char* needle, int64_t value);

    // TV
    void chooseChannelsFile();
    void runChannelScan();
    void onScanFinished(GObject* proc, GAsyncResult* res);

    // Registrazione / snapshot
    void toggleRecording();
    void takeSnapshot();
    std::string recordFolder() const;
    void openRecordFolder();
    static gboolean recordTimerCb(gpointer self);

    // Uscite
    void onVideoOutputToggled();
    void onAudioOutputToggled();
    void onExternalWindowToggled();
    void createLoopbackDevice();

    // Varie
    void toggleFullscreen();
    void showAbout();
    void setStatus(const std::string& text);
    void showMessage(const std::string& text, GtkMessageType type);
    void saveSettings();
    static gboolean keyPressCb(GtkWidget* w, GdkEventKey* e, gpointer self);
    static gboolean deleteCb(GtkWidget* w, GdkEvent* e, gpointer self);

    GtkApplication* app_ = nullptr;
    Settings settings_;
    DeviceManager devices_;
    MediaPipeline pipeline_;
    V4l2Device ctrlDev_;

    // Widget principali
    GtkWidget* window_ = nullptr;
    GtkWidget* headerBar_ = nullptr;
    GtkWidget* startButton_ = nullptr;
    GtkWidget* recordButton_ = nullptr;
    GtkWidget* snapshotButton_ = nullptr;
    GtkWidget* infoBar_ = nullptr;
    GtkWidget* infoLabel_ = nullptr;
    GtkWidget* sidebar_ = nullptr;
    GtkWidget* stack_ = nullptr;
    GtkWidget* recBadge_ = nullptr;
    GtkWidget* videoInfoLabel_ = nullptr;
    GtkWidget* placeholder_ = nullptr;
    GtkWidget* placeholderTitle_ = nullptr;
    GtkWidget* placeholderText_ = nullptr;
    GtkWidget* deviceLabel_ = nullptr;
    std::string lastVideoInfo_;
    GtkWidget* statusLabel_ = nullptr;
    GtkWidget* levelBar_ = nullptr;
    GtkWidget* bottomBar_ = nullptr;

    // Pagina sorgente
    GtkWidget* sourceCombo_ = nullptr;
    GtkWidget* formatRow_ = nullptr;
    GtkWidget* formatCombo_ = nullptr;
    GtkWidget* uriRow_ = nullptr;
    GtkWidget* uriEntry_ = nullptr;
    GtkWidget* audioCombo_ = nullptr;
    GtkWidget* micScale_ = nullptr;
    GtkWidget* flipCombo_ = nullptr;

    // Pagina webcam
    GtkWidget* webcamInfo_ = nullptr;
    GtkWidget* webcamControlsBox_ = nullptr;
    struct CtrlWidget { V4l2Control ctrl; GtkWidget* widget = nullptr; GtkWidget* rowBox = nullptr; };
    std::vector<CtrlWidget> ctrlWidgets_;
    bool updatingControls_ = false;

    // Pagina TV
    GtkWidget* tvInfo_ = nullptr;
    GtkWidget* channelCombo_ = nullptr;
    GtkWidget* channelsFileLabel_ = nullptr;
    GtkWidget* scanLog_ = nullptr;
    GtkWidget* delsysCombo_ = nullptr;
    GtkWidget* freqSpin_ = nullptr;
    GtkWidget* bandwidthCombo_ = nullptr;
    GtkWidget* symbolSpin_ = nullptr;
    GtkWidget* polarityCombo_ = nullptr;
    std::string channelsFile_;
    bool manualTune_ = false;

    // Pagina registrazione
    GtkWidget* fmtMp4_ = nullptr;
    GtkWidget* fmtOgg_ = nullptr;
    GtkWidget* encoderInfo_ = nullptr;
    GtkWidget* videoKbps_ = nullptr;
    GtkWidget* audioKbps_ = nullptr;
    GtkWidget* recAudioCheck_ = nullptr;
    GtkWidget* folderButton_ = nullptr;
    GtkWidget* prefixEntry_ = nullptr;
    GtkWidget* bigRecordButton_ = nullptr;
    GtkWidget* lastFileLabel_ = nullptr;
    std::string currentRecordingPath_;
    guint recordTimer_ = 0;

    // Pagina uscite
    GtkWidget* videoOutCombo_ = nullptr;
    GtkWidget* videoOutSwitch_ = nullptr;
    GtkWidget* audioOutCombo_ = nullptr;
    GtkWidget* audioOutSwitch_ = nullptr;
    GtkWidget* outVolume_ = nullptr;
    GtkWidget* outMute_ = nullptr;
    GtkWidget* extWinSwitch_ = nullptr;

    static gboolean selfTestStep(gpointer self);
    int selfTestStep_ = 0;
    std::string selfTestDir_;

    bool fullscreen_ = false;
    bool suppressSignals_ = false;
};
