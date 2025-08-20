# 🛠️ GenProxyPro

**GenProxyPro** is a C++ tool that automatically generates a **Proxy DLL project** to forward exports to the original DLL.

---

## ⚙️ How It Works
1. Run `GenProxyPro`, passing the original DLL as argument.  
2. The tool parses the exports from that DLL.  
3. It generates a new `dllmain.cpp` that loads the real DLL (renamed with a custom suffix, default `_orig`) and forwards exports.  
4. Optionally, it can emit reports or extra helper files.

---

## 🚀 Usage

### Basic
```bash
GenProxyPro.exe <path_to_original_dll> <dllproxy_name>

GenProxyPro.exe C:\Windows\System32\version.dll

This generates a dllmain.cpp proxy file.
Rename the original version.dll to version_orig.dll, place your proxy DLL with the original name (version.dll) in the same directory, and the host program will load your proxy while real calls are forwarded.

📌 Options

--out <dir>                     : output directory (default: same dir as DLL)
--orig-suffix <suf>             : suffix to rename the real DLL (default: _orig)
--emit-def                      : generate .def file (in addition to pragmas in dllmain.cpp)
--emit-json-report              : generate exports_<base>.json with export metadata
--emit-host                     : generate Host_<base>.cpp (test loader program)
--include <regex>               : include only exports matching regex (by name)
--exclude <regex>               : exclude exports matching regex (by name)
--keep-ordinals                 : preserve ordinal layout; reports gaps (RVA=0)
--respect-existing-forwarders   : keep native forwarders (DLL.Func) instead of redirecting to *_orig
--verbose                       : verbose logging
