#include "pipeline.h"

#include <gst/video/video.h>

#include <cmath>
#include <cstring>

#if GST_CHECK_VERSION(1, 20, 0)
#define REQUEST_PAD(el, tmpl) gst_element_request_pad_simple(el, tmpl)
#else
#define REQUEST_PAD(el, tmpl) gst_element_get_request_pad(el, tmpl)
#endif

namespace {
std::string gstErrorText(GstMessage* msg) {
    GError* err = nullptr;
    gchar* dbg = nullptr;
    gst_message_parse_error(msg, &err, &dbg);
    std::string s = err ? err->message : "errore sconosciuto";
    if (dbg) {
        // Mostra solo l'ultima riga del debug, di solito la più utile.
        std::string d(dbg);
        auto nl = d.rfind('\n');
        if (nl != std::string::npos) d = d.substr(nl + 1);
        s += " (" + d + ")";
    }
    if (err) g_error_free(err);
    g_free(dbg);
    return s;
}

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}
} // namespace

bool MediaPipeline::hasElement(const char* factory) {
    GstElementFactory* f = gst_element_factory_find(factory);
    if (!f) return false;
    gst_object_unref(f);
    return true;
}

std::string MediaPipeline::describeEncoders(RecordFormat f) {
    if (f == RecordFormat::Ogg) return "Theora + Vorbis (oggmux)";
    std::string v = hasElement("x264enc") ? "x264" : hasElement("vah264enc") ? "VA-API H.264" :
                    hasElement("vaapih264enc") ? "VA-API H.264" : hasElement("openh264enc") ? "OpenH264" : "nessun encoder H.264!";
    std::string a = hasElement("avenc_aac") ? "AAC (ffmpeg)" : hasElement("voaacenc") ? "AAC (vo-aacenc)" :
                    hasElement("fdkaacenc") ? "AAC (fdk)" : "nessun encoder AAC!";
    return v + " + " + a + " (mp4mux)";
}

MediaPipeline::MediaPipeline() {
    videoSink_ = gst_element_factory_make("gtksink", "preview");
    if (!videoSink_) {
        // Fallback: senza gtksink non c'è anteprima integrata.
        videoWidget_ = gtk_label_new("Plugin GStreamer 'gtksink' mancante (pacchetto gstreamer1.0-gtk3)");
        return;
    }
    gst_object_ref_sink(videoSink_);
    g_object_set(videoSink_, "force-aspect-ratio", TRUE, nullptr);
    g_object_get(videoSink_, "widget", &videoWidget_, nullptr);   // riferimento nostro
}

MediaPipeline::~MediaPipeline() {
    stop();
    if (extWindow_) gtk_widget_destroy(extWindow_);
    if (videoSink_) gst_object_unref(videoSink_);
}

/* ------------------------------------------------------------------ avvio */

bool MediaPipeline::start(const SourceConfig& src, const AudioConfig& audio, std::string& error) {
    stop();
    pipeline_ = gst_pipeline_new("smartvision");
    audioLinked_ = false;
    sourceAudioExpected_ = false;
    pendingFlip_ = src.flip;

    // Catena video comune: ingresso -> videoflip -> tee -> anteprima
    vconvIn_ = gst_element_factory_make("videoconvert", "vconv_in");
    videoflip_ = gst_element_factory_make("videoflip", "flip");
    GstElement* vconv2 = gst_element_factory_make("videoconvert", "vconv2");
    vtee_ = gst_element_factory_make("tee", "vtee");
    GstElement* pq = gst_element_factory_make("queue", "preview_q");
    GstElement* pconv = gst_element_factory_make("videoconvert", "preview_conv");
    if (!vconvIn_ || !videoflip_ || !vconv2 || !vtee_ || !pq || !pconv || !videoSink_) {
        error = "Elementi GStreamer di base mancanti (videoconvert/videoflip/tee/gtksink).";
        stop();
        return false;
    }
    g_object_set(vtee_, "allow-not-linked", TRUE, nullptr);
    g_object_set(pq, "leaky", 2, "max-size-buffers", 3, "max-size-time", guint64(0), "max-size-bytes", 0, nullptr);
    gst_util_set_object_arg(G_OBJECT(videoflip_), "method", src.flip.c_str());
    gst_bin_add_many(GST_BIN(pipeline_), vconvIn_, videoflip_, vconv2, vtee_, pq, pconv, videoSink_, nullptr);
    if (!gst_element_link_many(vconvIn_, videoflip_, vconv2, vtee_, pq, pconv, videoSink_, nullptr)) {
        error = "Impossibile collegare la catena video.";
        stop();
        return false;
    }
    GstPad* teeSink = gst_element_get_static_pad(vtee_, "sink");
    g_signal_connect(teeSink, "notify::caps", G_CALLBACK(capsNotifyCb), this);
    gst_object_unref(teeSink);

    // Catena audio comune: ingresso -> convert/resample -> volume -> level -> tee
    aconvIn_ = gst_element_factory_make("audioconvert", "aconv_in");
    GstElement* ares = gst_element_factory_make("audioresample", "ares");
    GstElement* acaps = gst_element_factory_make("capsfilter", "acaps");
    micVolume_ = gst_element_factory_make("volume", "micvol");
    GstElement* level = gst_element_factory_make("level", "level");
    atee_ = gst_element_factory_make("tee", "atee");
    if (!aconvIn_ || !ares || !acaps || !micVolume_ || !level || !atee_) {
        error = "Elementi GStreamer audio mancanti (audioconvert/audioresample/level).";
        stop();
        return false;
    }
    GstCaps* ac = gst_caps_from_string("audio/x-raw,rate=48000,channels=2,format=S16LE,layout=interleaved");
    g_object_set(acaps, "caps", ac, nullptr);
    gst_caps_unref(ac);
    g_object_set(level, "interval", guint64(100 * GST_MSECOND), "post-messages", TRUE, nullptr);
    g_object_set(micVolume_, "volume", micVolumeValue_, nullptr);
    g_object_set(atee_, "allow-not-linked", TRUE, nullptr);
    gst_bin_add_many(GST_BIN(pipeline_), aconvIn_, ares, acaps, micVolume_, level, atee_, nullptr);
    if (!gst_element_link_many(aconvIn_, ares, acaps, micVolume_, level, atee_, nullptr)) {
        error = "Impossibile collegare la catena audio.";
        stop();
        return false;
    }

    if (!buildSource(src, audio, error)) {
        stop();
        return false;
    }

    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline_));
    busWatch_ = gst_bus_add_watch(bus, busCb, this);
    gst_object_unref(bus);

    GstStateChangeReturn r = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (r == GST_STATE_CHANGE_FAILURE) {
        error = "La sorgente non si avvia (dispositivo occupato o formato non supportato).";
        stop();
        return false;
    }
    return true;
}

bool MediaPipeline::buildSource(const SourceConfig& src, const AudioConfig& audio, std::string& error) {
    GError* err = nullptr;
    GstElement* vsrc = nullptr;   // elemento con pad "src" statico (collegato subito)
    GstElement* dec = nullptr;    // elemento a pad dinamici (decodebin/uridecodebin)

    switch (src.kind) {
        case SourceKind::Test: {
            // timeoverlay (plugin pango, pacchetto gstreamer1.0-x) è facoltativo: senza, la mira resta senza orologio.
            std::string desc = "videotestsrc is-live=true pattern=smpte ! video/x-raw,width=1280,height=720,framerate=30/1";
            if (hasElement("timeoverlay")) desc += " ! timeoverlay halignment=right valignment=bottom";
            vsrc = gst_parse_bin_from_description(desc.c_str(), TRUE, &err);
            break;
        }
        case SourceKind::Webcam: {
            GstElement* v4l2 = gst_element_factory_make("v4l2src", "v4l2");
            GstElement* cf = gst_element_factory_make("capsfilter", "srccaps");
            dec = gst_element_factory_make("decodebin", "dec");
            if (!v4l2 || !cf || !dec) { error = "Plugin v4l2src/decodebin mancanti."; return false; }
            g_object_set(v4l2, "device", src.devicePath.c_str(), nullptr);
            if (src.format.valid()) {
                GstCaps* caps = gst_caps_from_string(src.format.capsString().c_str());
                g_object_set(cf, "caps", caps, nullptr);
                gst_caps_unref(caps);
            }
            gst_bin_add_many(GST_BIN(pipeline_), v4l2, cf, dec, nullptr);
            if (!gst_element_link_many(v4l2, cf, dec, nullptr)) { error = "Impossibile collegare v4l2src."; return false; }
            break;
        }
        case SourceKind::Dvb: {
            if (!src.dvbChannel.empty()) {
                if (!src.channelsConf.empty()) g_setenv("GST_DVB_CHANNELS_CONF", src.channelsConf.c_str(), TRUE);
                gchar* esc = g_uri_escape_string(src.dvbChannel.c_str(), nullptr, FALSE);
                std::string uri = std::string("dvb://") + esc;
                g_free(esc);
                dec = gst_element_factory_make("uridecodebin", "dec");
                if (!dec) { error = "Plugin uridecodebin mancante."; return false; }
                g_object_set(dec, "uri", uri.c_str(), nullptr);
                gst_bin_add(GST_BIN(pipeline_), dec);
            } else {
                GstElement* dvb = gst_element_factory_make("dvbsrc", "dvb");
                dec = gst_element_factory_make("decodebin", "dec");
                if (!dvb || !dec) { error = "Plugin dvbsrc mancante (gstreamer1.0-plugins-bad)."; return false; }
                bool sat = src.delsys == "dvb-s" || src.delsys == "dvb-s2";
                guint freq = sat ? guint(src.frequencyMHz * 1000.0) : guint(src.frequencyMHz * 1000000.0);
                g_object_set(dvb, "adapter", src.dvbAdapter, "frontend", src.dvbFrontend,
                             "frequency", freq, "bandwidth-hz", guint(src.bandwidthMHz * 1000000),
                             "symbol-rate", guint(src.symbolRateKBd), nullptr);
                gst_util_set_object_arg(G_OBJECT(dvb), "delsys", src.delsys.c_str());
                gst_util_set_object_arg(G_OBJECT(dvb), "polarity", src.polarity.c_str());
                gst_bin_add_many(GST_BIN(pipeline_), dvb, dec, nullptr);
                if (!gst_element_link(dvb, dec)) { error = "Impossibile collegare dvbsrc."; return false; }
            }
            sourceAudioExpected_ = audio.enabled && audio.fromSource;
            break;
        }
        case SourceKind::Uri: {
            dec = gst_element_factory_make("uridecodebin", "dec");
            if (!dec) { error = "Plugin uridecodebin mancante."; return false; }
            g_object_set(dec, "uri", src.uri.c_str(), nullptr);
            gst_bin_add(GST_BIN(pipeline_), dec);
            sourceAudioExpected_ = audio.enabled && audio.fromSource;
            break;
        }
    }
    if (err) {
        error = std::string("Impossibile creare la sorgente video: ") + err->message;
        g_error_free(err);
        return false;
    }
    if (dec) {
        g_signal_connect(dec, "pad-added", G_CALLBACK(padAddedCb), this);
    } else if (vsrc) {
        gst_bin_add(GST_BIN(pipeline_), vsrc);
        if (!gst_element_link(vsrc, vconvIn_)) { error = "Impossibile collegare la sorgente video."; return false; }
    } else {
        error = "Sorgente video non creata.";
        return false;
    }

    // Audio da dispositivo di cattura (webcam) o generatore (prova)
    if (audio.enabled && !sourceAudioExpected_) {
        GstElement* asrc = nullptr;
        if (src.kind == SourceKind::Test) {
            asrc = gst_parse_bin_from_description("audiotestsrc is-live=true wave=sine freq=440 volume=0.05", TRUE, nullptr);
        } else if (audio.device) {
            asrc = gst_device_create_element(audio.device, "asrc");
        } else {
            asrc = gst_element_factory_make("autoaudiosrc", "asrc");
        }
        if (asrc) {
            gst_bin_add(GST_BIN(pipeline_), asrc);
            if (gst_element_link(asrc, aconvIn_)) audioLinked_ = true;
            else if (onInfo) onInfo("Sorgente audio non collegabile: registrazione senza audio.");
        }
    }
    return true;
}

void MediaPipeline::padAddedCb(GstElement*, GstPad* pad, gpointer self) {
    static_cast<MediaPipeline*>(self)->linkDecodedPad(pad);
}

void MediaPipeline::linkDecodedPad(GstPad* pad) {
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps) caps = gst_pad_query_caps(pad, nullptr);
    const gchar* name = gst_caps_get_size(caps) ? gst_structure_get_name(gst_caps_get_structure(caps, 0)) : "";
    GstElement* target = nullptr;
    if (g_str_has_prefix(name, "video/x-raw")) target = vconvIn_;
    else if (g_str_has_prefix(name, "audio/x-raw") && sourceAudioExpected_) target = aconvIn_;
    gst_caps_unref(caps);
    if (!target) return;
    GstPad* sink = gst_element_get_static_pad(target, "sink");
    if (!gst_pad_is_linked(sink)) {
        if (gst_pad_link(pad, sink) == GST_PAD_LINK_OK && target == aconvIn_) audioLinked_ = true;
    }
    gst_object_unref(sink);
}

void MediaPipeline::stop() {
    if (!pipeline_) return;
    for (Branch** slot : { &record_, &videoOut_, &audioOut_, &extWin_ }) {
        Branch* b = *slot;
        *slot = nullptr;
        if (!b) continue;
        b->finalized = true;
        if (b->timeoutId) g_source_remove(b->timeoutId);
        gst_object_unref(b->bin);
        if (b->vTeePad) gst_object_unref(b->vTeePad);
        if (b->aTeePad) gst_object_unref(b->aTeePad);
        delete b;   // gli elementi vengono distrutti insieme alla pipeline
    }
    outVolume_ = nullptr;
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    if (busWatch_) {
        GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline_));
        gst_bus_remove_watch(bus);
        gst_object_unref(bus);
        busWatch_ = 0;
    }
    // Il gtksink sopravvive alla pipeline (il widget è impacchettato nella finestra).
    if (videoSink_ && GST_OBJECT_PARENT(videoSink_) == GST_OBJECT(pipeline_)) gst_bin_remove(GST_BIN(pipeline_), videoSink_);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    vtee_ = atee_ = vconvIn_ = aconvIn_ = videoflip_ = micVolume_ = nullptr;
    audioLinked_ = false;
    if (extWindow_) {
        gtk_widget_destroy(extWindow_);
        extWindow_ = nullptr;
    }
}

/* -------------------------------------------------------------------- bus */

gboolean MediaPipeline::busCb(GstBus*, GstMessage* msg, gpointer self) {
    auto* p = static_cast<MediaPipeline*>(self);
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR:
            if (p->onError) p->onError(gstErrorText(msg));
            break;
        case GST_MESSAGE_WARNING: {
            GError* err = nullptr;
            gst_message_parse_warning(msg, &err, nullptr);
            if (err) {
                if (p->onInfo) p->onInfo(std::string("Avviso: ") + err->message);
                g_error_free(err);
            }
            break;
        }
        case GST_MESSAGE_ELEMENT: {
            const GstStructure* s = gst_message_get_structure(msg);
            if (s && gst_structure_has_name(s, "level") && p->onAudioLevel) {
                const GValue* rms = gst_structure_get_value(s, "rms");
                double db = -90;
                if (rms && G_VALUE_HOLDS(rms, g_value_array_get_type())) {
                    auto* arr = static_cast<GValueArray*>(g_value_get_boxed(rms));
                    for (guint i = 0; i < arr->n_values; ++i)
                        db = std::max(db, g_value_get_double(g_value_array_get_nth(arr, i)));
                } else if (rms && GST_VALUE_HOLDS_LIST(rms)) {
                    for (guint i = 0; i < gst_value_list_get_size(rms); ++i)
                        db = std::max(db, g_value_get_double(gst_value_list_get_value(rms, i)));
                }
                p->onAudioLevel(db);
            }
            break;
        }
        default:
            break;
    }
    return TRUE;
}

void MediaPipeline::capsNotifyCb(GObject*, GParamSpec*, gpointer self) {
    g_idle_add(videoInfoIdle, self);
}

gboolean MediaPipeline::videoInfoIdle(gpointer self) {
    auto* p = static_cast<MediaPipeline*>(self);
    if (!p->vtee_ || !p->onVideoInfo) return G_SOURCE_REMOVE;
    GstPad* pad = gst_element_get_static_pad(p->vtee_, "sink");
    GstCaps* caps = gst_pad_get_current_caps(pad);
    gst_object_unref(pad);
    if (!caps) return G_SOURCE_REMOVE;
    GstVideoInfo info;
    if (gst_video_info_from_caps(&info, caps)) {
        double fps = info.fps_d ? double(info.fps_n) / info.fps_d : 0;
        p->onVideoInfo(info.width, info.height, fps);
    }
    gst_caps_unref(caps);
    return G_SOURCE_REMOVE;
}

/* --------------------------------------------------------- rami dinamici */

MediaPipeline::Branch* MediaPipeline::addBranch(GstElement* bin, bool video, bool audio, bool withEos, GstElement* finalSink) {
    auto* b = new Branch;
    b->owner = this;
    b->bin = GST_ELEMENT(gst_object_ref(bin));   // riferimento nostro: sopravvive alla pipeline
    b->withEos = withEos;
    gst_bin_add(GST_BIN(pipeline_), bin);
    if (video) {
        b->vTeePad = REQUEST_PAD(vtee_, "src_%u");
        GstPad* sink = gst_element_get_static_pad(bin, "vsink");
        gst_pad_link(b->vTeePad, sink);
        gst_object_unref(sink);
    }
    if (audio) {
        b->aTeePad = REQUEST_PAD(atee_, "src_%u");
        GstPad* sink = gst_element_get_static_pad(bin, "asink");
        gst_pad_link(b->aTeePad, sink);
        gst_object_unref(sink);
    }
    if (withEos && finalSink) {
        GstPad* sp = gst_element_get_static_pad(finalSink, "sink");
        gst_pad_add_probe(sp, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, eosProbeCb, b, nullptr);
        gst_object_unref(sp);
    }
    gst_element_sync_state_with_parent(bin);
    return b;
}

void MediaPipeline::removeBranch(Branch*& slot, std::function<void()> done) {
    Branch* b = slot;
    slot = nullptr;
    if (!b) { if (done) done(); return; }
    b->done = std::move(done);
    int n = (b->vTeePad ? 1 : 0) + (b->aTeePad ? 1 : 0);
    b->pendingBlocks = n;
    if (b->withEos) b->timeoutId = g_timeout_add(15000, finalizeBranchIdle, b);   // rete di sicurezza (encoder lenti)
    if (n == 0) { g_idle_add(finalizeBranchIdle, b); return; }
    if (b->vTeePad) gst_pad_add_probe(b->vTeePad, GST_PAD_PROBE_TYPE_IDLE, blockProbeCb, b, nullptr);
    if (b->aTeePad) gst_pad_add_probe(b->aTeePad, GST_PAD_PROBE_TYPE_IDLE, blockProbeCb, b, nullptr);
}

GstPadProbeReturn MediaPipeline::blockProbeCb(GstPad* pad, GstPadProbeInfo*, gpointer branch) {
    auto* b = static_cast<Branch*>(branch);
    GstPad* peer = gst_pad_get_peer(pad);
    if (peer) {
        gst_pad_unlink(pad, peer);
        if (b->withEos) gst_pad_send_event(peer, gst_event_new_eos());   // il muxer finalizza il file
        gst_object_unref(peer);
    }
    if (--b->pendingBlocks == 0 && !b->withEos) g_idle_add(finalizeBranchIdle, b);
    return GST_PAD_PROBE_REMOVE;
}

GstPadProbeReturn MediaPipeline::eosProbeCb(GstPad*, GstPadProbeInfo* info, gpointer branch) {
    if (GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) == GST_EVENT_EOS) {
        g_idle_add(finalizeBranchIdle, branch);
        return GST_PAD_PROBE_REMOVE;
    }
    return GST_PAD_PROBE_OK;
}

gboolean MediaPipeline::finalizeBranchIdle(gpointer branch) {
    auto* b = static_cast<Branch*>(branch);
    if (b->finalized.exchange(true)) return G_SOURCE_REMOVE;
    if (b->timeoutId) { g_source_remove(b->timeoutId); b->timeoutId = 0; }
    MediaPipeline* p = b->owner;
    gst_element_set_state(b->bin, GST_STATE_NULL);
    if (p->pipeline_) gst_bin_remove(GST_BIN(p->pipeline_), b->bin);
    if (b->vTeePad) {
        if (p->vtee_) gst_element_release_request_pad(p->vtee_, b->vTeePad);
        gst_object_unref(b->vTeePad);
    }
    if (b->aTeePad) {
        if (p->atee_) gst_element_release_request_pad(p->atee_, b->aTeePad);
        gst_object_unref(b->aTeePad);
    }
    gst_object_unref(b->bin);
    auto done = std::move(b->done);
    delete b;
    if (done) done();
    return G_SOURCE_REMOVE;
}

/* ---------------------------------------------------------- registrazione */

bool MediaPipeline::startRecording(const std::string& path, const RecordOptions& opt, std::string& error) {
    if (!pipeline_) { error = "Nessuna sorgente attiva."; return false; }
    if (record_) { error = "Registrazione già in corso."; return false; }
    bool withAudio = opt.withAudio && audioLinked_;
    std::string venc, aenc, mux;
    if (opt.format == RecordFormat::Mp4) {
        if (hasElement("x264enc"))
            venc = "x264enc bitrate=" + std::to_string(opt.videoKbps) + " tune=zerolatency speed-preset=veryfast key-int-max=60 ! h264parse";
        else if (hasElement("vah264enc"))
            venc = "vah264enc bitrate=" + std::to_string(opt.videoKbps) + " ! h264parse";
        else if (hasElement("vaapih264enc"))
            venc = "vaapih264enc bitrate=" + std::to_string(opt.videoKbps) + " ! h264parse";
        else if (hasElement("openh264enc"))
            venc = "openh264enc bitrate=" + std::to_string(opt.videoKbps * 1000) + " ! h264parse";
        else { error = "Nessun encoder H.264 disponibile (installa gstreamer1.0-plugins-ugly)."; return false; }
        if (hasElement("avenc_aac")) aenc = "avenc_aac bitrate=" + std::to_string(opt.audioKbps * 1000) + " ! aacparse";
        else if (hasElement("voaacenc")) aenc = "voaacenc bitrate=" + std::to_string(opt.audioKbps * 1000) + " ! aacparse";
        else if (hasElement("fdkaacenc")) aenc = "fdkaacenc bitrate=" + std::to_string(opt.audioKbps * 1000) + " ! aacparse";
        else withAudio = false;
        mux = "mp4mux name=mux";
    } else {
        if (!hasElement("theoraenc") || !hasElement("oggmux")) { error = "Encoder Theora/Ogg mancante (gstreamer1.0-plugins-base)."; return false; }
        venc = "theoraenc bitrate=" + std::to_string(opt.videoKbps) + " speed-level=3 keyframe-auto=true drop-frames=true";
        aenc = hasElement("vorbisenc") ? "vorbisenc bitrate=" + std::to_string(opt.audioKbps * 1000) : "";
        if (aenc.empty()) withAudio = false;
        mux = "oggmux name=mux";
    }
    std::string desc =
        "queue name=vq leaky=downstream max-size-buffers=0 max-size-bytes=0 max-size-time=3000000000 ! "
        "videoconvert ! video/x-raw,format=I420 ! " + venc + " ! " + mux + " ! filesink name=fsink location=" + quote(path);
    if (withAudio)
        desc += "  queue name=aq max-size-buffers=0 max-size-bytes=0 max-size-time=3000000000 ! audioconvert ! audioresample ! " + aenc + " ! mux.";

    GError* err = nullptr;
    GstElement* bin = gst_parse_bin_from_description(desc.c_str(), FALSE, &err);
    if (!bin) {
        error = std::string("Impossibile creare il ramo di registrazione: ") + (err ? err->message : "");
        if (err) g_error_free(err);
        return false;
    }
    gst_element_set_name(bin, "recbin");
    GstElement* vq = gst_bin_get_by_name(GST_BIN(bin), "vq");
    GstPad* vqs = gst_element_get_static_pad(vq, "sink");
    gst_element_add_pad(bin, gst_ghost_pad_new("vsink", vqs));
    gst_object_unref(vqs);
    gst_object_unref(vq);
    if (withAudio) {
        GstElement* aq = gst_bin_get_by_name(GST_BIN(bin), "aq");
        GstPad* aqs = gst_element_get_static_pad(aq, "sink");
        gst_element_add_pad(bin, gst_ghost_pad_new("asink", aqs));
        gst_object_unref(aqs);
        gst_object_unref(aq);
    }
    GstElement* fsink = gst_bin_get_by_name(GST_BIN(bin), "fsink");
    record_ = addBranch(bin, true, withAudio, true, fsink);
    gst_object_unref(fsink);
    recordStartUs_ = g_get_monotonic_time();
    return true;
}

void MediaPipeline::stopRecording(std::function<void()> done) {
    removeBranch(record_, std::move(done));
}

gint64 MediaPipeline::recordingElapsedUs() const {
    return record_ ? g_get_monotonic_time() - recordStartUs_ : 0;
}

/* ----------------------------------------------------------- uscite ponte */

bool MediaPipeline::startVideoOutput(const std::string& devicePath, std::string& error) {
    if (!pipeline_) { error = "Nessuna sorgente attiva."; return false; }
    if (videoOut_) stopVideoOutput();
    if (!hasElement("v4l2sink")) { error = "Plugin v4l2sink mancante (gstreamer1.0-plugins-good)."; return false; }
    std::string desc = "queue name=vq leaky=downstream max-size-buffers=2 ! videoconvert ! videoscale ! "
                       "v4l2sink name=vsink sync=false device=" + quote(devicePath);
    GError* err = nullptr;
    GstElement* bin = gst_parse_bin_from_description(desc.c_str(), FALSE, &err);
    if (!bin) {
        error = std::string("Uscita video: ") + (err ? err->message : "");
        if (err) g_error_free(err);
        return false;
    }
    GstElement* vq = gst_bin_get_by_name(GST_BIN(bin), "vq");
    GstPad* vqs = gst_element_get_static_pad(vq, "sink");
    gst_element_add_pad(bin, gst_ghost_pad_new("vsink", vqs));
    gst_object_unref(vqs);
    gst_object_unref(vq);
    videoOut_ = addBranch(bin, true, false, false, nullptr);
    return true;
}

void MediaPipeline::stopVideoOutput() { removeBranch(videoOut_); }

bool MediaPipeline::startAudioOutput(GstDevice* sinkDev, std::string& error) {
    if (!pipeline_) { error = "Nessuna sorgente attiva."; return false; }
    if (!audioLinked_) { error = "Nessun audio disponibile dalla sorgente."; return false; }
    if (audioOut_) stopAudioOutput();
    GstElement* bin = gst_bin_new("audioout");
    GstElement* q = gst_element_factory_make("queue", "aq");
    GstElement* conv = gst_element_factory_make("audioconvert", nullptr);
    GstElement* res = gst_element_factory_make("audioresample", nullptr);
    outVolume_ = gst_element_factory_make("volume", "outvol");
    GstElement* sink = sinkDev ? gst_device_create_element(sinkDev, "asink") : gst_element_factory_make("autoaudiosink", "asink");
    if (!q || !conv || !res || !outVolume_ || !sink) {
        error = "Impossibile creare l'uscita audio.";
        gst_object_unref(bin);
        return false;
    }
    g_object_set(q, "leaky", 2, "max-size-time", guint64(500 * GST_MSECOND), nullptr);
    g_object_set(outVolume_, "volume", outVolumeValue_, "mute", outMuted_ ? TRUE : FALSE, nullptr);
    gst_bin_add_many(GST_BIN(bin), q, conv, res, outVolume_, sink, nullptr);
    gst_element_link_many(q, conv, res, outVolume_, sink, nullptr);
    GstPad* qs = gst_element_get_static_pad(q, "sink");
    gst_element_add_pad(bin, gst_ghost_pad_new("asink", qs));
    gst_object_unref(qs);
    audioOut_ = addBranch(bin, false, true, false, nullptr);
    return true;
}

void MediaPipeline::stopAudioOutput() {
    outVolume_ = nullptr;
    removeBranch(audioOut_);
}

void MediaPipeline::setOutputVolume(double v) {
    outVolumeValue_ = v;
    if (outVolume_) g_object_set(outVolume_, "volume", v, nullptr);
}

void MediaPipeline::setOutputMuted(bool m) {
    outMuted_ = m;
    if (outVolume_) g_object_set(outVolume_, "mute", m ? TRUE : FALSE, nullptr);
}

bool MediaPipeline::startExternalWindow(std::string& error) {
    if (!pipeline_) { error = "Nessuna sorgente attiva."; return false; }
    if (extWin_) return true;
    GError* err = nullptr;
    GstElement* bin = gst_parse_bin_from_description(
        "queue name=vq leaky=downstream max-size-buffers=2 ! videoconvert ! gtksink name=extsink force-aspect-ratio=true", FALSE, &err);
    if (!bin) {
        error = std::string("Finestra esterna: ") + (err ? err->message : "");
        if (err) g_error_free(err);
        return false;
    }
    GstElement* vq = gst_bin_get_by_name(GST_BIN(bin), "vq");
    GstPad* vqs = gst_element_get_static_pad(vq, "sink");
    gst_element_add_pad(bin, gst_ghost_pad_new("vsink", vqs));
    gst_object_unref(vqs);
    gst_object_unref(vq);
    GstElement* sink = gst_bin_get_by_name(GST_BIN(bin), "extsink");
    GtkWidget* w = nullptr;
    g_object_get(sink, "widget", &w, nullptr);
    gst_object_unref(sink);

    extWindow_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(extWindow_), "WebCam and TV Smart Vision – Uscita esterna (F11 schermo intero)");
    gtk_window_set_default_size(GTK_WINDOW(extWindow_), 640, 360);
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, "window { background: black; }", -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(extWindow_), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    gtk_container_add(GTK_CONTAINER(extWindow_), w);
    g_object_unref(w);
    g_signal_connect(extWindow_, "delete-event", G_CALLBACK(extWindowDeleteCb), this);
    g_signal_connect(extWindow_, "key-press-event", G_CALLBACK(extWindowKeyCb), this);
    gtk_widget_show_all(extWindow_);

    extWin_ = addBranch(bin, true, false, false, nullptr);
    return true;
}

void MediaPipeline::stopExternalWindow() {
    removeBranch(extWin_);
    if (extWindow_) {
        gtk_widget_destroy(extWindow_);
        extWindow_ = nullptr;
    }
}

gboolean MediaPipeline::extWindowDeleteCb(GtkWidget*, GdkEvent*, gpointer self) {
    auto* p = static_cast<MediaPipeline*>(self);
    p->extWindow_ = nullptr;   // GTK distrugge la finestra dopo il ritorno
    p->removeBranch(p->extWin_);
    if (p->onExternalWindowClosed) p->onExternalWindowClosed();
    return FALSE;
}

gboolean MediaPipeline::extWindowKeyCb(GtkWidget* w, GdkEventKey* e, gpointer) {
    if (e->keyval == GDK_KEY_F11 || e->keyval == GDK_KEY_f) {
        GdkWindow* gw = gtk_widget_get_window(w);
        bool fs = gw && (gdk_window_get_state(gw) & GDK_WINDOW_STATE_FULLSCREEN);
        if (fs) gtk_window_unfullscreen(GTK_WINDOW(w)); else gtk_window_fullscreen(GTK_WINDOW(w));
        return TRUE;
    }
    if (e->keyval == GDK_KEY_Escape) { gtk_window_unfullscreen(GTK_WINDOW(w)); return TRUE; }
    return FALSE;
}

/* ---------------------------------------------------------------- varie */

bool MediaPipeline::snapshot(const std::string& pngPath, std::string& error) {
    if (!pipeline_ || !videoSink_) { error = "Nessuna sorgente attiva."; return false; }
    GstSample* sample = nullptr;
    g_object_get(videoSink_, "last-sample", &sample, nullptr);
    if (!sample) { error = "Nessun fotogramma disponibile."; return false; }
    GstCaps* png = gst_caps_from_string("image/png");
    GError* err = nullptr;
    GstSample* out = gst_video_convert_sample(sample, png, 3 * GST_SECOND, &err);
    gst_caps_unref(png);
    gst_sample_unref(sample);
    if (!out) {
        error = std::string("Conversione PNG fallita: ") + (err ? err->message : "");
        if (err) g_error_free(err);
        return false;
    }
    GstBuffer* buf = gst_sample_get_buffer(out);
    GstMapInfo map;
    bool ok = false;
    if (gst_buffer_map(buf, &map, GST_MAP_READ)) {
        ok = g_file_set_contents(pngPath.c_str(), reinterpret_cast<const gchar*>(map.data), map.size, &err);
        gst_buffer_unmap(buf, &map);
    }
    gst_sample_unref(out);
    if (!ok) {
        error = std::string("Scrittura fallita: ") + (err ? err->message : "");
        if (err) g_error_free(err);
    }
    return ok;
}

void MediaPipeline::setFlip(const std::string& method) {
    pendingFlip_ = method;
    if (videoflip_) gst_util_set_object_arg(G_OBJECT(videoflip_), "method", method.c_str());
}

void MediaPipeline::setMicVolume(double v) {
    micVolumeValue_ = v;
    if (micVolume_) g_object_set(micVolume_, "volume", v, nullptr);
}
