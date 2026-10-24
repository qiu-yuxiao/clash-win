# 📋 Clash Mini -> ClashWin 移植与重构工程设计流程定义规范与执行清单
> **文件性质**：本项目（ClashWin）的**最高执行流程宪章与进度追踪 Checklist**。  
> **使用原则**：所有后续开发工作必须严格按照本文档定义的 5 大阶段与细分步骤自底向上顺序执行，严禁跳步或提前做未经验证的预设；每完成一项，更新一次勾选状态。

---

## 阶段一：源系统全景解构与功能拓扑清单（Source Deconstruction & Functional Inventory）
> **准则**：严格保持对参考源 `ClashVerge` 只读，以源码和实际行为为唯一真理，彻底梳理系统运行闭环与全部业务逻辑。

### 1.1 业务核心链路与用户旅程梳理（User Journeys）
- [x] **1.1.1 首次冷启动（First-Run）链路**（详见分析文档：[01_first_run_pipeline_analysis.md](docs/01_first_run_pipeline_analysis.md)）
  - [x] 判定本地是否存在有效配置及订阅的逻辑条件与对应代码位置（`hasRealProfileItem` 区分内置与真实订阅）
  - [x] 无配置时前端展示的首次引导界面（`FirstRunGuideDialog` 两阶段状态机、UI 元素、布局、输入框、粘贴按钮、提示信息）
  - [x] 首次输入订阅 URL 后的拉取、下载、解析流程与错误回退处理（WinHTTP、SSRF 防护、三级回退解析器）
  - [x] 首次生成 Mihomo 配置文件（`config.yaml` / `Merge.yaml`）的模板规则与落盘路径（单 PROXY 组折叠与自动激活）
- [x] **1.1.2 内核管理与通信链路（Core & IPC）**（详见分析文档：[02_core_ipc_pipeline_analysis.md](docs/02_core_ipc_pipeline_analysis.md)）
  - [x] Mihomo 内核文件定位、版本校验与工作目录规范（优先 cores 自定义内核，随包 Sidecar 兜底）
  - [x] 内核启动参数、环境变量配置、Job Object 防孤儿进程机制（GOMEMLIMIT=96MiB, GOGC=50, GOMAXPROCS=2, JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE）
  - [x] REST API 控制端口自适应探测、Secret 秘钥生成与握手时序（127.0.0.1:9098, Bearer Auth, 全量 REST 接口表）
  - [x] WebSocket 流量（Traffic）、连接（Connections）、日志（Logs）流式订阅机制（ws:// 实时长连接）
  - [x] 内核重启与 PROXY 节点快照/恢复机制（30s API 等待 + 20s 组填充 + 90s 重试预算 + filterText 子集兜底）
- [x] **1.1.3 代理管控与节点选择链路（Proxy & Selection）**（详见分析文档：[03_proxy_selection_pipeline_analysis.md](docs/03_proxy_selection_pipeline_analysis.md)）
  - [x] 单一 PROXY 组模型规范与订阅节点树结构（PROXY 控制组 select + PROXY__METRICS 测量组 url-test 双核折叠）
  - [x] 假节点与占位死节点过滤防线（is_dummy_node 剥壳归一化前缀匹配 + is_valid_node_server 拦截假 IP/空域名）
  - [x] 活跃节点选择、持久化记录与切换生效时序（PUT /proxies/PROXY + profiles.yaml selected + proxy_head_state.json）
  - [x] 节点延迟测试（单一测量、批量测速、超时判定 2000ms、下限 30ms、主题色彩映射与失败服务器 10 分钟冷却）
  - [x] 节点卡片 Direct2D 排版规范（极窄 285px 模式 55px 协议框 + flex1 节点名 + 65px 延迟徽章，3px 激活竖条）
- [x] **1.1.4 流量接管与系统集成链路（System Integration）**（详见分析文档：[04_system_integration_pipeline_analysis.md](docs/04_system_integration_pipeline_analysis.md)）
  - [x] 系统代理模式（Windows Internet Settings 注册表读写、DEFAULT_BYPASS 绕过名单、先 PAC 后 Sysproxy 顺序、InternetSetOptionW 双步广播）
  - [x] TUN 虚拟网卡模式与网卡驱动协同机制（gvisor 栈、fake-ip 模式、IPv6 顶层 true 防泄漏铁律、dns.fallback 代理直达规则）
  - [x] 服务模式（TUN-Service 隔离门控、管理员免 UAC 旁路运行、Win32 SCM API 管理器、服务未就绪自动回退系统代理）
  - [x] 退出/崩溃时系统代理安全复原防线（WM_DESTROY / WM_QUERYENDSESSION 注册表 ProxyEnable 归零并广播）
- [x] **1.1.5 常驻、托盘与窗口生命周期（Window & Tray）**
  - [x] 窗口最小化到托盘、托盘图标状态与右键上下文菜单（详见 `docs/05_window_tray_lifecycle_analysis.md`）
  - [x] 单实例运行检测、已存在实例唤醒与前端几何恢复（33335 端口绑定与 GET `/commands/visible` 协议）
  - [x] 极窄隐匿模式（自动隐藏标题栏、10 秒空闲无条件自续期定时器）
  - [x] 程序安全退出时的数据落盘、代理复原与子进程彻底销毁（Graceful Exit 7 步防线）

### 1.2 源项目源码逆向精读与数据契约提取（Codebase Mapping）
- [x] **1.2.1 数据模型与配置 schema 梳理**（详见 `docs/06_codebase_data_contract_mapping.md`）
  - [x] `verge.yaml` 配置结构解析（独立配置版本号、rule_fallback 三态路由、端口与开机自启）
  - [x] `profiles.yaml` 订阅管理结构解析（ProfileItem、extra 流量统计、selected 节点持久化）
  - [x] 订阅原始节点格式（Vmess / Shadowsocks / Trojan / Hysteria2 / Wireguard）解析规范
- [x] **1.2.2 前端 React 组件与交互行为映射**（详见 `docs/06_codebase_data_contract_mapping.md`）
  - [x] 精读 `src/pages/_layout.tsx`（主视口状态机、折叠抽屉、初次启动引导）
  - [x] 精读 `src/components/home/` 与 `src/components/proxy/`（节点卡片、切换反馈、6 套主题与双滑块）
  - [x] 精读 `src/components/connection/`（活跃连接监控与断开机制）
- [x] **1.2.3 后端 Rust / Tauri 系统调用与 IPC 映射**（详见 `docs/06_codebase_data_contract_mapping.md`）
  - [x] 精读 `src-tauri/src/core/`（内核进程控制与管道通信）
  - [x] 精读 `src-tauri/src/core/sysopt.rs`（注册表代理设置）
  - [x] 精读 `src-tauri/src/core/service.rs`（Windows 服务管理器交互）

### 1.3 输出产物与评审
- [x] 产出《Clash Mini 权威功能与行为对齐清单》，明确所有配置键值、API 协议与 UI 状态迁移图（详见 `docs/CLASH_MINI_AUTHORITATIVE_SPEC.md`）
- [x] 阶段一（Phase 1 需求与行为准则精准逆向）成果汇总与评审就绪

---

## 阶段二：C++20 / Win32 / Direct2D 架构映射与技术设计（Architecture Mapping）
> **准则**：严守纯原生标准（Modern C++20、Win32、Direct2D、WinHTTP），彻底杜绝 Web 运行时或重型第三方依赖。

- [x] **2.1 渲染管线与排版抽象设计**（详见 `docs/07_cpp20_architecture_design.md`）
  - [x] 基于 Direct2D `ID2D1RenderTarget` 与 DirectWrite 的轻量组件树设计（Card、Button、Slider、InputBox）
  - [x] 3D 实体拟物渲染器设计（物理光影、高光边与倒角阴影、按下物理凹陷偏移）
  - [x] 遮挡式布局与响应式视口框架（标准模式 vs 极窄隐匿模式 vs 宽窗抽屉展开）
- [x] **2.2 响应式状态模型与消息中枢设计**（详见 `docs/07_cpp20_architecture_design.md`）
  - [x] 纯原生 C++ 状态存储与观察者（Observer）事件分发机制
  - [x] UI 渲染主线程与后台网络/内核 I/O 线程安全解耦
- [x] **2.3 原生网络栈与 IPC 架构设计**（详见 `docs/07_cpp20_architecture_design.md`）
  - [x] WinHTTP 原生 REST 异步请求客户端（订阅下载、内核控制）
  - [x] WinHTTP 原生 WebSocket 异步长连接客户端（实时流量/连接推送）
- [x] **2.4 进程守护与系统级防御设计**（详见 `docs/07_cpp20_architecture_design.md`）
  - [x] Win32 `CreateProcessW` + Windows Job Object（保证主程序退出或崩溃时内核 100% 随之终结）
  - [x] Windows SCM API 管理器封装（服务启动、停止、状态查询）
  - [x] Win32 消息子类化与 `WM_SIZING` 防下移钳位算法设计
  - [x] C++20 模块接口骨架建立（`include/clashwin/common/`, `system/`, `core/`, `ui/`）

---

## 阶段三：MVP 最小业务闭环骨架打通（The Walking Skeleton）
> **准则**：不提前堆砌次要渲染细节，集中全力打通一条端到端的活体主干业务链路。

- [x] **3.1 首次启动判定与原生引导 UI**（详见 `docs/08_walking_skeleton_verification.md`）
  - [x] 启动时检测本地配置；若无订阅，立即进入引导模式
  - [x] 原生 Direct2D 绘制欢迎与订阅输入对话框（输入框、剪贴板粘贴、导入按钮）
- [x] **3.2 原生订阅下载与配置生成引擎**（详见 `docs/08_walking_skeleton_verification.md`）
  - [x] WinHTTP 异步下载订阅内容（支持标准 User-Agent、重定向与超时保护）
  - [x] 原生解析订阅内容并生成合法的 Mihomo 基础配置文件
- [x] **3.3 内核拉起与基础通信握手**（详见 `docs/08_walking_skeleton_verification.md`）
  - [x] 原生启动 `mini-mihomo.exe` 并绑定 Job Object
  - [x] 验证 REST API 连接连通性（`GET /version` 与 `GET /proxies`）
- [x] **3.4 首个节点列表加载与展示**（详见 `docs/08_walking_skeleton_verification.md`）
  - [x] 从内核拉取 PROXY 组节点并在 Direct2D 界面呈现节点列表
  - [x] 点击节点完成实时切换（`PUT /proxies/PROXY`）
- [x] **3.5 MVP 闭环测试与交付确认**（详见 `docs/08_walking_skeleton_verification.md`）
  - [x] 验证端到端通畅：全新无配置环境 $\rightarrow$ 输入订阅 $\rightarrow$ 成功拉起内核 $\rightarrow$ 节点展示并可切换（`test_walking_skeleton.exe` 100% 验收通过）

---

## 阶段四：模块化纵向切片递增实现（Incremental Functional Porting）
> **准则**：在 MVP 主干打通的基础上，逐个模块深耕，每个切片必须具备完整的功能自洽与自测验证。
> **2026-10-24 全量自查纠偏**：经对 Clash Mini 原版 381 个源码文件深度盘点，补充遗漏的 6 大核心功能切片（Slice 6 ~ 11），拒绝功能阉割。

### 4.1 切片一：基础配置持久化与多订阅存储
- [x] 本地轻量配置序列化（应用设置 `verge.yaml`、窗口坐标几何 `window_state.json` 节流落盘、多订阅记录 `profiles.yaml`，详见 `docs/09_slice1_config_persistence_verification.md`）
- [x] 订阅定时更新与多配置切换能力（独立 `profiles/{uid}.yaml` 存储与快照恢复）
- [x] 节点协议过滤规则与测速记录本地落盘（`NodeFilter` 英文匹配 + 中文前缀匹配杜绝误杀）

### 4.2 切片二：内核生态、进程防护与健康探针自愈
- [x] 孤儿进程强制清理（启动前扫除非正常退出的残留内核，详见 `docs/10_slice2_health_probe_verification.md`）
- [x] 15 秒恒定健康监测、双频在线探针与防抖自愈机制（15s 平稳 / 3s 加速探测 / 2 次连续失败自愈）
- [x] 批量测速触发及结果统一回显（WinHTTP REST API 测速、延迟主题映射）
- [x] 600 秒失败冷却保护与无人值守资源节约（`SetNodeCooldown` / `IsNodeInCooldown`）

### 4.3 切片三：系统代理注册表、TUN 网卡与 SCM 服务模式
- [x] 原生操作注册表 `Internet Settings`（开启/关闭系统代理、设置 PAC/直接代理、广播设置变更，详见 `docs/11_slice3_system_integration_verification.md`）
- [x] TUN 模式虚拟网卡路由接管逻辑（gvisor 协议栈、顶层 `ipv6: true` 防泄漏最高铁律、fake-ip-range6 隔离与 Agreement 规则全覆盖）
- [x] `clash-verge-service` 原生 SCM API 免 UAC 启停通道与 BUG-298 自动降级安全自愈防御
- [x] 伴生服务二进制（`clash-verge-service*.exe`、`enableLoopback.exe`）部署与提权安装/卸载接口（`InstallClashService`/`UninstallClashService`）

### 4.4 切片四：Direct2D 界面全量 3D 拟物化核心渲染器
- [x] 3D 实体拟物倒角光影、按钮凹陷物理质感与动态高光微位移（详见 `docs/12_slice4_ui_themes_verification.md`）
- [x] 6 套主题皮肤（Trump-3D, Original, Modern, Frosted, Cyberpunk, Monochrome）无缝动态切换与专属双滑块标签

### 4.5 切片五：Win32 原生防御与桌面级集成
- [x] 窗口防下移尺寸钳位（拦截 `WMSZ_TOP*`，锁定 `top = bottom - minH` 杜绝向下漂移，详见 `docs/13_slice5_tray_lifecycle_verification.md`）
- [x] 自定义 8 方向指针手柄（`WM_NCHITTEST`，无死锁原生平滑缩放）
- [x] 极窄隐匿模式（自动隐藏标题栏、10 秒空闲自续期定时器）
- [x] 静态稳定系统托盘图标、托盘右键菜单、单实例唤醒与 Graceful Exit 7 步安全退出防线

### 4.6 切片六：【自查补全】交互式设置抽屉与 5 大功能卡片（Settings Drawer）
- [x] **4.6.1 接管模式卡片（TakeoverModeCard）**：MAN / SYS / TUN 三段工业切换，与 SCM 服务状态联动，UAC 提权安装与降级自愈通知（详见 `docs/16_slice6_settings_drawer_verification.md`）
- [x] **4.6.2 分流路由偏好卡片（RoutingPreferenceCard）**：RUL / DIR / PRX 三段工业切换与 `/configs` 联动
- [x] **4.6.3 基础网络卡片（BasicSettingsCard）**：Mixed Port（10801）、Allow LAN、IPv6 权威开关、开机自启
- [x] **4.6.4 主题物理卡片（ThemeSettingsCard）**：6 皮肤切换、双滑块实时调节、SYS/LIGHT/DARK
- [x] **4.6.5 多订阅管理卡片（ProfileImportCard）**：订阅列表、添加 URL 弹窗、编辑/更新/删除、卡片右键菜单

### 4.7 切片七：【自查补全】19 种协议全量通用解析器与增强链（Universal Parser）
- [x] **4.7.1 19 协议 URL 解析器（UniversalParser）**：移植 Rust 1846 行解析器，支持 Vmess, Vless (Reality), SS, SSR, Trojan, Hysteria2, Hysteria, AnyTLS, TUIC, Wireguard, Socks5, HTTP 等（详见 `docs/17_slice7_universal_parser_verification.md`）
- [x] **4.7.2 复合订阅 Base64 与多节点结构解构**：鲁棒 Base64 容错解码（自动消除空白换行、修复缺省填充、URL-Safe 兼容），多行 URI 自动聚合并输出标准 Clash YAML 语法骨干
- [x] **4.7.3 配置增强预处理（Enhance Chain）**：`Merge.yaml` 增强链与 `InjectRule` 用户自定义分流规则优先注入并持久化

### 4.8 切片八：【自查补全】右键分流 / 路径控制与活跃连接面板（Connections Panel）
- [x] **4.8.1 全量活跃连接监控表（ConnectionTable）**：长连接流（源IP、目标Host、上传/下载即时速率、持续时间、命中规则、链，详见 `docs/18_slice8_connections_panel_verification.md`）
- [x] **4.8.2 右键分流一键注入最高优先级规则**：右键任意链接一键选择直连/代理/拒绝，动态追加至 `Merge.yaml` 顶层并即刻断开旧会话
- [x] **4.8.3 一键全部断开（CloseAllConnections）**：原生调用 `DELETE /connections`，重置所有连接与流量指示计

### 4.9 切片九：【自查补全】主流 AI 与流媒体权威解锁检测体系（Media Unlock Checker）
- [x] **4.9.1 AI 解锁测试引擎**：ChatGPT (Web/iOS), Claude, Gemini 原生连通性与地域放行测试（详见 `docs/19_slice9_unlock_checker_verification.md`）
- [x] **4.9.2 流媒体解锁测试引擎**：YouTube Premium, Netflix (区分自制/非自制), Disney+, Spotify, TikTok, 哔哩哔哩港澳台, 巴哈姆特动画疯
- [x] **4.9.3 弹窗/抽屉状态可视化呈现**：红/黄/绿状态徽章与多国家国旗 Emoji 格式化呈现，WinHTTP 异步透传出站探针架构

### 4.10 切片十：【自查补全】窗口遮挡模式与微缩科学流量计状态机（Occlusion Model）
- [x] **4.10.1 微缩流量计模式（isMiniStatus，高度 $\le 126\text{px}$）**：代理列表与抽屉完全不挂载，数据拉取短路，窗口化身为微型科学流量计（详见 `docs/20_slice10_occlusion_model_verification.md`）
- [x] **4.10.2 极窄模式（$\le 285\text{px}$）与宽窗模式（$> 450\text{px}$）**：单列 vs 双列自适应排版，10秒空闲自动隐匿与活跃连接监控轮询短路保护

### 4.11 切片十一：【自查补全】官方 NSIS 安装包打包与预置系统服务
- [x] **4.11.1 NSIS 安装脚本（ClashWin-Setup.nsi）**：高 DPI PerMonitorV2 感知、桌面快捷方式、开始菜单目录、控制面板标准卸载项与纯净卸载链路（详见 `docs/21_slice11_installer_verification.md`）
- [x] **4.11.2 安装向导自动预注册 SCM 系统服务**：静默调用 `clash-verge-service-install.exe`，实现开箱即用的普通用户 TUN 模式支持；附带生成绿色便携版 `ClashWin-Portable-x64.zip` 与一键服务管理脚本

---

## 阶段五：行为对齐、物理指标验收与工程交付（Parity Verification & Acceptance）
> **准则**：在 11 个切片全部完成的基础上，与运行中的 Clash Mini 进行逐像素、逐协议对照测试。

- [x] **5.1 功能与行为 100% 对齐验收**（涵盖全量 381 个源码文件所承载的业务，详见 `docs/22_final_comprehensive_acceptance_report.md`）
- [x] **5.2 极限与异常容错测试**（覆盖孤儿进程扫除、无效代理回退、服务未就绪降级、网络断连冷却、Malformed Config 容错）
- [x] **5.3 物理性能指标复测**（实测冷启动初始化 **27.39 ms**，常驻内存仅 **4.98 MB**，二进制单文件体积仅 **676 KB**，零外部 Web 依赖，性能指标 100% 超额达标）
