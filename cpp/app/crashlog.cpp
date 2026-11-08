#include "crashlog.h"
#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>

static LONG WINAPI onCrash(EXCEPTION_POINTERS* ep) {
  static volatile LONG entered = 0;
  if (InterlockedExchange(&entered, 1)) return EXCEPTION_CONTINUE_SEARCH;   // a crash inside the handler: give up quietly
  std::string out;
  char line[256];
  const HMODULE self = GetModuleHandleW(nullptr);
  std::snprintf(line, sizeof line, "FATAL exception 0x%08lX at %p (module base %p)\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress, static_cast<void*>(self));
  out += line;
  if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
    std::snprintf(line, sizeof line, "  access violation: %s address %p\n", ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading", reinterpret_cast<void*>(ep->ExceptionRecord->ExceptionInformation[1]));
    out += line;
  }
  void* frames[48];
  const USHORT n = CaptureStackBackTrace(0, 48, frames, nullptr);
  for (USHORT i = 0; i < n; i++) {
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(frames[i]), &m);
    wchar_t name[MAX_PATH] = L"?";
    if (m) GetModuleFileNameW(m, name, MAX_PATH);
    const wchar_t* base = wcsrchr(name, L'\\');
    std::snprintf(line, sizeof line, "  #%u %p  %ls+0x%llx\n", unsigned(i), frames[i], base ? base + 1 : name, static_cast<unsigned long long>(reinterpret_cast<char*>(frames[i]) - reinterpret_cast<char*>(m)));
    out += line;
  }
  std::fputs(out.c_str(), stderr);
  wchar_t tmp[MAX_PATH];
  if (GetTempPathW(MAX_PATH, tmp)) {
    const std::wstring path = std::wstring(tmp) + L"xournal-lab-crash.log";
    if (FILE* f = _wfopen(path.c_str(), L"ab")) { std::fputs(out.c_str(), f); std::fclose(f); }
  }
  return EXCEPTION_CONTINUE_SEARCH;   // the system still ends the process (and Windows Error Reporting still sees it)
}

void installCrashLog() { SetUnhandledExceptionFilter(onCrash); }
#else
void installCrashLog() {}
#endif
