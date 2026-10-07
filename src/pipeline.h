#pragma once
#include <gst/gst.h>
#include <gtk/gtk.h>

#include <atomic>
#include <functional>
#include <string>

#include "device_manager.h"

struct SourceConfig {
    SourceKind kind = SourceKind::Test;
    std::string devicePath;        // webcam: /dev/videoN
    VideoFormat format;            // webcam: formato scelto (facoltativo)
    int dvbAdapter = 0, dvbFrontend = 0;
    std::string dvbChannel;        // canale da channels.conf; vuoto => sintonia manuale
    std::string channelsConf;      // percorso channels.conf (vuoto => predefinito GStreamer)
    std::string delsys = "dvb-t";  // sintonia manuale
    double frequencyMHz = 0;
    int bandwidthMHz = 8;
    int symbolRateKBd = 27500;
    std::string polarity = "h";
    std::string uri;
    std::string flip = "none";     // metodo di videoflip
};

struct AudioConfig {
    bool enabled = true;
    bool fromSource = false;       // audio demuxato dalla sorgente (TV / URL)
    GstDevice* device = nullptr;   // dispositivo di cattura; nullptr => automatico
};

// Pipeline GStreamer: sorgente -> tee video/audio -> anteprima, registrazione,
// ponte verso uscite hardware (v4l2loopback, sink audio, finestra esterna).
class MediaPipeline {
public:
    enum class RecordFormat { Mp4, Ogg };
    struct RecordOptions {
        RecordFormat format = RecordFormat::Mp4;
        int videoKbps = 4000;
        int audioKbps = 128;
        bool withAudio = true;
    };

    MediaPipeline();
    ~MediaPipeline();

    GtkWidget* videoWidget() const { return videoWidget_; }

    bool start(const SourceConfig& src, const AudioConfig& audio, std::string& error);
    void stop();
    bool isRunning() const { return pipeline_ != nullptr; }
    bool hasAudio() const { return audioLinked_.load(); }

    bool startRecording(const std::string& path, const RecordOptions& opt, std::string& error);
    void stopRecording(std::function<void()> done = nullptr);
    bool isRecording() const { return record_ != nullptr; }
    gint64 recordingElapsedUs() const;

    bool startVideoOutput(const std::string& devicePath, std::string& error);
    void stopVideoOutput();
    bool videoOutputActive() const { return videoOut_ != nullptr; }

    bool startAudioOutput(GstDevice* sink, std::string& error);   // nullptr => uscita predefinita
    void stopAudioOutput();
    bool audioOutputActive() const { return audioOut_ != nullptr; }
    void setOutputVolume(double v);
    void setOutputMuted(bool m);

    bool startExternalWindow(std::string& error);
    void stopExternalWindow();
    bool externalWindowActive() const { return extWin_ != nullptr; }

    bool snapshot(const std::string& pngPath, std::string& error);
    void setFlip(const std::string& method);
    void setMicVolume(double v);

    static bool hasElement(const char* factory);
    static std::string describeEncoders(RecordFormat f);

    // Callback (thread principale)
    std::function<void(const std::string&)> onError;
    std::function<void(const std::string&)> onInfo;
    std::function<void(double rmsDb)> onAudioLevel;
    std::function<void(int w, int h, double fps)> onVideoInfo;
    std::function<void()> onExternalWindowClosed;

private:
    struct Branch {
        MediaPipeline* owner = nullptr;
        GstElement* bin = nullptr;
        GstPad* vTeePad = nullptr;
        GstPad* aTeePad = nullptr;
        bool withEos = false;
        std::atomic<int> pendingBlocks { 0 };
        std::atomic<bool> finalized { false };
        guint timeoutId = 0;
        std::function<void()> done;
    };

    bool buildSource(const SourceConfig& src, const AudioConfig& audio, std::string& error);
    Branch* addBranch(GstElement* bin, bool video, bool audio, bool withEos, GstElement* finalSink);
    void removeBranch(Branch*& slot, std::function<void()> done = nullptr);
    void linkDecodedPad(GstPad* pad);

    static gboolean busCb(GstBus* bus, GstMessage* msg, gpointer self);
    static void padAddedCb(GstElement* el, GstPad* pad, gpointer self);
    static void capsNotifyCb(GObject* pad, GParamSpec* pspec, gpointer self);
    static gboolean videoInfoIdle(gpointer self);
    static GstPadProbeReturn blockProbeCb(GstPad* pad, GstPadProbeInfo* info, gpointer branch);
    static GstPadProbeReturn eosProbeCb(GstPad* pad, GstPadProbeInfo* info, gpointer branch);
    static gboolean finalizeBranchIdle(gpointer branch);
    static gboolean extWindowDeleteCb(GtkWidget* w, GdkEvent* e, gpointer self);
    static gboolean extWindowKeyCb(GtkWidget* w, GdkEventKey* e, gpointer self);

    GstElement* pipeline_ = nullptr;
    GstElement* vtee_ = nullptr;
    GstElement* atee_ = nullptr;
    GstElement* vconvIn_ = nullptr;   // ingresso catena video (sink pad da collegare)
    GstElement* aconvIn_ = nullptr;   // ingresso catena audio
    GstElement* videoflip_ = nullptr;
    GstElement* micVolume_ = nullptr;
    GstElement* videoSink_ = nullptr; // gtksink, conservato tra un avvio e l'altro
    GtkWidget* videoWidget_ = nullptr;
    guint busWatch_ = 0;

    Branch* record_ = nullptr;
    Branch* videoOut_ = nullptr;
    Branch* audioOut_ = nullptr;
    Branch* extWin_ = nullptr;
    GstElement* outVolume_ = nullptr;
    GtkWidget* extWindow_ = nullptr;
    gint64 recordStartUs_ = 0;
    std::atomic<bool> audioLinked_ { false };
    bool sourceAudioExpected_ = false;
    double outVolumeValue_ = 1.0;
    bool outMuted_ = false;
    double micVolumeValue_ = 1.0;
    std::string pendingFlip_ = "none";
};
