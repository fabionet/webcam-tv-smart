// OGG: video Theora (libtheora) + audio Vorbis (libvorbis), contenitore libogg.
#include <cstdlib>
#include <cstring>

#include "recorder.h"

extern "C" {
#include <ogg/ogg.h>
#include <theora/theoraenc.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
}

namespace sv {

namespace {

class OggWriter : public IMediaWriter {
public:
    ~OggWriter() override { close(); }

    bool open(const std::wstring& path, int w, int h, double fps, int videoKbps,
              bool audio, int audioRate, int audioKbps, std::wstring& err) override {
        w_ = w; h_ = h;
        if (!file_.open(path)) { err = L"Impossibile creare il file:\n" + path; return false; }
        srand(GetTickCount());
        ogg_stream_init(&to_, rand());
        toInit_ = true;

        // --- Theora
        th_info ti;
        th_info_init(&ti);
        ti.frame_width = w; ti.frame_height = h;              // già multipli di 16
        ti.pic_width = w; ti.pic_height = h; ti.pic_x = 0; ti.pic_y = 0;
        ti.fps_numerator = (ogg_uint32_t)(fps * 1000 + 0.5);
        ti.fps_denominator = 1000;
        ti.aspect_numerator = 1; ti.aspect_denominator = 1;
        ti.colorspace = TH_CS_UNSPECIFIED;
        ti.pixel_fmt = TH_PF_420;
        ti.target_bitrate = videoKbps * 1000;
        ti.quality = 0;
        ti.keyframe_granule_shift = 6;                        // fotogramma chiave almeno ogni 64
        td_ = th_encode_alloc(&ti);
        th_info_clear(&ti);
        if (!td_) { err = L"Codificatore Theora non inizializzato."; return false; }
        int spMax = 0;
        if (th_encode_ctl(td_, TH_ENCCTL_GET_SPLEVEL_MAX, &spMax, sizeof spMax) == 0)
            th_encode_ctl(td_, TH_ENCCTL_SET_SPLEVEL, &spMax, sizeof spMax);   // massima velocità: tempo reale
        int rateFlags = TH_RATECTL_DROP_FRAMES | TH_RATECTL_CAP_OVERFLOW;
        th_encode_ctl(td_, TH_ENCCTL_SET_RATE_FLAGS, &rateFlags, sizeof rateFlags);

        // --- Vorbis
        if (audio) {
            ogg_stream_init(&vo_, rand());
            voInit_ = true;
            vorbis_info_init(&vi_);
            if (vorbis_encode_init(&vi_, 2, audioRate, -1, audioKbps * 1000, -1) != 0) {
                vorbis_info_clear(&vi_);
                err = L"Codificatore Vorbis non inizializzato (bitrate/frequenza).";
                return false;
            }
            vorbis_comment_init(&vc_);
            vorbis_comment_add_tag(&vc_, "ENCODER", "WebCam and TV Smart Vision");
            vorbis_analysis_init(&vd_, &vi_);
            vorbis_block_init(&vd_, &vb_);
            hasAudio_ = true;
        }

        // --- intestazioni: prima le pagine iniziali (BOS) di ogni flusso, poi il resto
        th_comment tc;
        th_comment_init(&tc);
        ogg_packet op;
        ogg_page og;
        if (th_encode_flushheader(td_, &tc, &op) <= 0) { th_comment_clear(&tc); err = L"Intestazione Theora non generata."; return false; }
        ogg_stream_packetin(&to_, &op);
        if (ogg_stream_pageout(&to_, &og) != 1) { th_comment_clear(&tc); err = L"Errore interno Ogg."; return false; }
        writePage(og);
        ogg_packet vh[3];
        if (hasAudio_) {
            vorbis_analysis_headerout(&vd_, &vc_, &vh[0], &vh[1], &vh[2]);
            ogg_stream_packetin(&vo_, &vh[0]);
            if (ogg_stream_pageout(&vo_, &og) != 1) { th_comment_clear(&tc); err = L"Errore interno Ogg."; return false; }
            writePage(og);
        }
        while (th_encode_flushheader(td_, &tc, &op) > 0) ogg_stream_packetin(&to_, &op);
        th_comment_clear(&tc);
        if (hasAudio_) {
            ogg_stream_packetin(&vo_, &vh[1]);
            ogg_stream_packetin(&vo_, &vh[2]);
        }
        while (ogg_stream_flush(&to_, &og) > 0) writePage(og);
        if (hasAudio_) while (ogg_stream_flush(&vo_, &og) > 0) writePage(og);

        last_.resize((size_t)w * h * 3 / 2);
        return file_.ok();
    }

    bool writeVideo(const VideoPlanes& p, int slots) override {
        // Conserva una copia: serve a chiudere il flusso con un pacchetto marcato "ultimo".
        size_t ysz = (size_t)w_ * h_;
        memcpy(&last_[0], p.y, ysz);
        memcpy(&last_[ysz], p.u, ysz / 4);
        memcpy(&last_[ysz + ysz / 4], p.v, ysz / 4);
        haveLast_ = true;
        if (slots > 1) {
            int dup = slots - 1;
            th_encode_ctl(td_, TH_ENCCTL_SET_DUP_COUNT, &dup, sizeof dup);
        }
        return encodeFrame(p.y, p.u, p.v, false);
    }

    bool writeAudio(const int16_t* stereo, int frames) override {
        if (!hasAudio_) return true;
        float** buf = vorbis_analysis_buffer(&vd_, frames);
        for (int i = 0; i < frames; ++i) {
            buf[0][i] = stereo[i * 2] / 32768.0f;
            buf[1][i] = stereo[i * 2 + 1] / 32768.0f;
        }
        vorbis_analysis_wrote(&vd_, frames);
        return drainAudio();
    }

    void close() override {
        if (td_) {
            if (haveLast_ && file_.ok()) {
                size_t ysz = (size_t)w_ * h_;
                encodeFrame(&last_[0], &last_[ysz], &last_[ysz + ysz / 4], true);
            }
            ogg_page og;
            while (ogg_stream_flush(&to_, &og) > 0) writePage(og);
            th_encode_free(td_);
            td_ = nullptr;
        }
        if (hasAudio_) {
            vorbis_analysis_wrote(&vd_, 0);                    // fine del flusso audio
            drainAudio();
            ogg_page og;
            while (ogg_stream_flush(&vo_, &og) > 0) writePage(og);
            vorbis_block_clear(&vb_);
            vorbis_dsp_clear(&vd_);
            vorbis_comment_clear(&vc_);
            vorbis_info_clear(&vi_);
            hasAudio_ = false;
        }
        if (toInit_) { ogg_stream_clear(&to_); toInit_ = false; }
        if (voInit_) { ogg_stream_clear(&vo_); voInit_ = false; }
        file_.close();
    }

private:
    void writePage(const ogg_page& og) {
        file_.write(og.header, og.header_len);
        file_.write(og.body, og.body_len);
    }

    bool encodeFrame(const uint8_t* y, const uint8_t* u, const uint8_t* v, bool last) {
        th_ycbcr_buffer yc;
        yc[0].width = w_; yc[0].height = h_; yc[0].stride = w_; yc[0].data = const_cast<unsigned char*>(y);
        yc[1].width = w_ / 2; yc[1].height = h_ / 2; yc[1].stride = w_ / 2; yc[1].data = const_cast<unsigned char*>(u);
        yc[2].width = w_ / 2; yc[2].height = h_ / 2; yc[2].stride = w_ / 2; yc[2].data = const_cast<unsigned char*>(v);
        if (th_encode_ycbcr_in(td_, yc) != 0) return false;
        ogg_packet op;
        ogg_page og;
        while (th_encode_packetout(td_, last ? 1 : 0, &op) > 0) {
            ogg_stream_packetin(&to_, &op);
            while (ogg_stream_pageout(&to_, &og) > 0) writePage(og);
        }
        return file_.ok();
    }

    bool drainAudio() {
        ogg_packet op;
        ogg_page og;
        while (vorbis_analysis_blockout(&vd_, &vb_) == 1) {
            vorbis_analysis(&vb_, nullptr);
            vorbis_bitrate_addblock(&vb_);
            while (vorbis_bitrate_flushpacket(&vd_, &op)) {
                ogg_stream_packetin(&vo_, &op);
                while (ogg_stream_pageout(&vo_, &og) > 0) writePage(og);
            }
        }
        return file_.ok();
    }

    OutFile file_;
    int w_ = 0, h_ = 0;
    th_enc_ctx* td_ = nullptr;
    ogg_stream_state to_, vo_;
    bool toInit_ = false, voInit_ = false, hasAudio_ = false, haveLast_ = false;
    vorbis_info vi_;
    vorbis_comment vc_;
    vorbis_dsp_state vd_;
    vorbis_block vb_;
    std::vector<uint8_t> last_;
};

} // namespace

IMediaWriter* createOggWriter() { return new OggWriter; }

} // namespace sv
