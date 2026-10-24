# Clash Mini 源码精读与逆向剖析：常驻、托盘与窗口生命周期（Window & Tray）

> **归档位置**：`docs/05_window_tray_lifecycle_analysis.md`  
> **关联清单项**：Phase 1.1.5 常驻、托盘与窗口生命周期（Window & Tray）  
> **对齐源工程**：`ClashVerge` (`src-tauri/src/core/tray/mod.rs`, `src-tauri/src/utils/window_manager.rs`, `src-tauri/src/utils/server.rs`, `src-tauri/src/feat/window.rs`, `src/providers/window/window-provider.tsx`, `clash_mini_agreements.md` 2.3–2.6, 2.9, 4.1–4.5)

---

## 1. 窗口几何常量体系与物理坐标铁律

### 1.1 核心尺寸常量
| 维度 | 逻辑像素 (Logical px) | 100% DPI (96) | 125% DPI (120) | 150% DPI (144) | 备注 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **默认启动宽 (DEFAULT_WIDTH)** | `271.0` | `271 px` | `339 px` | `406.5 → 407 px` | 兼容旧窄窗视觉密度（旧版无框阴影内缩消除后为 285 - 14 = 271） |
| **默认启动高 (DEFAULT_HEIGHT)** | `680.0` | `680 px` | `850 px` | `1020 px` | 经典单列/双栏完整展示高度 |
| **最小极限宽 (MINIMAL_WIDTH)** | `271.0` | `271 px` | `339 px` | `407 px` | 几何绝对下限 |
| **最小极限高 (MINIMAL_HEIGHT)** | `126.0` | `126 px` | `157.5 → 158 px` | `189 px` (恰好整数) | 极小隐匿状态触发高度（旧版 135/121 演进至 126） |
| **最大限制宽 (MAX_WIDTH)** | `640.0` | `640 px` | `800 px` | `960 px` | 大窗监控模式展开上限 |
| **最大限制高 (MAX_HEIGHT)** | `860.0` | `860 px` | `1075 px` | `1290 px` | 大窗监控模式展开上限 |
| **排版断点 (MINI_WIDTH_THRESHOLD)** | `285.0` | - | - | - | **排版断点非最小宽度**：$\le 285$ 为窄窗（单列卡片、卸载监控路由表） |

### 1.2 尺寸与位置口径唯一化（铁律）
- **尺寸**：全工程所有尺寸一律指**客户区（Inner Size / Client Area）**。
- **位置**：全工程所有坐标一律指**屏幕外位置（Outer Position / Screen Coordinates）**。
- **关闭无框阴影（`shadow(false)`）**：
  - Windows 原生无框阴影（`MARKER_UNDECORATED_SHADOW`）会在 `WM_NCCALCSIZE` 中强加物理内缩（150% DPI 下左右各 11px、上 2 下 11px）。若开启阴影，Outer Size $\neq$ Inner Size，导致鼠标缩放每帧都会因尺寸差放大一圈。
  - **在 ClashWin 原生 Win32 中**：自定义窗口过程完全掌控 `WM_NCCALCSIZE`、`WM_NCPAINT` 与双缓冲 Direct2D 客户区自绘，天然实现 Outer == Inner，彻底杜绝内缩偏差。

---

## 2. Win32 消息子类化与 `WM_SIZING` 防下移钩子

### 2.1 缺陷机理
在 Windows 无边框窗口中，标准 `WM_GETMINMAXINFO` 只限制矩形大小，不限制窗口原点。当用户**拖拽上边缘（Top Edge）向上压缩至最小高度**时，Windows 默认逻辑会不断累加 Y 坐标，导致窗口在缩到最小时整体向下漂移（Y 坐标持续变大）。

### 2.2 防御逻辑与 C++20 移植实现
- 拦截 `WM_SIZING` 消息：
```cpp
case WM_SIZING: {
    RECT* rc = reinterpret_cast<RECT*>(lParam);
    UINT side = static_cast<UINT>(wParam);
    UINT dpi = GetDpiForWindow(hWnd);
    double scale = static_cast<double>(dpi) / 96.0;
    
    int min_w = static_cast<int>(std::round(271.0 * scale));
    int min_h = static_cast<int>(std::round(126.0 * scale));
    int max_w = static_cast<int>(std::round(640.0 * scale));
    int max_h = static_cast<int>(std::round(860.0 * scale));

    // 1. 拦截上边缘拖拽（WMSZ_TOP, WMSZ_TOPLEFT, WMSZ_TOPRIGHT）
    if (side == WMSZ_TOP || side == WMSZ_TOPLEFT || side == WMSZ_TOPRIGHT) {
        if ((rc->bottom - rc->top) < min_h) {
            rc->top = rc->bottom - min_h; // 强制锁死 Top，严禁向下位移
        }
    }
    // 2. 拦截左边缘拖拽（WMSZ_LEFT, WMSZ_TOPLEFT, WMSZ_BOTTOMLEFT）
    if (side == WMSZ_LEFT || side == WMSZ_TOPLEFT || side == WMSZ_BOTTOMLEFT) {
        if ((rc->right - rc->left) < min_w) {
            rc->left = rc->right - min_w; // 强制锁死 Left，严禁向右位移
        }
    }
    return TRUE;
}
```

---

## 3. 几何状态持久化（节流与尾随补写机制）

### 3.1 持久化存储契约
- 文件路径：`%APPDATA%\clash_win\window_state.json`
- 结构体：
```json
{
  "width": 271.0,
  "height": 680.0,
  "x": 450.0,
  "y": 200.0
}
```

### 3.2 节流写入与防丢机制
1. **递增版本号（Sequence ID）**：每次 `WM_MOVE` 或 `WM_SIZE` 时原子自增 `seq`，写入前检查 `seq > written_seq`，抛弃乱序迟到的旧事件。
2. **节流阈值**：`GEOMETRY_SAVE_INTERVAL_MS = 1000ms`。
3. **尾随补写（Trailing Write Armed）**：拖拽停止瞬间如果处于 1 秒节流窗口内，必须排期一个延迟定时任务执行终值落盘，避免用户拖完窗口立即退出时丢失终值。
4. **守卫与 DPI 容差**：`GEOMETRY_MIN_EPS = 1.0`（逻辑像素），判断 `w >= 271 - 1.0 && h >= 126 - 1.0`，杜绝 `WM_DESTROY` 瞬间抛出的 `(0, 0)` 脏数据污染存储。
5. **多屏拔插越界保护**：
   - 启动恢复时调用 `EnumDisplayMonitors` 校验 `(x, y)` 是否落在当前已连接屏幕工作区内。
   - **特殊防御（最小化状态）**：Windows 最小化窗口会被系统移至 `(-32000, -32000)`，因此校验坐标前必须判断未处于最小化状态。

---

## 4. 极窄隐匿模式（Stealth Mode / 自动隐藏标题栏）

### 4.1 触发与退出条件
1. **激活条件**：
   - 处于窄窗：`physW <= 285.0 * scale + 1`
   - 处于极小高度：`physH <= 126.0 * scale + 1`
   - 用户连续 **10 秒**（`IDLE_HIDE_DELAY_MS = 10000`）无鼠标移动、点击、拖拽等交互。
2. **隐匿表现**：
   - 自动隐藏窗口顶部标题栏（Navigation Header、拖拽条、控制按钮）。
   - 仅显示当前高亮代理节点状态与延迟徽章，背景保持纯净拟物背景（无多余边框发光装饰）。
3. **唤醒与退出隐匿**：
   - 任何鼠标在窗口内的移动、悬停、点击或窗口尺寸调整立即退出隐匿，展示标题栏。
   - **自续期机制（防假死）**：10 秒空闲定时器必须无条件自续期，不得因单次条件不满足而注销失效。

---

## 5. 单实例互斥与进程间唤醒（Single Instance & IPC）

### 5.1 单实例检测与端口绑定
- **端口定义**：`SINGLETON_SERVER = 33335`（备用 `33336`）。
- **抢占与防 TOCTOU**：
  1. 进程启动第一步，立即尝试以 `SO_REUSEADDR` 绑定 `127.0.0.1:33335`。
  2. 若绑定成功：当前为主实例，暂存已绑定的 Socket 句柄，启动内置微型 HTTP IPC 监听。
  3. 若绑定失败：说明已有实例运行。解析命令行参数：
     - 若参数以 `clash:` 开头（URI 订阅唤醒）：向 `http://127.0.0.1:33335/commands/scheme?param=...` 发送 GET 请求。
     - 若普通点击：向 `http://127.0.0.1:33335/commands/visible` 发送 GET 请求。
     - 发送完毕后，次实例静默退出（`ExitProcess(0)`）。

### 5.2 主实例唤醒响应
- 收到 `/commands/visible` 时：
  1. 调用 `ShowWindow(hWnd, SW_RESTORE)`。
  2. 调用 `SetForegroundWindow(hWnd)` 抢占焦点并置顶激活。
  3. 立即触发首帧节点延迟刷新（首帧优先全量刷新）。

---

## 6. 系统托盘与常驻生命周期规范

### 6.1 绝对静态化准则
- **背景**：Windows 提权（UAC）环境下，频繁调用 Shell API 刷新托盘图标或 ToolTip 极易诱发系统级 `E_FAIL (0x80004005)` 导致托盘假死。
- **规范**：
  1. 托盘图标仅在接管模式变更时切换（3 态资产）：
     - TUN 接管激活：`tray-icon-tun.ico`
     - 系统代理激活：`tray-icon-sys.ico`
     - 普通旁路运行：`tray-icon.ico`
  2. 严禁运行期间以高频定时器刷新托盘动画或 ToolTip 文本。

### 6.2 托盘交互行为
- **左键单击**：防抖 300ms（`TRAY_CLICK_DEBOUNCE_MS = 300`）。若窗口隐藏则还原并激活到前台；若窗口已在前台则最小化/隐藏。
- **右键单击**：弹出极简 Win32 静态右键菜单：
  - `轻量模式 / Lite mode`（在 C++20 原生架构下表现为“隐藏窗口并冻结渲染引擎，保持后台代理不断流”）
  - `退出 / Exit`

### 6.3 窗口关闭与安全退出链路（Graceful Exit）
1. **用户点击窗口关闭按钮（X）**：
   - 不退出程序！拦截 `WM_CLOSE`。
   - 执行 `ShowWindow(hWnd, SW_HIDE)`。
   - 冻结 Direct2D 渲染帧循环与非必要 UI 轮询，CPU 占用降至 0%。
   - 内核进程（Mihomo）、TUN 虚拟网卡与网络连接**绝对不中断（不断流铁律）**。
2. **用户点击托盘菜单「退出 (Exit)」**：
   - 步骤 1：置位 `is_exiting = true`，托盘与窗口事件立即短路拦截。
   - 步骤 2：中止所有后台定时监控探针。
   - 步骤 3：重置系统代理注册表（`ProxyEnable = 0`，清空 `ProxyServer` 并调用 `InternetSetOptionW` 广播）。
   - 步骤 4：若处于 TUN 模式，向内核发送 `{"tun": {"enable": false}}` 或通知服务清理虚拟网卡路由。
   - 步骤 5：终止内核子进程（关闭 Job Object 触发内核彻底注销）。
   - 步骤 6：保存最终配置与日志缓冲区落盘。
   - 步骤 7：调用 `DestroyWindow(hWnd)`，释放托盘图标（`Shell_NotifyIconW(NIM_DELETE)`），退出主消息循环。

---

## 7. C++20 纯原生实现对照方案

| 原 Tauri / TS 实现 | ClashWin (C++20 / Win32 / Direct2D) 移植方案 | 优势 |
| :--- | :--- | :--- |
| `use-window.ts` + 8 方向 DOM Div | 原生 `WM_NCHITTEST` 返回 `HTTOP`、`HTLEFT` 等 | 0 DOM 损耗，Win32 硬件级平滑缩放 |
| `SetWindowLongPtrW` 子类化防下移 | 自身主 `WndProc` 中原生拦截 `WM_SIZING` 锁死 `rc->top` | 无需 hook 外部过程，无重入栈溢出风险 |
| `window_state.json` 异步落盘 | 独立后台 worker 线程处理节流版本化落盘 | 永不阻塞 Win32 消息泵 |
| 10s 空闲 React 定时器 | Win32 `SetTimer(hWnd, TIMER_IDLE, 10000, ...)` | 极低系统开销，自续期稳定 |
| Warp 微型 HTTP 唤醒服务 | Win32 本地 Socket 监听 `127.0.0.1:33335` | 内存占用 < 100KB |
| 托盘图标动态更新冲突 | 原生 `NOTIFYICONDATAW` + 模式变更事件驱动更新 | 彻底规避 COM / Shell API `E_FAIL` |
