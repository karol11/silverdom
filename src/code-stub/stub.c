#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

__declspec(dllimport) void log_fn(const char* text);

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        log_fn("Code loaded");
        break;
    }
    return TRUE;
}
