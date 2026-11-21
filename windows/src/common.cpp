#include "common.h"

#include <cstdarg>
#include <cstdio>
#include <malloc.h>

namespace sv {

double nowSeconds() {
    static LARGE_INTEGER freq = {};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) / double(freq.QuadPart);
}

std::wstring format(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnwprintf(buf, 2047, fmt, ap);
    va_end(ap);
    if (n < 0) n = 2047;
    buf[n] = 0;
    return buf;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring timestampName() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    return format(L"%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

bool fileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES;
}

int64_t fileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return -1;
    return (int64_t(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
}

void ensureDir(const std::wstring& path) {
    // Crea anche le cartelle intermedie.
    for (size_t i = 3; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == L'\\' || path[i] == L'/') {
            std::wstring sub = path.substr(0, i);
            if (!sub.empty()) CreateDirectoryW(sub.c_str(), nullptr);
        }
    }
}

static std::wstring g_logPath;
static Lock g_logLock;

void setLogFile(const std::wstring& path) { g_logPath = path; }

void log(const std::wstring& line) {
    OutputDebugStringW((line + L"\n").c_str());
    if (g_logPath.empty()) return;
    Guard g(g_logLock);
    HANDLE h = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, nullptr, FILE_END);
    std::string s = narrow(line) + "\r\n";
    DWORD w;
    WriteFile(h, s.data(), (DWORD)s.size(), &w, nullptr);
    CloseHandle(h);
}

void* alignedAlloc(size_t bytes, size_t align) { return __mingw_aligned_malloc(bytes, align); }
void alignedFree(void* p) { if (p) __mingw_aligned_free(p); }

bool OutFile::open(const std::wstring& path) {
    close();
    failed_ = false;
    pos_ = 0;
    h_ = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return h_ != INVALID_HANDLE_VALUE;
}

bool OutFile::write(const void* data, size_t bytes) {
    if (h_ == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    if (!WriteFile(h_, data, (DWORD)bytes, &w, nullptr) || w != bytes) { failed_ = true; return false; }
    pos_ += bytes;
    return true;
}

bool OutFile::writeAt(int64_t offset, const void* data, size_t bytes) {
    if (h_ == INVALID_HANDLE_VALUE) return false;
    if (offset != pos_) {
        LARGE_INTEGER li;
        li.QuadPart = offset;
        if (!SetFilePointerEx(h_, li, nullptr, FILE_BEGIN)) { failed_ = true; return false; }
        pos_ = offset;
    }
    return write(data, bytes);
}

void OutFile::close() {
    if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
    h_ = INVALID_HANDLE_VALUE;
}

} // namespace sv
