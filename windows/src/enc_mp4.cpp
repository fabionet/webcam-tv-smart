// MP4: video H.264 Baseline (minih264) + audio AAC-LC (vo-aacenc), contenitore minimp4.
#include <cstring>

#include "codecs_c.h"
#include "recorder.h"

extern "C" {
#include "cmnMemory.h"
#include "voAAC.h"
}

namespace sv {

namespace {

int mp4WriteCb(int64_t offset, const void* buffer, size_t size, void* token) {
    return static_cast<OutFile*>(token)->writeAt(offset, buffer, size) ? 0 : 1;
}

class Mp4Writer : public IMediaWriter {
public:
    ~Mp4Writer() override { close(); }

    bool open(const std::wstring& path, int w, int h, double fps, int videoKbps,
              bool audio, int audioRate, int audioKbps, std::wstring& err) override {
        w_ = w; h_ = h;
        if (!file_.open(path)) { err = L"Impossibile creare il file:\n" + path; return false; }
        mux_ = MP4E_open(0, 0, &file_, mp4WriteCb);
        if (!mux_) { err = L"Impossibile inizializzare il contenitore MP4."; return false; }
        if (mp4_h26x_write_init(&h26x_, mux_, w, h, 0) != MP4E_STATUS_OK) { err = L"Traccia video MP4 non creata."; return false; }
        h26xInit_ = true;

        // --- codificatore H.264
        H264E_create_param_t cp;
        memset(&cp, 0, sizeof cp);
        cp.width = w;
        cp.height = h;
        cp.gop = (int)(fps * 2 + 0.5);                       // un fotogramma chiave ogni 2 secondi
        cp.vbv_size_bytes = videoKbps * 1000 / 8 * 2;        // finestra di controllo del bitrate: 2 s
        cp.vbv_overflow_empty_frame_flag = 0;
        cp.vbv_underflow_stuffing_flag = 0;
        cp.fine_rate_control_flag = 0;
        cp.const_input_flag = 1;                             // non modificare i piani in ingresso
        cp.max_long_term_reference_frames = 0;
        cp.temporal_denoise_flag = 0;
        int szPersist = 0, szScratch = 0;
        if (H264E_sizeof(&cp, &szPersist, &szScratch) != H264E_STATUS_SUCCESS) { err = L"Parametri H.264 non validi (dimensioni multiple di 16?)."; return false; }
        enc_ = (H264E_persist_t*)alignedAlloc(szPersist);
        scratch_ = (H264E_scratch_t*)alignedAlloc(szScratch);
        if (!enc_ || !scratch_) { err = L"Memoria insufficiente per il codificatore H.264."; return false; }
        if (H264E_init(enc_, &cp) != H264E_STATUS_SUCCESS) { err = L"Codificatore H.264 non inizializzato."; return false; }
        memset(&run_, 0, sizeof run_);
        run_.frame_type = 0;
        // Più pixel al secondo => preimpostazione più veloce, così la codifica resta in tempo reale.
        double mbps = double(w) * h * fps / 1e6;
        run_.encode_speed = mbps > 40 ? 8 : mbps > 20 ? 6 : 4;
        run_.desired_frame_bytes = (int)(videoKbps * 1000.0 / 8.0 / fps);
        run_.qp_min = 12;
        run_.qp_max = 46;
        frameDur_ = (unsigned)(90000.0 / fps + 0.5);

        // --- codificatore AAC
        if (audio) {
            voGetAACEncAPI(&aac_);
            memOp_.Alloc = cmnMemAlloc;
            memOp_.Copy = cmnMemCopy;
            memOp_.Free = cmnMemFree;
            memOp_.Set = cmnMemSet;
            memOp_.Check = cmnMemCheck;
            VO_CODEC_INIT_USERDATA ud;
            memset(&ud, 0, sizeof ud);
            ud.memflag = VO_IMF_USERMEMOPERATOR;
            ud.memData = &memOp_;
            if (aac_.Init(&aacH_, VO_AUDIO_CodingAAC, &ud) != VO_ERR_NONE) { err = L"Codificatore AAC non inizializzato."; return false; }
            AACENC_PARAM ap;
            memset(&ap, 0, sizeof ap);
            ap.sampleRate = audioRate;
            ap.bitRate = audioKbps * 1000;
            ap.nChannels = 2;
            ap.adtsUsed = 0;                                  // AAC "grezzo" per MP4
            if (aac_.SetParam(aacH_, VO_PID_AAC_ENCPARAM, &ap) != VO_ERR_NONE) { err = L"Parametri AAC non accettati (bitrate/frequenza)."; return false; }

            MP4E_track_t tr;
            memset(&tr, 0, sizeof tr);
            tr.track_media_kind = e_audio;
            tr.language[0] = 'u'; tr.language[1] = 'n'; tr.language[2] = 'd'; tr.language[3] = 0;
            tr.object_type_indication = MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3;
            tr.time_scale = audioRate;
            tr.default_duration = 1024;
            tr.u.a.channelcount = 2;
            atrack_ = MP4E_add_track(mux_, &tr);
            // AudioSpecificConfig: AAC-LC (2), indice di frequenza, 2 canali
            int idx = audioRate == 48000 ? 3 : audioRate == 44100 ? 4 : audioRate == 32000 ? 5 : 3;
            unsigned char dsi[2] = { (unsigned char)((2 << 3) | (idx >> 1)), (unsigned char)(((idx & 1) << 7) | (2 << 3)) };
            MP4E_set_dsi(mux_, atrack_, dsi, 2);
            aout_.resize(8192);
            hasAudio_ = true;
        }
        return true;
    }

    bool writeVideo(const VideoPlanes& p, int slots) override {
        H264E_io_yuv_t yuv;
        yuv.yuv[0] = const_cast<unsigned char*>(p.y);
        yuv.yuv[1] = const_cast<unsigned char*>(p.u);
        yuv.yuv[2] = const_cast<unsigned char*>(p.v);
        yuv.stride[0] = p.w;
        yuv.stride[1] = p.w / 2;
        yuv.stride[2] = p.w / 2;
        unsigned char* data = nullptr;
        int size = 0;
        if (H264E_encode(enc_, scratch_, &run_, &yuv, &data, &size) != H264E_STATUS_SUCCESS) return false;
        if (size > 0 && mp4_h26x_write_nal(&h26x_, data, size, frameDur_ * (unsigned)(slots < 1 ? 1 : slots)) != MP4E_STATUS_OK) return false;
        return file_.ok();
    }

    bool writeAudio(const int16_t* stereo, int frames) override {
        if (!hasAudio_) return true;
        apcm_.insert(apcm_.end(), stereo, stereo + (size_t)frames * 2);
        size_t pos = 0;
        while (apcm_.size() - pos >= 2048) {                  // 1024 campioni stereo per pacchetto AAC
            VO_CODECBUFFER in, out;
            VO_AUDIO_OUTPUTINFO info;
            memset(&in, 0, sizeof in); memset(&out, 0, sizeof out); memset(&info, 0, sizeof info);
            in.Buffer = (VO_PBYTE)&apcm_[pos];
            in.Length = 2048 * sizeof(int16_t);
            aac_.SetInputData(aacH_, &in);
            out.Buffer = aout_.data();
            out.Length = (VO_U32)aout_.size();
            if (aac_.GetOutputData(aacH_, &out, &info) == VO_ERR_NONE && out.Length > 0) {
                if (MP4E_put_sample(mux_, atrack_, out.Buffer, (int)out.Length, 1024, MP4E_SAMPLE_RANDOM_ACCESS) != MP4E_STATUS_OK) return false;
            }
            pos += 2048;
        }
        apcm_.erase(apcm_.begin(), apcm_.begin() + pos);
        return file_.ok();
    }

    void close() override {
        if (mux_) { MP4E_close(mux_); mux_ = nullptr; }       // scrive gli indici (moov)
        if (h26xInit_) { mp4_h26x_write_close(&h26x_); h26xInit_ = false; }
        if (aacH_) { aac_.Uninit(aacH_); aacH_ = nullptr; }
        alignedFree(enc_); enc_ = nullptr;
        alignedFree(scratch_); scratch_ = nullptr;
        file_.close();
    }

private:
    OutFile file_;
    MP4E_mux_t* mux_ = nullptr;
    mp4_h26x_writer_t h26x_;
    bool h26xInit_ = false;
    H264E_persist_t* enc_ = nullptr;
    H264E_scratch_t* scratch_ = nullptr;
    H264E_run_param_t run_;
    unsigned frameDur_ = 3000;
    int w_ = 0, h_ = 0;

    bool hasAudio_ = false;
    VO_AUDIO_CODECAPI aac_;
    VO_MEM_OPERATOR memOp_;
    VO_HANDLE aacH_ = nullptr;
    int atrack_ = -1;
    std::vector<int16_t> apcm_;
    std::vector<unsigned char> aout_;
};

} // namespace

IMediaWriter* createMp4Writer() { return new Mp4Writer; }

} // namespace sv
