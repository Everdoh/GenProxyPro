// DllProxy.cpp — C++ com exports estáveis (extern "C"), Unicode e WinAPI
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include "pch.h"
#include <windows.h>
#include <shellapi.h>

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}

// ===== EXPORTS =====
// Use extern "C" nas DEFINIÇÕES para travar os nomes dos símbolos:

extern "C" __declspec(dllexport) BOOL WINAPI Plugin_ShowMessageBox(LPCWSTR title, LPCWSTR text) {
    return MessageBoxW(nullptr, text, title, MB_OK | MB_ICONINFORMATION) != 0;
}

extern "C" __declspec(dllexport) BOOL WINAPI Plugin_OpenGoogle() {
    HINSTANCE h = ShellExecuteW(nullptr, L"open", L"https://www.google.com", nullptr, nullptr, SW_SHOWNORMAL);
    return (reinterpret_cast<UINT_PTR>(h) > 32);
}

extern "C" __declspec(dllexport) int  WINAPI Plugin_Add(int a, int b) {
    return a + b;
}

extern "C" __declspec(dllexport) DWORD WINAPI Plugin_GetVersion() {
    // 1.0 → 0x00010000 (HIWORD.MAJOR | LOWORD.MINOR)
    return (1u << 16) | 0u;
}
