// Utilità comuni: lock, tempo, stringhe, log. Solo API presenti da Windows XP.
#pragma once
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sv {

class Lock {
public:
    Lock() { InitializeCriticalSection(&cs_); }
    ~Lock() { DeleteCriticalSection(&cs_); }
    void enter() { EnterCriticalSection(&cs_); }
    void leave() { LeaveCriticalSection(&cs_); }
private:
    CRITICAL_SECTION cs_;
    Lock(const Lock&);
    Lock& operator=(const Lock&);
};

struct Guard {
    explicit Guard(Lock& l) : l_(l) { l_.enter(); }
    ~Guard() { l_.leave(); }
    Lock& l_;
};

double nowSeconds();                               // orologio monotòno (QueryPerformanceCounter)
std::wstring format(const wchar_t* fmt, ...);      // usare %ls per le stringhe wide
std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& w);
std::wstring timestampName();                      // "20261121-153045"
bool fileExists(const std::wstring& path);
int64_t fileSize(const std::wstring& path);
void ensureDir(const std::wstring& path);

void setLogFile(const std::wstring& path);         // attivo solo in auto-test / diagnostica
void log(const std::wstring& line);

void* alignedAlloc(size_t bytes, size_t align = 64);
void alignedFree(void* p);

// File di uscita su handle Win32 (percorsi Unicode, offset a 64 bit anche su XP).
class OutFile {
public:
    ~OutFile() { close(); }
    bool open(const std::wstring& path);
    bool write(const void* data, size_t bytes);
    bool writeAt(int64_t offset, const void* data, size_t bytes);
    void close();
    bool ok() const { return h_ != INVALID_HANDLE_VALUE && !failed_; }
private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
    int64_t pos_ = -1;
    bool failed_ = false;
};

} // namespace sv
