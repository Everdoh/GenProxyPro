// Host.cpp — C++, chama as exports da DLL via LoadLibrary/GetProcAddress

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <windows.h>
#include <cstdio>

using PFN_ShowMessageBox = BOOL(WINAPI*)(LPCWSTR, LPCWSTR);
using PFN_OpenGoogle = BOOL(WINAPI*)();
using PFN_Add = int  (WINAPI*)(int, int);
using PFN_GetVersion = DWORD(WINAPI*)();

int wmain() {
    HMODULE mod = LoadLibraryW(L"DllProxy.dll");
    if (!mod) {
        std::wprintf(L"[!] LoadLibrary falhou: %lu\n", GetLastError());
        return 1;
    }

    auto pShow = reinterpret_cast<PFN_ShowMessageBox>(GetProcAddress(mod, "Plugin_ShowMessageBox"));
    auto pOpen = reinterpret_cast<PFN_OpenGoogle>(GetProcAddress(mod, "Plugin_OpenGoogle"));
    auto pAdd = reinterpret_cast<PFN_Add>(GetProcAddress(mod, "Plugin_Add"));
    auto pVer = reinterpret_cast<PFN_GetVersion>(GetProcAddress(mod, "Plugin_GetVersion"));

    if (!pShow || !pOpen) {
        std::wprintf(L"[!] GetProcAddress falhou para exports essenciais.\n");
        FreeLibrary(mod);
        return 1;
    }

    if (pVer) {
        DWORD v = pVer();
        std::wprintf(L"[i] Versão do plugin: %u.%u\n", HIWORD(v), LOWORD(v));
    }

    pShow(L"Hello from DllProxy (C++)", L"Chamada ao MessageBoxW feita via DLL.");

    if (pOpen()) std::wprintf(L"[+] Abriu https://www.google.com\n");
    else         std::wprintf(L"[!] Falha ao abrir navegador\n");

    if (pAdd) {
        int r = pAdd(10, 32);
        std::wprintf(L"[i] Plugin_Add(10,32) = %d\n", r);
    }

    FreeLibrary(mod);
    return 0;
}
