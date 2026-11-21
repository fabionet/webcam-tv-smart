// Compatibilità con Windows XP a 32 bit.
// Le librerie di supporto di GCC 13 (gthr-win32, libstdc++) importano da kernel32.dll alcune funzioni che
// esistono solo da Windows Vista: GetThreadId e le variabili di condizione. Su XP basterebbe questo a
// impedire il caricamento dell'eseguibile. Qui definiamo noi i simboli d'importazione ("__imp__Nome@N"):
// il linker usa queste versioni, che chiamano la funzione vera quando esiste e altrimenti la emulano.
// Questo file va collegato come oggetto dell'eseguibile (non in una libreria statica).
#include <windows.h>

#if defined(__i386__)
extern "C" {

// Attenzione: niente variabili statiche locali con inizializzatore dinamico in questo file. Userebbero
// __cxa_guard_acquire, che passa proprio dalle primitive sostituite qui sotto (ricorsione infinita).
static FARPROC k32(FARPROC* cache, int* done, const char* name) {
    if (!*done) {
        HMODULE k = GetModuleHandleW(L"kernel32.dll");
        *cache = k ? GetProcAddress(k, name) : nullptr;
        *done = 1;                                   // una corsa tra thread qui è innocua: il risultato è identico
    }
    return *cache;
}
#define SV_REAL(type, name) static FARPROC cache_; static int done_; type real = (type)(void*)k32(&cache_, &done_, name)

// ------------------------------------------------------------------ GetThreadId
typedef LONG(WINAPI* NtQueryInformationThread_t)(HANDLE, int, PVOID, ULONG, PULONG);

static DWORD WINAPI svGetThreadId(HANDLE thread) {
    typedef DWORD(WINAPI* Fn)(HANDLE);
    SV_REAL(Fn, "GetThreadId");
    if (real) return real(thread);
    struct { LONG exitStatus; PVOID teb; struct { HANDLE process; HANDLE thread; } client; ULONG_PTR affinity; LONG priority; LONG basePriority; } tbi;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    NtQueryInformationThread_t query = ntdll ? (NtQueryInformationThread_t)(void*)GetProcAddress(ntdll, "NtQueryInformationThread") : nullptr;
    if (query && query(thread, 0 /*ThreadBasicInformation*/, &tbi, sizeof tbi, nullptr) >= 0)
        return (DWORD)(ULONG_PTR)tbi.client.thread;
    return 0;
}

// ------------------------------------------------------- variabili di condizione
// Emulazione per XP: l'attesa rilascia la sezione critica, cede il processore e la riprende. I risvegli
// "spuri" sono ammessi dalla semantica delle variabili di condizione (chi attende ricontrolla il predicato).
static VOID WINAPI svInitializeConditionVariable(PVOID cv) {
    typedef VOID(WINAPI* Fn)(PVOID);
    SV_REAL(Fn, "InitializeConditionVariable");
    if (real) real(cv);
    else *(PVOID*)cv = nullptr;
}

static BOOL WINAPI svSleepConditionVariableCS(PVOID cv, PCRITICAL_SECTION cs, DWORD ms) {
    typedef BOOL(WINAPI* Fn)(PVOID, PCRITICAL_SECTION, DWORD);
    SV_REAL(Fn, "SleepConditionVariableCS");
    if (real) return real(cv, cs, ms);
    LeaveCriticalSection(cs);
    Sleep(ms < 1 ? 0 : 1);
    EnterCriticalSection(cs);
    return TRUE;
}

static VOID WINAPI svWakeConditionVariable(PVOID cv) {
    typedef VOID(WINAPI* Fn)(PVOID);
    SV_REAL(Fn, "WakeConditionVariable");
    if (real) real(cv);
}

static VOID WINAPI svWakeAllConditionVariable(PVOID cv) {
    typedef VOID(WINAPI* Fn)(PVOID);
    SV_REAL(Fn, "WakeAllConditionVariable");
    if (real) real(cv);
}

DWORD(WINAPI* svImpGetThreadId)(HANDLE) __asm__("__imp__GetThreadId@4") = svGetThreadId;
VOID(WINAPI* svImpInitCV)(PVOID) __asm__("__imp__InitializeConditionVariable@4") = svInitializeConditionVariable;
BOOL(WINAPI* svImpSleepCV)(PVOID, PCRITICAL_SECTION, DWORD) __asm__("__imp__SleepConditionVariableCS@12") = svSleepConditionVariableCS;
VOID(WINAPI* svImpWakeCV)(PVOID) __asm__("__imp__WakeConditionVariable@4") = svWakeConditionVariable;
VOID(WINAPI* svImpWakeAllCV)(PVOID) __asm__("__imp__WakeAllConditionVariable@4") = svWakeAllConditionVariable;

}
#endif
