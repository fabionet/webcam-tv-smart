// Registrazione audio/video: coda + thread di codifica, MP4 (H.264 + AAC) oppure OGG (Theora + Vorbis).
#pragma once
#include <deque>
#include <string>

#include "common.h"

namespace sv {

struct VideoPlanes {            // I420, stride = larghezza (luma) e larghezza/2 (croma)
    const uint8_t* y;
    const uint8_t* u;
    const uint8_t* v;
    int w, h;
};

// Scrittore di un formato contenitore; usato solo dal thread di codifica.
class IMediaWriter {
public:
    virtual ~IMediaWriter() {}
    virtual bool open(const std::wstring& path, int w, int h, double fps, int videoKbps,
                      bool audio, int audioRate, int audioKbps, std::wstring& err) = 0;
    // "slots": numero di intervalli di fotogramma coperti da questa immagine (>1 se ne sono saltati).
    virtual bool writeVideo(const VideoPlanes& p, int slots) = 0;
    virtual bool writeAudio(const int16_t* stereo, int frames) = 0;
    virtual void close() = 0;
};

IMediaWriter* createMp4Writer();
IMediaWriter* createOggWriter();

enum class RecFormat { Mp4, Ogg };

struct RecOptions {
    RecFormat format = RecFormat::Mp4;
    int videoKbps = 4000;
    int audioKbps = 128;
    bool audio = true;
    int audioRate = 48000;      // 48000 o 44100, stereo a 16 bit
    double fps = 30.0;
};

// Converte BGRA (dall'alto verso il basso) in I420 ritagliando la regione indicata.
void bgraToI420(const uint8_t* bgra, int stride, int cropX, int cropY, int w, int h,
                uint8_t* y, uint8_t* u, uint8_t* v, bool bt709);

class Recorder {
public:
    Recorder();
    ~Recorder();

    bool start(const std::wstring& path, int srcW, int srcH, const RecOptions& opt, std::wstring& err);
    void stop();                               // svuota la coda e finalizza il file (bloccante)
    bool active() const { return active_; }

    // Chiamabili da qualsiasi thread mentre la registrazione è attiva.
    void pushVideo(const uint8_t* bgra, int w, int h, int stride, double t);
    void pushAudio(const int16_t* stereo, int frames, double tEnd);

    double elapsed() const;
    long framesWritten() const { return framesWritten_; }
    long framesDropped() const { return framesDropped_; }
    bool failed() const { return failed_; }
    int outWidth() const { return w_; }
    int outHeight() const { return h_; }

private:
    struct Item {
        int type = 0;            // 0 video, 1 audio
        uint8_t* planes = nullptr;
        int slots = 1;
        std::vector<int16_t> pcm;
    };
    static DWORD WINAPI threadProc(LPVOID self);
    void run();

    IMediaWriter* writer_ = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE wake_ = nullptr;
    Lock lock_;
    std::deque<Item*> queue_;
    volatile bool active_ = false;
    volatile bool stopping_ = false;
    volatile bool failed_ = false;
    volatile long framesWritten_ = 0, framesDropped_ = 0;
    int queuedVideo_ = 0;

    RecOptions opt_;
    int srcW_ = 0, srcH_ = 0, w_ = 0, h_ = 0, cropX_ = 0, cropY_ = 0;
    bool bt709_ = false;
    double startWall_ = 0;
    double t0_ = -1;             // istante del primo fotogramma
    long lastSlot_ = -1;
    bool audioStarted_ = false;
};

} // namespace sv
