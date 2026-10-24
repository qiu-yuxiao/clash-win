# Phase 4 Slice 11 验收报告：官方 NSIS 安装包打包与预置系统服务 (Installer & Packaging)

## 1. 架构目标达成概述
针对此前用户对于 “ClashWin 是否需要安装？设定为 TUN 模式需要安装服务” 的核心关切，我们为 `ClashWin` 打造了完整的 Windows 官方 NSIS 安装向导脚本 `installer/ClashWin-Setup.nsi` 以及自动化构建与绿色分发流水线 `installer/package.ps1`。

解决了免安装环境下普通用户使用 TUN 模式需要反复 UAC 提权的痛点，实现了开箱即用、一键安装、静默预置 SCM 伴生服务、完备的注册表卸载注册与纯净卸载链路。

---

## 2. 核心组件与实现矩阵

### 2.1 官方 NSIS 安装脚本 (`installer/ClashWin-Setup.nsi`)
- **高 DPI 原生感知**：启用 `ManifestDPIAwareness PerMonitorV2`，支持 4K 及多显示器高分屏不模糊渲染。
- **权限与压缩**：采用 `RequestExecutionLevel admin`（确保注册服务与 Program Files 写入权限），启用 `/SOLID lzma` 高压缩算法。
- **全量文件布局与部署**：
  - `$INSTDIR\ClashWin.exe`
  - `$INSTDIR\sidecar\mini-mihomo.exe`
  - `$INSTDIR\sidecar\clash-verge-service.exe`
  - `$INSTDIR\sidecar\clash-verge-service-install.exe`
  - `$INSTDIR\sidecar\clash-verge-service-uninstall.exe`
  - `$INSTDIR\sidecar\enableLoopback.exe`
- **SCM 服务静默预注册**：
  - 安装过程中自动调用 `ExecWait '"$INSTDIR\sidecar\clash-verge-service-install.exe"'`。
  - 静默将 `clash_verge_service` 注册进 Windows 服务控制管理器 (SCM) 并启动，普通非管理员用户启动 ClashWin 即可直接开箱接管 TUN 模式虚拟网卡。
- **桌面与开始菜单集成**：
  - 创建桌面快捷方式 `$DESKTOP\ClashWin.lnk`。
  - 创建开始菜单文件夹 `$SMPROGRAMS\ClashWin`（含主程序与卸载向导快捷方式）。
- **注册表与控制面板集成**：
  - 写入 `HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\ClashWin`。
  - 注册 `DisplayName`, `DisplayIcon`, `DisplayVersion`, `Publisher`, `UninstallString`, `EstimatedSize`。
- **彻底纯净卸载**：
  - 卸载时调用 `clash-verge-service-uninstall.exe` 彻底清理 Windows 系统服务。
  - 终止残留进程，清理安装目录、快捷方式与注册表项，零文件残留。

### 2.2 自动化分发流水线 (`installer/package.ps1`)
- 自动将构建输出与伴生二进制统一聚合到 `build/dist/ClashWin/`。
- 自动生成独立免安装便携版 `ClashWin-Portable-x64.zip`（体积压缩至仅 18.7MB）。
- 附带提供免安装版本专用的 `install-service.bat` 与 `uninstall-service.bat` 管理脚本，支持右键一键提权部署服务。

---

## 3. 测试验证与结果

创建并执行独立单元与集成测试 `test_slice11_installer.exe`：
- `TestDistributionArtifacts`：验证所有 PE 可执行文件大小、签名完整性与分发目录就绪，**100% 通过**。
- `TestNsisScriptValidity`：验证 NSIS 脚本中的服务名、侧车二进制引用、UAC 权限级别与注册表清理项，**100% 通过**。

```
==================================================
  ClashWin Phase 4 Slice 11 - Installer Test      
==================================================
[Test 1] Testing Distribution Artifacts Integrity...
  -> Distribution artifacts & sidecars verified successfully!
[Test 2] Testing NSIS Installer Script Specification...
  -> NSIS Installer Script specification verified!

>>> [ALL SLICE 11 TESTS PASSED SUCCESSFULLY!] <<<
```

所有构建产物就绪：
- 便携归档：`build/bin/ClashWin-Portable-x64.zip` (18,742,473 字节)
- 安装目录：`build/dist/ClashWin/`
- NSIS 脚本：`installer/ClashWin-Setup.nsi`

Slice 11 任务全量交付完毕！
