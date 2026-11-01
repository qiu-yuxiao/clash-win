# Clash Win

[![GitHub release (latest by date)](https://img.shields.io/github/v/release/qiu-yuxiao/clash-win?style=flat-square&label=Release&color=e8890c)](https://github.com/qiu-yuxiao/clash-win/releases/latest)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0)
[![Language: C++20](https://img.shields.io/badge/Language-C%2B%2B20-00599C?style=flat-square&logo=c%2B%2B)](https://isocpp.org)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows-0078D6?style=flat-square&logo=windows)](https://github.com/qiu-yuxiao/clash-win)

> **Clash Win v0.1.0** — the pure Windows native C++ rewrite of [Clash Mini](https://github.com/qiu-yuxiao/clash-mini).
> Zero web engine. Zero JavaScript runtime. Built with Win32 API, Direct2D/DirectWrite, and modern C++20.

Clash Win keeps everything you love about Clash Mini — the minimalist zero-config workflow, the 3D skeuomorphic visuals, the auto node switching and rule updates — but replaces the Tauri/WebView layer with hand-written native rendering. The result: **~15–25 MB runtime memory**, **sub-150 ms cold startup**, and a **120 fps Direct2D** interface, while remaining fully feature-aligned with Clash Mini.

---

## Quick Start Guide

> **Core Value**: Minimalist, zero-threshold, out-of-the-box — same philosophy as Clash Mini.

### Getting Started
Click the [Gear] button in the upper right corner of Clash Win, paste your subscription link or node configuration (supporting 19 protocols) into the "Subscription Link/Node Config" field, click [Import Node Info], and wait for the proxy node to be activated.

### Client Configuration
* **Browsers**: You can start browsing the web directly after importing the nodes (system proxy is set automatically).
* **Other Client Apps**: Most apps can access the internet directly. For the few that cannot, manually set their proxy address to `127.0.0.1` with port `10802` (SOCKS) or `10803` (HTTP).
* **Global Proxy**: If an app has no manual proxy option, enable Clash Win's **TUN Mode**.

> [!NOTE]
> **What is [TUN Mode]?**
> It creates a system virtual network adapter to fully hijack all network traffic. It is suitable for region-locked games, command-line terminals, and other software that ignores system proxy settings.
> *TUN mode consumes more resources and is prone to conflicts. Unless you have the specific needs above, the default settings are sufficient.*

### Key Features
* **[Auto Switch]**: Speed-tests and switches to the fastest node automatically after importing or switching subscriptions; avoids congested or failed nodes in real time.
* **[Rule Updates]**: Integrates popular rule sets and GeoIP databases, kept synced automatically.
* **[Mini-Monitoring]**: Shrink the window to its minimum size and Clash Win becomes a compact traffic meter.
* **[Right-Click Routing]**: Right-click any domain in the connection list to direct-connect, proxy, or reject (maximize the window to use).
* **[100% Native]**: No Chromium, no WebView2, no JS runtime — a single native executable.

---

## 快速开始指南

> **核心价值**：极简、零门槛、开箱即用，与 Clash Mini 相同的理念。

### 开始使用
点击 Clash Win 右上角的 [齿轮] 按钮，将订阅链接/节点配置（支持 19 种协议）粘贴到"订阅链接/节点配置"输入框，点击 [导入节点信息]，等待代理节点激活即可。

### 客户端配置
* **浏览器**：导入节点后即可直接浏览网页（自动设置系统代理）。
* **其他客户端应用**：大多数应用可直接上网。少数无法直接上网的应用，可手动设置代理地址为 `127.0.0.1`，端口为 `10802`（SOCKS）或 `10803`（HTTP）。
* **全局代理**：如果软件没有手动代理配置选项，请启用 Clash Win 的 **TUN 模式**。

### 核心功能
* **[自动切换]**：导入或切换订阅后自动测速并切换到最快节点；实时避开拥堵或失效节点。
* **[规则更新]**：集成流行规则集和 GeoIP 数据库，自动同步更新。
* **[迷你监控]**：将窗口缩小至最小尺寸，应用变为小巧的流量监控器。
* **[右键路由]**：在连接列表中右键任意域名，可直连、代理或拒绝（需最大化窗口）。
* **[100% 原生]**：无 Chromium、无 WebView2、无 JS 运行时，单一原生可执行文件。

---

## Screenshots

### 1. Main Window — Node List (主窗口 · 节点列表)
<p align="left">
  <img src="clashwin_pixel_perfect.png" alt="Main Window Node List" width="280" />
</p>

### 2. Settings Drawer (设置抽屉)
<p align="left">
  <img src="clashwin_drawer.png" alt="Settings Drawer" width="280" />
</p>

### 3. Subscription Import (订阅导入弹窗)
<p align="left">
  <img src="clashwin_modal_addurl.png" alt="Add Subscription Modal" width="360" />
</p>

### 4. Streaming Unlock (流媒体解锁检测)
<p align="left">
  <img src="clashwin_modal_unlock.png" alt="Streaming Unlock Modal" width="360" />
</p>

---

## Tech Stack

| Layer | Technology | Description |
| :--- | :--- | :--- |
| **Language** | **Modern C++20** | Strict ISO C++20, RAII, no exceptions in hot paths |
| **Graphics** | **Direct2D & DirectWrite** | Hardware-accelerated vector rendering, 3D bevel skeuomorphic UI, zero web engine |
| **Window Host** | **Pure Win32 API** | Custom frameless window, NCHITTEST hit-testing, WM_SIZING geometry clamping, Per-Monitor v2 DPI |
| **Networking** | **WinHTTP** | Direct REST + WebSocket communication with the Mihomo core |
| **Core Service** | **Win32 SCM API** | Shares `clash-verge-service` with Clash Mini / Clash Verge |
| **Build** | **CMake 3.20+ / VS 2022** | `CMakePresets.json` for reproducible configuration |

---

## Building from Source

### Prerequisites
* Windows 10/11 x64
* Visual Studio 2022 (or VS Build Tools) with the "Desktop development with C++" workload
* CMake 3.20+

```powershell
# 1. Configure (Visual Studio 2022 generator, x64)
cmake -B build -G "Visual Studio 17 2022" -A x64

# 2. Build Release
cmake --build build --config Release

# 3. Run
.\build\bin\Release\ClashWin.exe
```

The repository ships a large unit-test suite (offscreen pixel assertions included); run it via CTest against the built test target before submitting changes.

---

## Install & Usage

1. Download the latest zip from [Releases](https://github.com/qiu-yuxiao/clash-win/releases) (current line: **v0.1.0**).
2. Extract to any folder (portable — no installer, no registry writes).
3. Run `ClashWin.exe`, import your subscription, done.

---

## Relationship with Clash Mini (与 Clash Mini 的关系)

**Clash Win is the Windows-native C++ rewrite of [Clash Mini](https://github.com/qiu-yuxiao/clash-mini).**

| | Clash Mini | Clash Win |
| :--- | :--- | :--- |
| UI layer | Tauri + WebView2 + Rust | Pure Win32 + Direct2D + C++20 |
| Web engine | Yes (Chromium/WebView2) | **None** |
| Feature set | Baseline | **Aligned 1:1** (pixel-parity layout, same workflows) |
| Version line | 3.x | **Independent, starting from 0.1.0** |

Key facts:

* **Feature-aligned, implementation-independent.** Clash Win reproduces Clash Mini's user-visible behavior (layout, skins, workflows, rule engine pipeline) with a completely separate C++ codebase — no shared Tauri/Rust/TypeScript code.
* **Not an official replacement and not an upstream fork.** Clash Win is a sibling project by the same author; Clash Mini remains the primary line and is developed on its own track.
* **Designed to coexist with Clash Mini on the same machine.** All network ports are deliberately staggered (e.g. SOCKS `10802` / HTTP `10803` vs Clash Mini's `10801`; the single-instance port and the controller port conflict-relocation are also offset), and both clients share the same `clash-verge-service`. The one rule: **do not enable TUN mode or system proxy on both clients at the same time** — enable exactly one active proxy path.
* **Independent versioning.** Clash Win versions from **0.1.0** and does not track Clash Mini's 3.x release numbers.

---

## Copyright and Licensing

* **Lineage**: Clash Win is a clean-room native rewrite aligned with the features of [Clash Mini](https://github.com/qiu-yuxiao/clash-mini), which is itself a derivative of [Clash Verge Rev](https://github.com/clash-verge-rev/clash-verge-rev). We respect and acknowledge the copyright of the original authors.
* **Custom Portions**: Copyright (c) 2026 秋雨潇潇 <qiuyuxiao@gmail.com>. All rights reserved.
* **License**: GPL-3.0-only. See the [GNU GPL v3](https://www.gnu.org/licenses/gpl-3.0) for the full text.

## Development and Contributions

* Design documents: [`docs/phase1`](docs/phase1) (frontend/backend census, PRD) and [`docs/phase2`](docs/phase2) (C++20 architecture, task breakdown, user rulings).
* Each task (T01–T26) is one compilable, linkable, smoke-tested commit; see the git history for per-task acceptance criteria.
* Wiki: [Clash Win Wiki](https://github.com/qiu-yuxiao/clash-win/wiki)

## Links

* [Clash Mini](https://github.com/qiu-yuxiao/clash-mini) — the original Tauri-based client
* [Clash Verge Rev](https://github.com/clash-verge-rev/clash-verge-rev) — upstream lineage
* [Mihomo Core](https://github.com/MetaCubeX/mihomo) — the proxy engine
