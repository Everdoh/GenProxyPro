// GenProxyPro.cpp — Proxy DLL generator
// Build (Developer Command Prompt):
//   cl /EHsc /O2 GenProxyPro.cpp /Fe:GenProxyPro.exe
//
// Uso (duas formas):
//   GenProxyPro.exe "C:\pasta" Foo.dll [opções]
//   GenProxyPro.exe "C:\pasta\Foo.dll"  [opções]    // 2º arg ignorado se 1º já for caminho .dll
//
// Opções:
//   --out <dir>                     : diretório de saída (default: <dir/da DLL>)
//   --orig-suffix <suf>             : sufixo para renomear a DLL real (default: _orig)
//   --emit-def                      : gerar arquivo .def (além dos pragmas no dllmain.cpp)
//   --emit-json-report              : gerar exports_<base>.json com relatório
//   --emit-host                     : gerar Host_<base>.cpp (loader de teste)
//   --include <regex>               : incluir apenas exports que casem com regex (nome)
//   --exclude <regex>               : excluir exports que casem com regex (nome)
//   --keep-ordinals                 : preservar layout de ordinais; relata lacunas (RVA=0)
//   --respect-existing-forwarders   : manter forwarders nativos (DLL.Func) em vez de apontar para *_orig
//   --verbose                       : logs verbosos



#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <strsafe.h>
#include <cwctype>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <regex>
#include <fstream>
#include <iostream>

// -------------------- Utilidades básicas --------------------

struct PEView {
    BYTE* base{}; size_t size{};
    IMAGE_DOS_HEADER* dos{};
    IMAGE_NT_HEADERS* nt{};
    IMAGE_FILE_HEADER* fh{};
    IMAGE_OPTIONAL_HEADER* oh{};
    IMAGE_SECTION_HEADER* firstSec{};
};

static std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    wchar_t sep = L'\\';
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + sep + b;
}

static std::wstring Dirname(const std::wstring& s) {
    size_t sl = s.find_last_of(L"\\/");
    return (sl == std::wstring::npos) ? L"." : s.substr(0, sl);
}

static std::wstring BasenameNoExt(const std::wstring& s) {
    size_t sl = s.find_last_of(L"\\/");
    size_t dot = s.find_last_of(L'.');
    size_t st = (sl == std::wstring::npos ? 0 : sl + 1);
    if (dot == std::wstring::npos || dot < st) dot = s.size();
    return s.substr(st, dot - st);
}

static bool IsDllPath(const std::wstring& s) {
    size_t dot = s.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = s.substr(dot);
    for (auto& ch : ext) ch = (wchar_t)towlower(ch);
    return ext == L".dll";
}

static bool MapWholeFile(const std::wstring& path, PEView& out) {
    HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(hf, &sz) || sz.QuadPart <= 0) { CloseHandle(hf); return false; }
    HANDLE hm = CreateFileMappingW(hf, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hm) { CloseHandle(hf); return false; }
    BYTE* pv = (BYTE*)MapViewOfFile(hm, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hm); CloseHandle(hf);
    if (!pv) return false;
    out.base = pv; out.size = (size_t)sz.QuadPart;
    out.dos = (IMAGE_DOS_HEADER*)pv; if (out.dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    out.nt = (IMAGE_NT_HEADERS*)(pv + out.dos->e_lfanew); if (out.nt->Signature != IMAGE_NT_SIGNATURE) return false;
    out.fh = &out.nt->FileHeader; out.oh = &out.nt->OptionalHeader; out.firstSec = IMAGE_FIRST_SECTION(out.nt);
    return true;
}

static IMAGE_SECTION_HEADER* FindSectionForRva(PEView& pe, DWORD rva) {
    auto sec = pe.firstSec;
    for (WORD i = 0; i < pe.fh->NumberOfSections; i++, sec++) {
        DWORD va = sec->VirtualAddress, vsz = sec->Misc.VirtualSize;
        if (rva >= va && rva < va + vsz) return sec;
    }
    return nullptr;
}

static BYTE* RvaToPtr(PEView& pe, DWORD rva) {
    auto sec = FindSectionForRva(pe, rva);
    if (!sec) return nullptr;
    DWORD off = sec->PointerToRawData + (rva - sec->VirtualAddress);
    if (off >= pe.size) return nullptr;
    return pe.base + off;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n); // &w[0] = buffer MUTÁVEL (ok em C++11+)
    return w;
}
static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// -------------------- Extração de Exports --------------------

struct ExportItem {
    std::string name;      // vazio => ordinal-only
    DWORD ordinal{};       // ordinalBase + index
    DWORD rva{};           // 0 => slot vazio
    bool isForwardString{};
    bool probableData{};
    std::string forwardTarget; // "DLL.Func" se forward nativo
};

static bool ExtractExports(PEView& pe, std::vector<ExportItem>& out, DWORD& ordinalBase) {
    auto& dd = pe.oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dd.VirtualAddress || !dd.Size) return false;
    auto exp = (IMAGE_EXPORT_DIRECTORY*)RvaToPtr(pe, dd.VirtualAddress);
    if (!exp) return false;

    ordinalBase = exp->Base;
    auto addrFuncs = (DWORD*)RvaToPtr(pe, exp->AddressOfFunctions);
    auto addrNames = (DWORD*)RvaToPtr(pe, exp->AddressOfNames);
    auto addrOrds = (WORD*)RvaToPtr(pe, exp->AddressOfNameOrdinals);
    if (!addrFuncs || (exp->NumberOfNames && (!addrNames || !addrOrds))) return false;

    // map idx->name em 1 passada
    std::vector<const char*> idx2name(exp->NumberOfFunctions, nullptr);
    for (DWORD n = 0; n < exp->NumberOfNames; n++) {
        WORD idx = addrOrds[n];
        const char* s = (const char*)RvaToPtr(pe, addrNames[n]);
        if (idx < idx2name.size()) idx2name[idx] = s;
    }

    out.clear(); out.reserve(exp->NumberOfFunctions);
    for (DWORD i = 0; i < exp->NumberOfFunctions; i++) {
        ExportItem e{};
        e.ordinal = ordinalBase + i;
        e.rva = addrFuncs[i];
        if (idx2name[i]) e.name = idx2name[i];

        // forward-string detection (RVA aponta p/ string dentro do export dir)
        if (e.rva >= dd.VirtualAddress && e.rva < dd.VirtualAddress + dd.Size) {
            e.isForwardString = true;
            const char* fs = (const char*)RvaToPtr(pe, e.rva);
            if (fs) e.forwardTarget = fs; // "DLL.Func"
        }

        // heurística de export de dados (seção não-executável)
        if (e.rva && !e.isForwardString) {
            auto sec = FindSectionForRva(pe, e.rva);
            if (sec && !(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) e.probableData = true;
        }
        out.push_back(std::move(e));
    }
    return true;
}

// -------------------- Opções de CLI --------------------

struct Options {
    std::wstring inDir, inDllName, inFullPath; bool useFullPath{};
    std::wstring outDir;
    std::wstring origSuffix = L"_orig";
    bool emitDef{}, emitJson{}, emitHost{}, keepOrdinals{}, respectFwd{}, verbose{ true };
    bool hasInclude{}, hasExclude{}; std::wregex reInclude, reExclude;
};

static void ParseArgs(int argc, wchar_t** argv, Options& o) {
    if (argc < 2) {
        fwprintf(stderr, L"Uso:\n  %ls <dir> <dll> [opções]\n  %ls <caminho\\para\\dll.dll> [opções]\n", argv[0], argv[0]);
        ExitProcess(1);
    }
    if (argc >= 3 && IsDllPath(argv[1])) {
        // forma: fullpath .dll
        o.useFullPath = true;
        o.inFullPath = argv[1];
        o.inDir = Dirname(o.inFullPath);
        o.inDllName = BasenameNoExt(o.inFullPath) + L".dll";
    }
    else if (argc >= 3) {
        o.useFullPath = false;
        o.inDir = argv[1];
        o.inDllName = argv[2];
    }
    else {
        fwprintf(stderr, L"[!] Parâmetros insuficientes.\n"); ExitProcess(1);
    }

    o.outDir = o.inDir;

    for (int i = (o.useFullPath ? 2 : 3); i < argc; i++) {
        std::wstring k = argv[i];
        if (k == L"--out" && i + 1 < argc) o.outDir = argv[++i];
        else if (k == L"--orig-suffix" && i + 1 < argc) o.origSuffix = argv[++i];
        else if (k == L"--emit-def") o.emitDef = true;
        else if (k == L"--emit-json-report") o.emitJson = true;
        else if (k == L"--emit-host") o.emitHost = true;
        else if (k == L"--keep-ordinals") o.keepOrdinals = true;
        else if (k == L"--respect-existing-forwarders") o.respectFwd = true;
        else if (k == L"--include" && i + 1 < argc) { o.hasInclude = true; o.reInclude = std::wregex(argv[++i], std::regex::icase); }
        else if (k == L"--exclude" && i + 1 < argc) { o.hasExclude = true; o.reExclude = std::wregex(argv[++i], std::regex::icase); }
        else if (k == L"--verbose") o.verbose = true;
        else { fwprintf(stderr, L"[!] Opção desconhecida: %ls\n", k.c_str()); ExitProcess(1); }
    }
}

static bool NamePassesFilters(const Options& o, const std::string& name) {
    if (!o.hasInclude && !o.hasExclude) return true;
    std::wstring w = Utf8ToWide(name);
    if (o.hasInclude && !std::regex_search(w, o.reInclude)) return false;
    if (o.hasExclude && std::regex_search(w, o.reExclude)) return false;
    return true;
}

// -------------------- Emissão de artefatos --------------------

static void WriteJsonReport(const std::wstring& path, const std::vector<ExportItem>& exps) {
    std::ofstream js(WideToUtf8(path), std::ios::binary);
    if (!js) return;
    js << "{\n  \"exports\": [\n";
    for (size_t i = 0; i < exps.size(); ++i) {
        const auto& e = exps[i];
        js << "    { \"ordinal\": " << e.ordinal
            << ", \"name\": \"" << e.name << "\""
            << ", \"rva\": " << e.rva
            << ", \"is_forward\": " << (e.isForwardString ? 1 : 0)
            << ", \"probable_data\": " << (e.probableData ? 1 : 0)
            << ", \"forward_target\": \"" << e.forwardTarget << "\" }";
        js << (i + 1 < exps.size() ? ",\n" : "\n");
    }
    js << "  ]\n}\n";
}

static void EmitHost(const std::wstring& pathCpp, const std::wstring& proxyBase) {
    std::ofstream f(WideToUtf8(pathCpp), std::ios::binary);
    if (!f) return;
    f <<
        R"(#include "pch.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

int wmain(){
    HMODULE m = LoadLibraryW(L")" << WideToUtf8(proxyBase) << R"(.dll");
    if(!m){
        wprintf(L"LoadLibrary failed: %lu\n", GetLastError());
        return 1;
    }
    wprintf(L"Loaded )" << WideToUtf8(proxyBase) << R"(.dll\n");
    FreeLibrary(m);
    return 0;
}
)";
}

static void EmitDef(const std::wstring& pathDef,
    const std::wstring& inDllName,
    const std::wstring& origSuffix,
    bool respectFwd,
    const Options& opt,
    const std::vector<ExportItem>& exps,
    bool keepOrdinals)
{
    std::ofstream d(WideToUtf8(pathDef), std::ios::binary);
    if (!d) { fwprintf(stderr, L"[!] Não foi possível criar: %ls\n", pathDef.c_str()); return; }
    auto base = BasenameNoExt(inDllName);
    auto renamed = base + origSuffix;

    d << "LIBRARY " << WideToUtf8(base) << "\nEXPORTS\n";
    for (const auto& e : exps) {
        if (e.rva == 0) { if (keepOrdinals) {/* lacuna mantida implicitamente */ } continue; }
        if (!e.name.empty() && !NamePassesFilters(opt, e.name)) continue;

        if (!e.name.empty()) {
            if (respectFwd && e.isForwardString && !e.forwardTarget.empty())
                d << e.name << "=" << e.forwardTarget << "\n";
            else
                d << e.name << "=" << WideToUtf8(renamed) << "." << e.name << "\n";
        }
        else {
            d << "@" << e.ordinal << "=" << WideToUtf8(renamed) << ".@" << e.ordinal << " NONAME\n";
        }
    }
}

static void EmitDllMainCpp(const std::wstring& outPath,
    const std::wstring& inDllName,
    const std::wstring& origSuffix,
    const Options& opt,
    const std::vector<ExportItem>& exps)
{
    std::ofstream f(WideToUtf8(outPath), std::ios::binary);
    if (!f) { fwprintf(stderr, L"[!] Não foi possível criar: %ls\n", outPath.c_str()); ExitProcess(5); }

    auto base = BasenameNoExt(inDllName);
    auto renamed = base + origSuffix;

    // Cabeçalho + includes (PCH primeiro!)
    f <<
        R"(#include "pch.h"
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
static const wchar_t* kRealBase = L")" << WideToUtf8(renamed) << R"(.dll";

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
)";

    size_t byName = 0, byOrd = 0, dataCnt = 0, fwdCnt = 0, keptCnt = 0, gaps = 0;

    for (const auto& e : exps) {
        if (e.rva == 0) { if (opt.keepOrdinals) gaps++; continue; }
        if (e.isForwardString) fwdCnt++;
        if (e.probableData)    dataCnt++;

        if (!e.name.empty() && !NamePassesFilters(opt, e.name)) continue;

        if (!e.name.empty()) {
            if (opt.respectFwd && e.isForwardString && !e.forwardTarget.empty()) {
                // mantém forwarder nativo exatamente como está
                f << "#pragma comment(linker, \"/export:" << e.name << "=" << e.forwardTarget << "\")\n";
                keptCnt++;
            }
            else {
                f << "#pragma comment(linker, \"/export:" << e.name << "="
                    << WideToUtf8(renamed) << "." << e.name << "\")\n";
                byName++;
            }
        }
        else {
            f << "#pragma comment(linker, \"/export:#" << e.ordinal << "="
                << WideToUtf8(renamed) << ".#" << e.ordinal << "\")\n";
            byOrd++;
        }
    }

    f << "\n// stats: byName=" << byName
        << " byOrdinal=" << byOrd
        << " keptForwarders=" << keptCnt
        << " gaps(RVA=0)=" << gaps
        << " probableData=" << dataCnt << "\n";
}

// -------------------- main --------------------

int wmain(int argc, wchar_t** argv) {
    Options opt;
    ParseArgs(argc, argv, opt);

    std::wstring inPath = opt.useFullPath ? opt.inFullPath : JoinPath(opt.inDir, opt.inDllName);
    DWORD attrs = GetFileAttributesW(inPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        fwprintf(stderr, L"[!] Arquivo não encontrado: %ls (err=%lu)\n", inPath.c_str(), GetLastError());
        return 2;
    }

    PEView pe{};
    if (!MapWholeFile(inPath, pe)) {
        fwprintf(stderr, L"[!] Falha ao abrir/parsear: %ls (err=%lu)\n", inPath.c_str(), GetLastError());
        return 3;
    }

    std::vector<ExportItem> exps; DWORD base = 0;
    if (!ExtractExports(pe, exps, base)) {
        fwprintf(stderr, L"[!] DLL sem export table válida: %ls\n", inPath.c_str());
        return 4;
    }

    // Saídas
    CreateDirectoryW(opt.outDir.c_str(), nullptr);
    std::wstring baseNoExt = BasenameNoExt(opt.inDllName);

    std::wstring dllmainPath = JoinPath(opt.outDir, L"dllmain.cpp");
    EmitDllMainCpp(dllmainPath, opt.inDllName, opt.origSuffix, opt, exps);

    if (opt.emitDef) {
        std::wstring defOut = JoinPath(opt.outDir, baseNoExt + L".def");
        EmitDef(defOut, opt.inDllName, opt.origSuffix, opt.respectFwd, opt, exps, opt.keepOrdinals);
    }
    if (opt.emitJson) {
        std::wstring jsonOut = JoinPath(opt.outDir, L"exports_" + baseNoExt + L".json");
        WriteJsonReport(jsonOut, exps);
    }
    if (opt.emitHost) {
        std::wstring hostOut = JoinPath(opt.outDir, L"Host_" + baseNoExt + L".cpp");
        EmitHost(hostOut, baseNoExt);
    }

    if (opt.verbose) {
        fwprintf(stdout, L"[+] Gerado: %ls\n", dllmainPath.c_str());
        if (opt.emitDef)  fwprintf(stdout, L"[+] .def: %ls\n", JoinPath(opt.outDir, baseNoExt + L".def").c_str());
        if (opt.emitJson) fwprintf(stdout, L"[+] json: %ls\n", JoinPath(opt.outDir, L"exports_" + baseNoExt + L".json").c_str());
        if (opt.emitHost) fwprintf(stdout, L"[+] host: %ls\n", JoinPath(opt.outDir, L"Host_" + baseNoExt + L".cpp").c_str());
        fwprintf(stdout, L"[i] Renomeie a DLL real para: %ls.dll\n", (baseNoExt + opt.origSuffix).c_str());
        fwprintf(stdout, L"[i] Compile a proxy como: %ls.dll\n", baseNoExt.c_str());
        fwprintf(stdout, L"    Ex.: cl /LD %ls /Fe:%ls\\%ls.dll\n", dllmainPath.c_str(), opt.outDir.c_str(), baseNoExt.c_str());
    }

    return 0;
}

