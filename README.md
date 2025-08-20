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
