#include "pch.h"
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <strsafe.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;
static INIT_ONCE gOnce = INIT_ONCE_STATIC_INIT;
static HMODULE gReal = nullptr;
static const wchar_t* kRealBase = L"DllProxy_orig.dll";

// Alguns Windows antigos podem não ter SetDefaultDllDirectories
static void SafeSetDefaultDllDirectories() {
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return;
    typedef BOOL (WINAPI *Fn)(DWORD);
    Fn p = (Fn)GetProcAddress(k32, "SetDefaultDllDirectories");
    if (p) p(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
}

static BOOL CALLBACK InitReal(PINIT_ONCE, PVOID, PVOID*) {
    // Pega o diretório da proxy usando __ImageBase
    wchar_t modPath[MAX_PATH];
    DWORD n = GetModuleFileNameW((HMODULE)&__ImageBase, modPath, MAX_PATH);
    if (!n) return TRUE;

    // recorta para a pasta
    for (int i = (int)n - 1; i >= 0; --i) {
        if (modPath[i] == L'\\' || modPath[i] == L'/') { modPath[i] = 0; break; }
    }

    SafeSetDefaultDllDirectories();

    // 1) tenta carregar apenas pelo nome (com LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR)
    HMODULE real = LoadLibraryExW(kRealBase, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);

    // 2) fallback: caminho absoluto "<dir>\kRealBase"
    if (!real) {
        wchar_t buf[MAX_PATH];
        StringCchCopyW(buf, MAX_PATH, modPath);
        StringCchCatW(buf, MAX_PATH, L"\\");
        StringCchCatW(buf, MAX_PATH, kRealBase);
        real = LoadLibraryExW(buf, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    }

    gReal = real;
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        InitOnceExecuteOnce(&gOnce, InitReal, NULL, NULL);
    }
    return TRUE;
}

// ---- Forwarders gerados automaticamente ----
#pragma comment(linker, "/export:Plugin_Add=DllProxy_orig.Plugin_Add")
#pragma comment(linker, "/export:Plugin_GetVersion=DllProxy_orig.Plugin_GetVersion")
#pragma comment(linker, "/export:Plugin_OpenGoogle=DllProxy_orig.Plugin_OpenGoogle")
#pragma comment(linker, "/export:Plugin_ShowMessageBox=DllProxy_orig.Plugin_ShowMessageBox")

// stats: byName=4 byOrdinal=0 keptForwarders=0 gaps(RVA=0)=0 probableData=0
