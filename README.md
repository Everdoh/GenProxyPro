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

📊 Usage Examples

| Command                                                     | Description                                                          |
| ----------------------------------------------------------- | -------------------------------------------------------------------- |
| `GenProxyPro.exe C:\Sys\foo.dll`                            | Generates a proxy for `foo.dll`, real DLL is renamed `foo_orig.dll`. |
| `GenProxyPro.exe C:\Sys\bar.dll --out C:\Projects\ProxyBar` | Saves generated files to `C:\Projects\ProxyBar`.                     |
| `GenProxyPro.exe test.dll --orig-suffix .real`              | Uses `.real` suffix → real file is `test.dll.real`.                  |
| `GenProxyPro.exe my.dll --emit-def`                         | Generates `my.def` file along with `dllmain.cpp`.                    |
| `GenProxyPro.exe api.dll --emit-json-report`                | Outputs `exports_api.json` with full export metadata.                |
| `GenProxyPro.exe net.dll --emit-host`                       | Creates `Host_net.cpp` for testing proxy loading.                    |
| `GenProxyPro.exe gui.dll --include Init.*`                  | Forwards only functions matching `Init.*`.                           |
| `GenProxyPro.exe gui.dll --exclude Debug.*`                 | Excludes exports matching `Debug.*`.                                 |
| `GenProxyPro.exe core.dll --keep-ordinals`                  | Preserves ordinal positions exactly as in original DLL.              |
| `GenProxyPro.exe engine.dll --verbose`                      | Runs with verbose logs for debugging.                                |


🔮 Future Ideas

Automatic detection and handling of complex forwarders

Better template customization (hooks, inline stubs)

Integration with shellcode embedding as an optional advanced flag
