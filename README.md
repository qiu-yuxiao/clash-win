# ⚡ Clash Win

**Pure Windows Native Visual C++ Clash Client**  
*Built with Modern C++20, Win32 API, and Hardware-Accelerated Direct2D.*

---

## 🚀 Overview

**Clash Win** is a dedicated, ultra-lightweight, 100% native Windows client for the Clash/Mihomo network core.  
It replaces multi-layer Web frameworks (Chromium/WebView2) with pure native Windows technologies, delivering:

- **Ultra-Low Memory Footprint**: ~15MB – 25MB runtime memory (vs ~80MB+ in WebView2).
- **Sub-150ms Cold Startup**: Native executable with zero JavaScript/JIT warm-up time.
- **Hardware-Accelerated Skeuomorphic UI**: Hand-crafted 3D bevels, metallic textures, and fluid 120fps Direct2D / DirectWrite graphics.
- **Flawless Win32 Native Integration**: Native subclassed `WM_SIZING` hooks, anti-downward-drift geometry clamping, Per-Monitor v2 DPI awareness, and direct SCM privileged service management.

---

## 🛠️ Tech Stack

| Layer | Technology | Description |
| :--- | :--- | :--- |
| **Language** | **Modern C++20** | Strict ISO C++20 standard, RAII, coroutines |
| **Graphics** | **Direct2D & DirectWrite** | Pure native GPU vector rendering, zero web engine |
| **Window Host** | **Pure Win32 API** | Custom frameless window, non-client hit testing, subclassing |
| **Networking** | **WinHTTP (Win32 Native)** | Direct REST API (9098) & WebSocket communication with Mihomo core |
| **Core Service** | **Win32 SCM API** | Direct Service Control Manager integration (`clash-verge-service`) |

---

## 🏗️ Building from Source

### Prerequisites
- Windows 10/11 x64
- Visual Studio 2022 (or VS Build Tools) with C++ Desktop Development workload
- CMake 3.20+

### Command Line Build
```powershell
# 1. Configure CMake with Visual Studio 2022 generator
cmake -B build -G "Visual Studio 17 2022" -A x64

# 2. Build Release target
cmake --build build --config Release

# 3. Run executable
.\build\bin\Release\ClashWin.exe
```

---

## 📜 Design Contract & Agreements

See [clash_win_agreements.md](clash_win_agreements.md) for detailed architecture specifications, window geometry constants, and network engine protocols.

---

## 📄 License

GPL-3.0-only
