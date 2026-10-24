# 🗺️ Clash Mini (ClashVerge) ➔ ClashWin 源码 1:1 对照追溯与覆盖率矩阵

> **建立时间**：2026-10-24  
> **制定铁律**：**源程序中的所有代码，任意挑出任何一个功能与文件，都必须在 ClashWin 中有明确的代码映射与实现归宿。**  
> 本矩阵作为今后所有移植、自查、验收的**最高追溯基准**。任何未列入、未实现的源程序模块，均属于重大缺陷。

---

## 一、系统整体覆盖率总览

| 模块类别 | 源工程 (`ClashVerge`) 文件与规模 | ClashWin 对应架构与目标代码 | 当前实现状态 | 对应切片 |
|---|---|---|:---:|:---:|
| **1. 内核管理与生命周期** | `src-tauri/src/core/manager/`<br>`lifecycle.rs`, `state.rs` | `include/clashwin/core/core_supervisor.h`<br>`src/core/core_supervisor.cpp` | ✅ **已实现** | Slice 2 |
| **2. 健康探针与自愈轮换** | `src-tauri/src/core/delay.rs`<br>`src/services/delay.ts` | `include/clashwin/core/health_probe.h`<br>`src/core/health_probe.cpp` | ✅ **已实现** | Slice 2 |
| **3. Windows 系统代理** | `src-tauri/src/core/sysopt.rs` | `include/clashwin/system/sysproxy.h`<br>`src/system/sysproxy.cpp` | ✅ **已实现** | Slice 3 |
| **4. TUN 模式编译器** | `src-tauri/src/enhance/tun.rs` | `include/clashwin/config/tun_compiler.h`<br>`src/config/tun_compiler.cpp` | ✅ **已实现** | Slice 3 |
| **5. Windows SCM 系统服务** | `src-tauri/src/core/service.rs`<br>`resources/clash-verge-service*` | `include/clashwin/system/service_manager.h`<br>`src/system/service_manager.cpp` | ✅ **已完善** | Slice 3 |
| **6. 基础配置与多订阅持久化** | `src-tauri/src/config/`<br>`verge.rs`, `profiles.rs`, `prfitem.rs` | `include/clashwin/config/app_config.h`<br>`profile_manager.h` | ✅ **已实现基础** | Slice 1 |
| **7. 假节点与协议清洗** | `src-tauri/src/utils/node.rs` | `include/clashwin/config/node_filter.h`<br>`src/config/node_filter.cpp` | ✅ **已实现** | Slice 1 |
| **8. 3D 拟物实体渲染器** | `src/utils/button-styles.ts`<br>`src/pages/_theme.tsx` | `include/clashwin/ui/theme.h`<br>`src/render/d2d_renderer.cpp` | ✅ **已实现** | Slice 4 |
| **9. 桌面集成与防下移生命周期** | `src-tauri/src/core/tray/`<br>`src/hooks/use-window.ts` | `include/clashwin/ui/window.h`<br>`src/window/main_window.cpp` | ✅ **已实现** | Slice 5 |
| **10. 设置抽屉与 5 大功能卡片** | `src/pages/_layout/components/`<br>`takeover-mode-card.tsx`<br>`routing-preference-card.tsx`<br>`basic-settings-card.tsx`<br>`theme-settings-card.tsx`<br>`profile-import-card.tsx` | `src/window/main_window.cpp`<br>`src/render/d2d_renderer.cpp` | ✅ **已实现** | **Slice 6** |
| **11. 19 种全协议通用解析器** | `src-tauri/src/utils/resolve/`<br>`universal_parser.rs` (1846行)<br>`src-tauri/src/enhance/` | `include/clashwin/config/universal_parser.h`<br>`src/config/universal_parser.cpp` | ⏳ **待实现** | **Slice 7** |
| **12. 右键分流与活跃连接监控** | `src/components/connection/`<br>`connection-table.tsx`<br>`connection-detail.tsx`<br>`connections-panel.tsx` | `include/clashwin/core/connection_manager.h`<br>`src/window/connections_window.cpp` | ⏳ **待实现** | **Slice 8** |
| **13. AI 与流媒体解锁检测** | `src-tauri/src/cmd/`<br>`media_unlock_checker/` (14文件)<br>`src/pages/unlock.tsx` | `include/clashwin/network/unlock_checker.h`<br>`src/network/unlock_checker.cpp` | ⏳ **待实现** | **Slice 9** |
| **14. 遮挡模式与微缩科学流量计** | `docs/window-mode-optimization-checklist.md`<br>`isMiniStatus` (126px 阈值) | `src/window/main_window.cpp`<br>(WM_SIZING & 遮挡挂载状态机) | ⏳ **待实现** | **Slice 10** |
| **15. 正式 NSIS 安装包与服务预装** | `src-tauri/tauri.conf.json`<br>NSIS 脚本 | `installer/ClashWin-Setup.nsi` | ⏳ **待实现** | **Slice 11** |

---

## 二、逐文件详细映射矩阵（File-by-File Detailed Mapping）

### 1. 后端核心（Backend `src-tauri/src/`）

| 源文件 (`ClashVerge`) | 关键函数 / 结构体 | ClashWin 对应头文件与实现 | 状态 |
|---|---|---|:---:|
| `core/manager/lifecycle.rs` | `start_core()`, `stop_core()`, `restart_core()` | `CoreSupervisor::StartCore`, `StopCore`, `RestartCore` | ✅ 已实现 |
| `core/manager/state.rs` | `RunningMode::Service`, `Sidecar` | `CoreSupervisor::m_mode` | ✅ 已实现 |
| `core/service.rs` | `is_service_installed()`, `start_service()`, `install_service()`, `uninstall_service()` | `ServiceManager::QueryServiceState`, `StartClashService`, `InstallClashService`, `UninstallClashService` | ✅ 已实现 |
| `core/sysopt.rs` | `set_system_proxy()`, `clean_system_proxy()` | `SystemProxyManager::EnableProxy`, `DisableProxy` | ✅ 已实现 |
| `core/delay.rs` | `start_delay_service()`, 探针自愈机制 | `HealthProbeService::RunLoop`, 2-Strike Heal | ✅ 已实现 |
| `config/verge.rs` | `VergeConfig`, `rule_fallback`, `mixed_port` | `AppConfig::Load`, `Save`, `GetMixedPort` | ✅ 已实现 |
| `config/profiles.rs` | `ProfilesConfig`, `items`, `current` | `ProfileManager::GetProfiles`, `SaveProfiles` | ✅ 已实现 |
| `utils/node.rs` | `is_dummy_node()`, `is_valid_node_server()` | `NodeFilter::IsDummyNode`, `IsValidNodeServer` | ✅ 已实现 |
| `enhance/tun.rs` | `create_tun_config()`, `ipv6: true`, `gvisor` | `TunCompiler::CompileTunConfig` | ✅ 已实现 |
| `utils/resolve/universal_parser.rs` | `parse_subscription()`, 19 协议解析 | `UniversalParser::Parse` (Slice 7 目标) | ⏳ 待移植 |
| `cmd/media_unlock_checker/*` | 11 种 AI / 流媒体平台检测 | `MediaUnlockChecker::TestAll` (Slice 9 目标) | ⏳ 待移植 |

---

### 2. 前端交互与页面组件（Frontend `src/`）

| 源文件 (`ClashVerge`) | 关键 UI 元素 / 交互行为 | ClashWin 对应绘制与事件响应 | 状态 |
|---|---|---|:---:|
| `src/pages/_layout/components/active-node-card.tsx` | 活跃节点协议标签、点击切下一个、点击测速、IP:Port | `D2DRenderer::DrawActiveNodeCard`, `MainWindow::OnLButtonDown` | ✅ 已对齐 |
| `src/components/proxy/proxy-head.tsx` | 测速按钮 ⚡、三模排序 ↕、实时搜索框 🔍 | `D2DRenderer::DrawProxyHeadToolbar`, `MainWindow::OnChar` | ✅ 已对齐 |
| `src/components/proxy/proxy-item.tsx` | 3 列布局（55px协议 + 节点名 + 65px延迟）、3px激活条、斑马纹 | `D2DRenderer::DrawProxyItem` | ✅ 已对齐 |
| `src/pages/_layout/components/mini-traffic-panel.tsx` | 实时流量速率 `↓ DL / ↑ UL` 与总累计计数器 | `D2DRenderer::DrawTrafficPanel` | ✅ 已对齐 |
| `src/pages/_layout/components/takeover-mode-card.tsx` | 接管模式卡片（MAN / SYS / TUN 三段切，SCM 联动） | `D2DRenderer::DrawTakeoverModeCard` | ✅ 已对齐 |
| `src/pages/_layout/components/routing-preference-card.tsx` | 路由偏好卡片（DIR / RUL / PRX 三段切） | `D2DRenderer::DrawRoutingPreferenceCard` | ✅ 已对齐 |
| `src/pages/_layout/components/basic-settings-card.tsx` | 基础网络卡片（端口输入框、LAN 开关、IPv6 开关、开机自启） | `D2DRenderer::DrawBasicSettingsCard` | ✅ 已对齐 |
| `src/pages/_layout/components/theme-settings-card.tsx` | 皮肤卡片（6 皮肤按钮、双滑块实时响应、明暗模式） | `D2DRenderer::DrawThemeSettingsCard` | ✅ 已对齐 |
| `src/pages/_layout/components/profile-import-card.tsx` | 订阅管理卡片（导入输入框、📋粘贴、多订阅卡片管理列表） | `D2DRenderer::DrawProfileImportCard` | ✅ 已对齐 |
| `src/components/connection/connection-table.tsx` | 全量长连接表（Host、速度、规则、右键一键加规则、一键断开） | `ConnectionsWindow` | ⏳ **Slice 8** |
| `src/pages/unlock.tsx` | 流媒体与 AI 解锁检测卡片面板 | `UnlockWindow` | ⏳ **Slice 9** |
| `docs/window-mode-optimization-checklist.md` | `isMiniStatus`（$\le 126\text{px}$ 遮挡降级为科学流量计） | `MainWindow::OnSizing` 遮挡状态机 | ⏳ **Slice 10** |

---

## 三、自查验证机制（任何人任意抽检的闭环流程）

为保证**“任意挑任何一个代码，都在程序里得到对应”**，确立如下自查检验机制：
1. **抽检方法**：在源工程 `ClashVerge` 中任意指定一个文件（例如 `takeover-mode-card.tsx` 或 `universal_parser.rs`）；
2. **查验对照**：在本文档中查阅其对应的 C++ 目标文件与实现函数；
3. **功能验收**：运行 `build/bin/ClashWin.exe` 或对应的单元测试程序，验证该功能行为与源程序完全一致（包括像素排版、状态流转、错误回退等）。
