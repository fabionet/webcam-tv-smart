#!/bin/sh
# Verifica che l'eseguibile a 32 bit importi solo funzioni e DLL presenti in Windows XP SP3.
# Uso: ./check-xp-imports.sh build-xp/WebCamTVSmartVision.exe
set -e
EXE="$1"
OBJDUMP=i686-w64-mingw32-objdump
LIST=$($OBJDUMP -p "$EXE" | awk '/DLL Name:/{d=tolower($3)} /^\t[0-9a-f]+\t +[0-9]+  /{print d" "$3}')
DLLS=$(echo "$LIST" | awk '{print $1}' | sort -u | tr '\n' ' ')
echo "DLL importate: $DLLS"
rc=0
for d in $DLLS; do
  case "$d" in
    kernel32.dll|user32.dll|gdi32.dll|msvcrt.dll|ole32.dll|oleaut32.dll|shell32.dll|comctl32.dll|winmm.dll|advapi32.dll) ;;
    *) echo "ERRORE: DLL non prevista per Windows XP: $d"; rc=1 ;;
  esac
done
# Funzioni introdotte dopo XP (Server 2003/Vista e successivi) che le librerie di GCC o il codice potrebbero tirare dentro.
BAD='GetThreadId|ConditionVariable|SRWLock|InitOnce|GetTickCount64|FlsAlloc|FlsFree|FlsGetValue|FlsSetValue|GetLocaleInfoEx|LCMapStringEx|CompareStringEx|GetSystemTimePreciseAsFileTime|InitializeCriticalSectionEx|CreateEventEx|CreateSemaphoreEx|CreateMutexEx|WaitForSingleObjectEx2|SetThreadStackGuarantee|GetFinalPathNameByHandle|GetFileInformationByHandleEx|CancelIoEx|QueryFullProcessImageName|GetCurrentProcessorNumber|CreateSymbolicLink|TryAcquire|_s$|_fseeki64|_ftelli64|strnlen|wcsnlen|_except_handler4|__acrt|_ftol2|SHGetKnownFolder|TaskDialog|DwmGet|SetProcessDPIAware|GetDpiFor'
FOUND=$(echo "$LIST" | awk '{print $2}' | grep -E "$BAD" || true)
if [ -n "$FOUND" ]; then echo "ERRORE: funzioni non presenti in Windows XP:"; echo "$FOUND"; rc=1; fi
SUB=$($OBJDUMP -p "$EXE" | awk '/MajorSubsystemVersion/{a=$2} /MinorSubsystemVersion/{b=$2} END{print a"."b}')
echo "Versione di sottosistema: $SUB (XP = 5.1)"
[ "$SUB" = "5.1" ] || { echo "ERRORE: versione di sottosistema diversa da 5.1"; rc=1; }
MACH=$($OBJDUMP -f "$EXE" | grep -c "pei-i386" || true)
[ "$MACH" = "1" ] || { echo "ERRORE: l'eseguibile non è a 32 bit"; rc=1; }
[ $rc = 0 ] && echo "OK: $(echo "$LIST" | wc -l) funzioni importate, tutte disponibili in Windows XP."
exit $rc
