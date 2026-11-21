// WebCam and TV Smart Vision — edizione Windows. Punto d'ingresso.
#include <objbase.h>
#include <shellapi.h>

#include "app.h"
#include "imaging.h"

using namespace sv;

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
#if defined(__i386__)
    // La build a 32 bit usa istruzioni SSE2 (Pentium 4, Pentium M, Athlon 64 o successivi).
    if (!IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE)) {
        MessageBoxW(nullptr, L"Questo programma richiede un processore con istruzioni SSE2\n(Pentium 4, Pentium M, Athlon 64 o più recente).",
                    L"WebCam and TV Smart Vision", MB_OK | MB_ICONERROR);
        return 2;
    }
#endif
    std::wstring selfTestDir;
    bool selfTestDevice = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        if (lstrcmpiW(argv[i], L"--self-test") == 0 && i + 1 < argc) selfTestDir = argv[++i];
        else if (lstrcmpiW(argv[i], L"--self-test-device") == 0) selfTestDevice = true;
    }
    if (argv) LocalFree(argv);
    if (!selfTestDir.empty()) ensureDir(selfTestDir);

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 3;
    imagingInit();
    int rc = 4;
    if (App::get().create(inst, show, selfTestDir, selfTestDevice)) rc = App::get().run();
    imagingShutdown();
    CoUninitialize();
    return rc;
}
