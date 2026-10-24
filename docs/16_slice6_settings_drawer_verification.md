# 📑 第十六步成果物：切片六（交互式设置抽屉与 5 大功能卡片）研发与验证报告

> **所属阶段**：阶段四：模块化纵向切片递增实现（Incremental Functional Porting）  
> **对应清单项**：`4.6 切片六：【自查补全】交互式设置抽屉与 5 大功能卡片（Settings Drawer）`  
> **1:1 源码对照文件**：
> - [`src/pages/_layout/components/takeover-mode-card.tsx`](file:///C:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src/pages/_layout/components/takeover-mode-card.tsx)
> - [`src/pages/_layout/components/routing-preference-card.tsx`](file:///C:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src/pages/_layout/components/routing-preference-card.tsx)
> - [`src/pages/_layout/components/basic-settings-card.tsx`](file:///C:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src/pages/_layout/components/basic-settings-card.tsx)
> - [`src/pages/_layout/components/theme-settings-card.tsx`](file:///C:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src/pages/_layout/components/theme-settings-card.tsx)
> - [`src/pages/_layout/components/profile-import-card.tsx`](file:///C:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src/pages/_layout/components/profile-import-card.tsx)
> **验证程序**：[`build/bin/test_slice6_settings_drawer.exe`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashWin/build/bin/test_slice6_settings_drawer.exe)

---

## 一、移植目标与架构对齐

在 Clash Mini 中，点击右上角齿轮 `⚙` 展开的抽屉（Drawer）承载了 5 大核心功能控制卡片。本切片在纯 C++20 / Direct2D 框架下完整实现了这 5 张卡片的排版绘制与交互逻辑，杜绝任何假功能与死界面。

---

## 二、5 大卡片详细实现与数据链路

### 1. 订阅与节点配置管理卡片 (`ProfileImportCard`)
* **源码对应**：`src/pages/_layout/components/profile-import-card.tsx`
* **交互能力**：
  * 内嵌 Direct2D 3D 文本输入框（带光标闪烁与输入法支持）与 `📋 粘贴`、`导入` 按钮；
  * 支持一键从 Windows 系统剪贴板读取订阅 URL；
  * 多订阅列表展示：每项包含激活标记（`✓`）、订阅名称、已用/总流量与到期时间；
  * 每项配备快捷操作按钮：`🔄`（异步更新订阅）、`🗑`（删除订阅）；
  * 点击任意订阅卡片即可瞬间切换生效并自动拉起内核。

### 2. 流量接管模式卡片 (`TakeoverModeCard`)
* **源码对应**：`src/pages/_layout/components/takeover-mode-card.tsx`
* **交互能力**：
  * 三段式工业切换：`MAN`（手动代理，仅监听端口）/ `SYS`（Windows 系统代理）/ `TUN`（虚拟网卡接管）；
  * 状态机下方配有实时状态指示与安全提示；
  * **SCM 服务与 BUG-298 自动降级保护**：当切换至 TUN 时，自动核验 Administrator 令牌或 SCM `clash_verge_service` 服务状态；若普通权限且未就绪，自动安全降级回退至系统代理（System Proxy），保障网络不断流。

### 3. 分流路由偏好卡片 (`RoutingPreferenceCard`)
* **源码对应**：`src/pages/_layout/components/routing-preference-card.tsx`
* **交互能力**：
  * 三段式工业切换：`DIR`（全部直连）/ `RUL`（智能规则分流，出厂推荐）/ `PRX`（全局代理）；
  * 实时联动内核 REST API `/configs` 路由规则并写入 `verge.yaml` 持久化。

### 4. 基础网络设置卡片 (`BasicSettingsCard`)
* **源码对应**：`src/pages/_layout/components/basic-settings-card.tsx`
* **交互能力**：
  * 2×2 网格布局拟物胶囊开关（Capsule Switch）：
    1. **开机自动启动**：自动写入/删除注册表 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`；
    2. **Allow LAN**：局域网共享模式切换；
    3. **启动时最小化**：启动直接隐藏至系统托盘；
    4. **Allow IPv6**：IPv6 接管与防泄漏权威开关；
  * **Mixed Port 混合端口设置**：包含端口输入框（默认 10801）与“保存”按钮，支持 Enter 快捷保存。

### 5. 界面外观与物理质感卡片 (`ThemeSettingsCard`)
* **源码对应**：`src/pages/_layout/components/theme-settings-card.tsx`
* **交互能力**：
  * 6 套实体皮肤一键切换：`3D`（Retro-3D）、`Org`（Original）、`Mod`（Modern）、`Fro`（Frosted）、`Cyber`（Cyberpunk）、`Mono`（Monochrome）；
  * 主题模式三态切换：`SYS`（跟随系统）/ `LIGHT`（浅色）/ `DARK`（深色）；
  * 双动态滑块：随所选皮肤自动切换标签（如 3D 模式下的 `Depth` / `Vibrancy`、磨砂模式下的 `Opacity` / `Blur`、赛博模式下的 `Glow` / `Contrast`），支持平滑拖拽与实时局部重绘。

---

## 三、单元与集成测试验证结果

执行测试程序 [`test_slice6_settings_drawer.exe`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashWin/build/bin/test_slice6_settings_drawer.exe)：

```text
====================================================
[ClashWin 切片六] 交互式设置抽屉与 5 大功能卡片验证
====================================================
[Test 1] 验证流量接管模式卡片 (TakeoverModeCard) 状态流转...
  [SCM Info] 当前处于管理员或 SCM 服务就绪状态，TUN 模式已就绪
  [PASS] 流量接管模式卡片验证通过！
[Test 2] 验证分流路由偏好卡片 (RoutingPreferenceCard)...
  [PASS] 路由偏好三段状态持久化与内核配置映射验证通过！
[Test 3] 验证基础网络设置卡片 (BasicSettingsCard)...
  [PASS] 基础网络 2x2 网格开关与 Mixed Port 保存验证通过！
[Test 4] 验证界面外观卡片 (ThemeSettingsCard) 6 皮肤与双滑块...
  [PASS] 6 套物理拟物皮肤与专属双滑块动态标签验证通过！
[Test 5] 验证订阅管理卡片 (ProfileImportCard) 多订阅生命周期...
  [PASS] 订阅管理卡片多配置导入、切换、更新、删除验证通过！

>>> 切片六: 设置抽屉与 5 大功能卡片 100% 验收通过！<<<
```

---

## 四、自查核验结论

所有 5 大卡片及其子控件均在 [`src/window/main_window.cpp`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashWin/src/window/main_window.cpp) 与 [`src/render/d2d_renderer.cpp`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashWin/src/render/d2d_renderer.cpp) 中找到了明确的代码归宿，经由 Windows 消息泵与 Direct2D 原生管线驱动，100% 契合源工程 `ClashVerge` 的业务与交互规范。
