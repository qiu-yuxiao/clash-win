# 📜 Clash Win 设计契约与原生技术规范协议书

**版本**: v0.1.0-alpha  
**适用范围**: Clash Win 全体代码库、模块边界、渲染管线与系统 API 交互

---

## 目录

- [一、项目终极定位与设计准则](#一项目终极定位与设计准则)
- [二、原生窗口几何规范与 Win32 防御](#二原生窗口几何规范与-win32-防御)
  - [2.1 窗口尺寸常量与边界约束](#21-窗口尺寸常量与边界约束)
  - [2.2 WM_SIZING 防下移与非客户区点击测试](#22-wm_sizing-防下移与非客户区点击测试)
  - [2.3 极窄隐匿模式（自动隐藏标题栏规范）](#23-极窄隐匿模式自动隐藏标题栏规范)
- [三、Direct2D 3D 实体拟物渲染规范](#三direct2d-3d-实体拟物渲染规范)
- [四、内核生态与运行时管控](#四内核生态与运行时管控)

---

## 一、项目终极定位与设计准则

Clash Win 是专门针对 **Windows 平台** 深度量身定制的极致原生客户端，坚守 **3D 实体拟物与物理光影美学**，彻底告别所有基于 Chromium、WebView2 或 Electron 等重型 Web 渲染引擎的抽象层：

1. **绝对原生（Pure Native）**：全工程只使用 ISO C++20 与微软 Win32 / COM 原生技术，不包含任何 Node.js、V8 或 Web 运行时代码。
2. **极致轻量（Ultra-Low Memory）**：常驻内存严格控制在 **15MB – 25MB**，秒级（<150ms）闪电启动。
3. **零外部大依赖（Self-Contained）**：Direct2D、DirectWrite、WinHTTP、SCM API 均为 Windows 操作系统内置核心动态库，支持单文件便携运行。

---

## 二、原生窗口几何规范与 Win32 防御

### 2.1 窗口尺寸常量与边界约束

| 常量名称 | 逻辑数值 | 150% 物理像素 | 说明 |
| :--- | :---: | :---: | :--- |
| `MIN_WIDTH` | **271** | 406.5 → **407 px** | 极窄模式内容宽度，与无框几何无缝贴合 |
| `MIN_HEIGHT` | **126** | **189 px** (整数) | $126 \times 1.5 = 189$ 无余数，根除 DPI 舍入歧义 |
| `MINI_WIDTH_THRESHOLD` | **285** | 427.5 → **428 px** | 判定进入窄窗状态的最大逻辑宽度 |

- **尺寸口径唯一化（铁律）**：全工程所有窗口尺寸一律指**客户区（内尺寸）**，由 Direct2D 负责全视口绘制。
- **Per-Monitor V2 DPI 规范**：使用 `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`，响应 `WM_DPICHANGED`，按显示器动态重算逻辑像素到物理像素的映射。

### 2.2 WM_SIZING 防下移与非客户区点击测试

针对 Windows 在无边框（Frameless）窗口上边缘缩放至最小高度时会错误累加 Y 坐标导致整个窗口向下平移的底层缺陷：
- **上边缘缩放拦截**：针对 `WMSZ_TOP`、`WMSZ_TOPLEFT`、`WMSZ_TOPRIGHT`，当 `pRect->bottom - pRect->top < minHeightPx` 时，强制修正 `pRect->top = pRect->bottom - minHeightPx`。
- **自定义 8 方向指针手柄（WM_NCHITTEST）**：通过 6px 边缘和 14px 四角向操作系统返回 `HTTOP`, `HTLEFT`, `HTTOPLEFT` 等非客户区代码，直接借用系统平滑拖拽，杜绝卡顿。

### 2.3 极窄隐匿模式（自动隐藏标题栏规范）

- **激活条件**：当窗口处于窄窗区间（逻辑宽 ≤285）且高度处于极简高度（≤126），且用户连续 10 秒无交互时，自动进入隐藏标题栏极窄模式。
- **空闲计时器自续期**：10 秒定时器触发后**无条件重新排程**，用户鼠标移动、按键或拖拽窗口立即唤醒并退出隐匿模式。
- **极简纯净边缘**：移除任何外框发光装饰（无 GlowBorder），呈现纯净沉浸的 3D 实体界面。

---

## 三、Direct2D 3D 实体拟物渲染规范

1. **硬件加速矢量管线**：使用 `ID2D1HwndRenderTarget` 结合 DirectWrite 排版，所有 UI 元素均通过 Direct2D 图元绘制。
2. **3D 拟物倒角光影标准**：
   - 卡片与按键顶边与左边：绘制 1px 高光（Specular Light Edge，`rgba(255, 255, 255, 0.15 ~ 0.6)`）；
   - 卡片与按键底边与右边：绘制 1px 倒角暗部（Bevel Shadow Edge，`rgba(0, 0, 0, 0.35 ~ 0.5)`）；
   - 按下（Pressed）状态物理凹陷：高光与阴影边缘反转，文本与图标向右下偏移 1px。

---

## 四、内核生态与运行时管控

1. **Mihomo 核心交互**：
   - 通过原生 `WinHTTP` 异步客户端与 `127.0.0.1:9098` 控制器 REST API 交互；
   - 订阅节点流量与健康延迟使用原生 WebSocket 连接。
2. **提权服务控制**：
   - 通过原生 Win32 SCM API（`OpenSCManagerW`, `OpenServiceW`, `ControlService`）管理 `clash-verge-service`，保障 TUN 模式在无 UAC 弹窗下平滑启停。
