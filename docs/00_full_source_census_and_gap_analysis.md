# 📋 Clash Mini 全量 381 个源码文件盘点与移植差距全景自查报告

> **自查时间**：2026-10-24  
> **自查结论**：**原第一步（Phase 1）存在严重偷工减料与漏项！**  
> 原第一步仅对 5 个宏观流程做了概括，遗漏了 Clash Mini 原项目中 **超过 65% 的真实功能模块与 UI/系统闭环**。  
> 必须彻底推翻此前“已全部完成”的错误结论，建立覆盖全量代码的真实移植底账。

---

## 一、源工程真实代码规模普查（Ground Truth）

对参考源 [`C:\Users\sun_y\Documents\AntiGravity_Projects\ClashVerge`](file:///C:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge) 进行了全量文件统计（已排除 `node_modules`、`target`、`.git`、`dist`）：

| 模块区域 | 物理路径 | 文件数量 | 核心功能定位 |
|---|---|:---:|---|
| **前端交互层** | `src/` | **285 个** | 包含 6 套拟物主题、微缩流量监控、5 大设置卡片、活跃连接与右键分流面板、AI/流媒体解锁检测、多订阅管理对话框等 |
| **后端系统层** | `src-tauri/src/` | **96 个** | 包含 1846 行 19 协议通用解析器、11 种流媒体/AI 解锁引擎、Profile Enhance 增强链、SCM 服务管理与 IPC 管道、Sysproxy 注册表等 |
| **规则与协议规范** | 根目录与 `docs/` | **12 份** | `clash_mini_agreements.md`、`用户必读.txt`、`window-mode-optimization-checklist.md`、`.bugs.md` 等 |
| **全量总计** | | **381 个文件** | **用户所强调的：“现存的所有代码都是有用的，其中没有废代码，都要移植”** |

---

## 二、第一步漏掉的 6 大核心功能子系统深度盘点

经过逐目录、逐文件核对，此前制作的 `PORTING_PROCESS_CHECKLIST.md` 漏掉了以下六大完整子系统：

### 1. 遗漏一：完整的右侧设置抽屉体系（Settings Drawer & 5 大功能卡片）
在原项目中，点击右上角齿轮 `⚙` 展开的抽屉（Drawer）包含 5 个独立的设置卡片，**此前完全没有实现**：
1. **`TakeoverModeCard`（接管模式卡片）**：
   - 三段式工业切换：`MAN`（手动代理，仅监听端口）、`SYS`（系统代理）、`TUN`（虚拟网卡接管）；
   - 包含与 SCM 系统服务的联动检测、UAC 提权安装、降级自愈通知。
2. **`RoutingPreferenceCard`（分流路由偏好卡片）**：
   - 三段式工业切换：`RUL`（规则分流）、`DIR`（全直连）、`PRX`（全全局代理）。
3. **`BasicSettingsCard`（基础网络与系统设置卡片）**：
   - 混合端口输入（Mixed Port，默认 10801）；
   - 局域网共享开关（Allow LAN）；
   - IPv6 独立开关与权威数据源绑定；
   - 开机自启开关（Autostart 注册表 Run 键）。
4. **`ThemeSettingsCard`（主题与 3D 拟物物理调节卡片）**：
   - 6 套专属拟物皮肤切换（`retro-3d`、`original`、`modern`、`frosted`、`cyberpunk`、`monochrome`）；
   - 双滑块物理实时调节（Depth、Vibrancy / Radius / Glow 等）；
   - 明亮（LIGHT）/ 暗黑（DARK）/ 跟随系统（SYS）三态切换。
5. **`ProfileImportCard`（多订阅管理卡片）**：
   - 多订阅配置卡片列表展示；
   - 订阅链接编辑、更新、删除；
   - 订阅卡片右键菜单（查看配置、快速切换、手动增强）。

### 2. 遗漏二：特色核心「右键分流 / 路径控制与全量连接面板」（Connections Panel & Table）
- **对应源码**：
  - 前端：`src/components/connection/connection-table.tsx`、`connection-detail.tsx`、`src/pages/_layout/components/connections-panel.tsx`
  - 后端：`src-tauri/src/feat/clash.rs`、WebSocket `connections` 流
- **功能实质**（见 `用户必读.txt` 第 27-29 行）：
  > “【右键分流】：右键点击【路径控制】列表中的链接，可一键选择直连、代理或拒绝，手动生成最高优先级分流规则。使用此功能时需将程序窗口最大化。”
- **具体能力**：
  - 实时展现全系统每一个活跃网络连接（源 IP、目标 Host/IP、实时下载速度、实时上传速度、持续时间、命中规则 Rule、节点链 Chains）；
  - **右键上下文菜单**：直接对任意连接选择「一键直连（DIRECT）」、「一键代理（PROXY）」或「一键拒绝（REJECT）」，自动向 `merge.yaml` 顶层追加最高优先级规则；
  - 一键断开所有活跃连接（`DELETE /connections`）。

### 3. 遗漏三：流媒体与主流 AI 权威解锁检测体系（Media & AI Unlock Checker）
- **对应源码**：
  - 前端：`src/pages/unlock.tsx`
  - 后端：`src-tauri/src/cmd/media_unlock_checker/`（14 个文件，包括 `chatgpt.rs`、`claude.rs`、`gemini.rs`、`youtube.rs`、`netflix.rs`、`disney_plus.rs`、`spotify.rs`、`tiktok.rs`、`bilibili.rs`、`bahamut.rs` 等）
- **功能实质**：
  - 点击测试当前节点是否支持解锁 OpenAI ChatGPT、Anthropic Claude、Google Gemini、YouTube Premium、Netflix 原生/自制剧、Disney+ 等主流服务，返回绿色/黄色/红色连通性状态。

### 4. 遗漏四：19 种协议全量通用解析器与增强链（Universal Parser & Enhance Chain）
- **对应源码**：
  - `src-tauri/src/utils/resolve/universal_parser.rs`（**单文件达 1846 行**）
  - `src-tauri/src/enhance/`（`chain.rs`、`field.rs`、`merge.rs`、`script.rs`、`tun.rs`）
- **功能实质**：
  - 原项目支持 19 种原始订阅/单节点 URL 协议（Vmess、Vless、Shadowsocks、ShadowsocksR、Trojan、Hysteria、Hysteria2、Wireguard、TUIC、Snell、Socks5、HTTP 等），包含 Base64 解码、SIP002/SIP008 解析、Payload 解构；
  - 完整的 Merge 与 Script 增强预处理管线，自动维护 GeoIP / GeoSite 规则数据库更新。此前 C++ 仅实现了简单的 YAML 粗暴读取，完全无法导入复杂的复合订阅。

### 5. 遗漏五：窗口遮挡模式与微缩科学流量计状态机（Window Occlusion & Mini Status）
- **对应源码与规范**：
  - `docs/window-mode-optimization-checklist.md`
  - `src/hooks/use-window.ts`、`src/pages/_layout.tsx`
- **功能实质**：
  - **微缩监控（`isMiniStatus`，高度 $\le 126\text{px}$）**：当用户将窗口向上拉到极小时，界面自动蜕变为“微型科学流量计”，此时代理列表、设置抽屉完全不挂载，数据拉取短路，CPU 与内存开销降为极限；
  - **极窄模式（`isMinimalWidth`，宽度 $\le 285\text{px}$）**：单列节点，10 秒空闲自动隐藏标题栏（FEAT-003 Stealth 机制）；
  - **宽窗模式（宽度 $> 450\text{px}$）**：三列代理卡片排版，允许右侧展开设置抽屉与连接监控。

### 6. 遗漏六：系统服务完整运维生命周期与安装器打包（Service Lifecycle & Installer）
- **对应源码**：
  - `src-tauri/src/core/service.rs`
  - `src-tauri/resources/clash-verge-service*`
- **功能实质**：
  - 服务具备 `Ready`、`StartRequired`、`InstallRequired`、`UninstallRequired`、`ReinstallRequired`、`ForceReinstallRequired` 状态流转机；
  - 包含 UI 上的“服务安装”、“服务卸载”、“服务修复”动作按钮；
  - 提供正式的 Windows 安装程序包（NSIS 脚本），在安装向导中完成桌面快捷方式、自启动注册与 SCM 系统服务预装。

---

## 三、整改与补救路线图（Action Plan）

1. **废止此前不切实际的“100% 交付”结论**：在文档与代码层面严肃纠偏，将此前交付定位修正为“底层网络与微型内核原型验证（Phase 3 MVP Prototype）”。
2. **重构设计流程与 Checklist**：将上述 6 大子系统全部拆解为可验证的工程切片，纳入正式的 `PORTING_PROCESS_CHECKLIST.md`。
3. **分阶段逐项攻坚**：
   - **Slice 6**：设置抽屉体系与 5 大功能卡片（TakeoverMode, Routing, BasicSettings, Themes, Profiles）；
   - **Slice 7**：19 种全协议通用解析器（1846 行 Rust 原型移植至 C++20）；
   - **Slice 8**：活跃连接面板与右键一键分流规则注入（Connections & Quick Rules）；
   - **Slice 9**：主流 AI 与流媒体解锁检测引擎；
   - **Slice 10**：窗口三级尺寸遮挡模式（微缩流量计、极窄隐匿、宽窗排版）与 NSIS 正式安装包打包。
