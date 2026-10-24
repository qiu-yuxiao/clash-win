# ClashWin 移植简单 PRD

> 产品经理 Alice · 2026-10-24
> 事实基础：`docs/phase1/01_frontend_census.md`、`docs/phase1/02_backend_census.md`（下称"前端普查/后端普查"）+ 4 张 UI 地面真相截图。所有数据均引自普查报告，标注"未确认"处不得自行臆断。
> 项目：Clash Mini（Tauri+React+Rust）→ ClashWin（C++20 / Win32 / Direct2D / WinHTTP / SCM，零 Web 运行时），1:1 移植，参考源 `C:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge` **绝对只读**。
> Programming Language：C++20 + Win32 + Direct2D；Project Name：clashwin

---

## 1. 产品目标与验收标准（四同）

| # | 目标 | 可检验验收标准 |
|---|------|----------------|
| G1 | **外观同** | 同尺寸窗口下逐像素级对照：窗口默认 271×680，最小 271×126，最大 640×860，断点 285px 宽；标题栏 30px；底部流量栏 135px（窄窗 100px）。亮/暗两套 iOS 风色板（light primary #007AFF / bg #F5F5F5；dark primary #0A84FF / bg #2E303D）；6 套控制皮肤（retro-3d 默认/original/modern-flat/frosted-glass/cyberpunk/monochrome）切换后控件观感一致。验收方式：与 Clash Mini 同订阅、同设置下截图比对，布局/配色/控件形态无肉眼可见差异 |
| G2 | **功能同** | 后端普查 lib.rs 注册的 Tauri command 全集 + tauri-plugin-mihomo 38 个命令的**行为等价**实现（IPC 形态可变为命名管道 JSON-RPC，但语义、参数、返回、超时梯度 10s/15s/30s/60s 一致）；订阅导入/解析（YAML→Base64→URI-list 三级回退）、节点过滤/排序/测速/切换、系统代理/TUN/服务模式、连接面板、解锁检测、托盘、开机自启全部可用 |
| G3 | **行为同** | 行为常量逐个一致（后端普查附录）：mixed 10801/socks 10802/http 10803/controller 9098/单例 33335；判死阈值 30ms–2000ms；monitor 15s/3s/5s 节奏；校验超时 10s；订阅超时 20s、YAML 上限 50MB；事件防抖 250ms、刷新节流 200/800ms；托盘点击防抖 300ms；窗口防抖 625ms（空闲 3s 豁免）。enhance 流水线生成的 clash-mini.yaml 规则链顺序逐行一致（含 PROXY__METRICS 组、gfwlist RULE-SET 位置、MATCH 兜底方向） |
| G4 | **结果同** | 同一订阅在两个客户端导入后：(a) 运行时配置 diff 为空；(b) 节点列表/延迟数据/连接表内容一致；(c) 系统代理注册表键效果一致（WinINET ProxyEnable/ProxyServer/ProxyOverride）；(d) 退出/重启后状态复原一致（节点选择、窗口几何 window_state.json、proxy_head_state.json） |

**总体验收**：双开 Clash Mini 与 ClashWin，执行同一脚本化操作序列（导入→切换→测速→开关 TUN/系统代理→重启→退出），两实例的 UI 截图、生成的配置文件、注册表/服务状态全部等价。

---

## 2. 用户故事

1. **首次启动**：作为新用户，我首次启动 ClashWin 时，若无任何真实订阅且未完成过引导，应用延迟 500ms 弹出两步向导（含 20s 超时降级），点"稍后"自动打开设置抽屉——与 Clash Mini 完全一致。
2. **订阅导入**：作为用户，我在抽屉"订阅与节点配置"卡粘贴 URL 点"导入"，订阅经下载（SSRF 校验、50MB 上限、Subscription-Userinfo 流量信息解析）导入并自动激活；也可直接粘贴 vmess/ss/trojan/vless/hysteria2 等 URI 列表导入为本地节点。
3. **节点浏览与切换**：作为用户，我在主窗口节点列表中按协议类型（左侧竖排标签）、名称过滤（顶部搜索框"过滤节点..."），点击节点即切换 PROXY 组当前节点，节点行显示延迟色。
4. **测速**：作为用户，我点击顶栏 TEST 按钮对全组测速，延迟结果按 DelayManager 缓存（TTL 30min）；窗口从最小化恢复时触发全节点静默测速（5 分钟冷却）。
5. **系统代理/TUN/服务**：作为用户，我在"流量接管模式"三段开关 MAN/SYS/TUN 间切换；TUN 在非管理员时要求走服务模式，服务未就绪时明确告警并回退系统代理（禁止静默降级）。
6. **设置抽屉**：作为用户，我通过顶部齿轮打开全屏覆盖抽屉，使用全部 5 张卡片（订阅导入/接管模式/分流路由偏好/基础设置/界面外观）+ 语言下拉 + 6 格皮肤选择条；宽≤285 时右侧连接面板不挂载。
7. **连接面板**：作为用户，我在抽屉右侧连接表中查看实时连接（上传/下载/目标/规则链），可复制、断开单条或全部连接。
8. **分流规则**：作为用户，我点"＋添加自定义分流规则"，在弹窗输入域名/IP（如 openai.com）并选择 DIRECT/PROXY/REJECT，规则写入自定义分流。
9. **解锁检测**：作为用户，我打开"流媒体与 AI 解锁检测"弹窗，12 项平台并发检测（B站陆/港台、ChatGPT、Claude、Gemini、YouTube Premium、Bahamut、Netflix、Disney+、Spotify、TikTok、Prime Video），显示地区码与已解锁/未解锁状态，可"重新检测全部平台"。
10. **托盘与轻量模式**：作为用户，我点关闭按钮时窗口隐藏并进入轻量模式（销毁主界面资源省内存，非退出）；托盘仅 2 项：轻量模式（勾选态）/ 退出；左键点击唤醒窗口；托盘图标随 TUN>系统代理>默认 三态切换。
11. **开机自启**：作为用户，我在基础设置打开"开机自启"，注册表 HKCU\...\Run 写入 "Clash Mini" 键（带 --silent）；启动时对账注册表与配置。
12. **单实例**：作为用户，我重复启动 ClashWin 时第二实例退出并唤醒已有窗口（TCP 33335 /commands/visible），深链 clash:... 转发（/commands/scheme）。
13. **退出安全复原**：作为用户，我退出应用时系统代理被复位、内核进程停止、配置落盘（prepare_exit→clean_async）；崩溃/关机/注销（Ctrl+C/Close/Shutdown/Logoff）同样触发安全清理；内核重启后 PROXY 组节点选择被快照并恢复（90s 重试预算）。

---

## 3. 需求池

### P0 — Clash Mini 实机可见、日常必经（Must have）

| 模块 | 需求 |
|------|------|
| 窗口框架 | 无框窗口、自绘 30px 标题栏（AppIcon+"Clash Mini Ver.{v}"+min/大尺寸/close 三键 28×28）、8 方向自绘拖拽边（computeResizeGeometry 钳制 271×126~640×860）、WM_SIZING 子类化上边缘钳制、窗口几何持久化 window_state.json（1s 节流+尾随补写）、大尺寸模式 toggleMaximize（禁 OS maximize）、极小窗闲置 10s 隐藏标题栏 |
| 窗口断点体系 | 宽≤285 → 窄窗 1 列布局（连接面板卸载、流量栏 100px）；宽≤285 且高≤126 → 极小窗（主内容卸载+数据精简）；尺寸持久化 last-non-large-size |
| 主界面 | 顶栏 30px 快捷条（ActiveNodeStatusCard 当前节点卡/TEST 按钮/置顶图钉/设置齿轮）、ProxyGroups 节点列表（虚拟滚动、单 PROXY 组架构、proxy-head 过滤/排序/测速工具条、proxy-item 延迟色）、MiniTrafficPanel Canvas 流量图（10fps、maxPoint 30、上传/下载速率+总量） |
| 订阅管理 | import/update/delete/patch/reorder profile、订阅下载管线（SSRF 校验、dirty-url 修复、Subscription-Userinfo、三级解析回退）、URI 直连节点导入（L_Direct_Imports.yaml 去重合并）、首次导入自动激活、定时更新（默认 1440min，失败 2min 重试） |
| 节点操作 | select_node_for_group、delay_proxy_by_name/test_delay、trigger_auto_select、激活选择三重校准防线（selected 校验+失败回写内核实际节点）、唤醒静默测速 |
| 内核管理 | mini-mihomo.exe 进程管理（-d -f -ext-ctl-pipe 参数、GOMEMLIMIT/GOGC/GOMAXPROCS 环境变量）、Sidecar/Service 双运行模式、monitor 健康监测自愈线程（15s/3s/5s、连续 2 次判死、自愈 3 次→restart_core、5 次弹 MessageBoxW）、重启快照恢复 PROXY 组、启动前 kill_all_mini_cores、配置热应用（PUT /configs?force=true→restore）、clash-mini-check.yaml 子进程校验（-t，10s） |
| 配置增强 | enhance 流水线全量移植：合并所有订阅 proxies（重名加 #n (HHMMSS) 后缀）、全局/Profile 级 Merge/Script/Rules/Proxies、merge_default_config、enforce_mini_agreements（PROXY select 组+PROXY__METRICS url-test 组+gfwlist 规则链+DNS 保通对偶规则+GEOIP,CN+MATCH 兜底）、use_tun 注入（fake-ip、ipv6 防泄漏）、apply_mandatory_dns_settings |
| mihomo 通信 | 命名管道 \\.\pipe\mini-mihomo 上 HTTP/1.1（连接池 min3/max16/idle30s、BUSY 重试 24×125ms）+ WS 四通道 /traffic /memory /connections /logs（Bearer secret 鉴权）；REST 端点全集 |
| 系统集成 | 系统代理写 WinINET 注册表+广播刷新、默认 bypass 列表、GuardMonitor 代理守护（默认 30s）、PAC 模式（:33335/commands/pac）、TUN 配置注入（零网卡操作）、SCM 服务模式（clash_verge_service，UAC 提权，SERVICE_WAIT_MAX 30s）、开机自启注册表+启动对账、单实例 TCP 33335 唤醒/深链 |
| 设置抽屉 | 5 张卡片全功能：订阅导入卡（粘贴/导入/列表右键菜单/流量信息行）、接管模式卡（MAN/SYS/TUN 三段+状态行）、分流路由偏好卡（DIR/RUL/PRX 三段+添加自定义分流规则弹窗）、基础设置卡（开机自启/启动最小化/Allow LAN/Allow IPv6/Mixed Port+保存）、主题设置卡（3D/Org/Mod/Fro/Cyber/Mono 六皮肤格、SYS/LIGHT/DARK 三段、Depth/Vibrancy 滑块）；底部语言下拉+皮肤选择条（高≤830 隐藏） |
| 连接面板 | 连接表（列管理/排序/右键复制断开）、单连接详情 Popover、close_all_connections/close_connection、snapshot+delta(epochId/sequenceId) 协议 |
| 弹窗群 | 首次启动两步向导、添加自定义分流规则弹窗、流媒体解锁检测弹窗（12 项并发、状态缓存 clash_verge_unlock_results/time）、编辑订阅/客户端更新/内核更新/日志 4 弹窗、Snackbar 通知（上限 50 条，success 3000/info 5000/error 8000ms） |
| 主题系统 | 亮/暗两套 iOS 色板、6 套控制皮肤（data-control-skin 分支逻辑→button-styles 等价生成）、primary 未配置取系统 AccentColor、original 强制 hsl(239,26%,49%)、CSS 变量系统等价（Direct2D 主题参数表）、背景图+blend+opacity、极光动画背景 |
| 托盘 | 2 项菜单（轻量模式勾选态/退出）、图标三态内嵌、左键唤醒（300ms 防抖）、tooltip "Clash Mini" |
| 轻量模式 | 关闭按钮→隐藏+轻量模式（销毁主界面资源+断开 WS）、托盘/唤醒重建 |
| 退出复原 | prepare_exit→clean_async（保存配置、复位系统代理、停内核、停 Timer/monitor）、信号注册（Ctrl+C/Close/Shutdown/Logoff） |
| i18n | 见"待确认问题清单"——语言范围待裁定，机制（fallback=zh、zh-cn/zh-tw 归一、verge-language 持久化）为 P0 |

### P1 — 有实现但常规使用不可见或边缘（Should have）

| 需求 | 说明 |
|------|------|
| 深链 scheme | clash:... 经单实例 server /commands/scheme 转发（scheme.rs 参数细节未确认） |
| 诊断包 | export_diagnostic_bundle（cmd/diagnostics.rs 502 行） |
| 图标缓存 | download_icon_cache/copy_icon_file + convertFileSrc 等价（订阅节点图标本地缓存） |
| 内核热升级 | check_core_update（GitHub API）+ start_core_upgrade（下载+sha256+热替换 cores/mini-mihomo.exe+进度事件） |
| 客户端自更新 | plugin-updater 等价（check/download/install）——ClashWin 是否需要待确认 |
| 服务管理增强 | reinstall_service/repair_service |
| 网络信息 | get_network_interfaces(_info)/get_system_hostname |
| 运行时检查工具 | get_runtime_yaml/get_runtime_exists/get_runtime_logs |
| 代理守护细节 | PAC/Sysproxy 顺序规则、自定义 bypass 追加/替换 |
| 帮助菜单全项 | 检查更新×2/导出诊断/打开目录/Wiki/重看向导 |
| 增强流量图 | enhanced-canvas-traffic-graph（Web Worker 采样，10min 原始+60min 压缩）——若 Mini UI 实际未展示则以普查确认为准（未确认是否可见） |
| 端口卫士 | 端口 0/越界回退默认、controller 端口冲突自动寻 9098 起空闲口 |
| 版本迁移 | migrate.rs（verge v3/clash v2/profiles v1，迁移前强制备份）——老用户升级场景 |
| 便携模式 | app_home_dir 切换逻辑 |

### P2 — 上游遗留疑似死代码（移植范围外，待用户裁定）

以下均来自普查报告明确标注的"未确认/疑似上游遗留"项，默认**不移植**：

1. `pages/logs.tsx` 路由外引用状态未确认（日志功能实际由日志弹窗承载，P0 已覆盖）。
2. `enable_hover_jump_navigator` / `quick-routing.ts` 实际消费方未确认。
3. `home_cards` / `test_list` 等 verge 字段在 Mini UI 中是否可见未确认（疑似上游遗留）。
4. 后端部分命令存在于文件但未列入 lib.rs 注册表（死代码嫌疑，以后端普查 §2 注记为准，不移植）。
5. `delay_group` 插件命令（已废弃未用）。
6. `polyfills/`（matchMedia/WeakRef/RegExp，Web 专用，原生环境无意义）。

> 以上各项标记"**移植范围外，待用户裁定**"。

---

## 4. UI 设计稿（依据 4 张截图固化规格）

### 4.1 主窗口窄模式（clashwin_pixel_perfect.png，窗口约 271×680）

| 区域 | 规格 |
|------|------|
| 标题栏 | 高 30px，bg 同窗口底色；左起 16px AppIcon + "Clash Mini Ver.3.0.10"（黑，约 12px）；右侧 min(—)/大尺寸(□)/close(×) 三键 28×28，hover 变灰，close hover 红 |
| 顶栏快捷条 | 高约 30px：当前节点卡（橙色高亮文字，选中节点名"v0-...a03.jiffdl.net"，白底胶囊 TEST 按钮，右侧图钉+齿轮图标按钮）；未选节点时显示占位文案 |
| 过滤工具条 | 高约 34px：左端闪电(⚡测速)+双箭头(排序)两个方形小按钮，右侧圆角搜索框占位"过滤节点..."，浅灰底细边框 |
| 节点列表 | 占主体。分组头行：左竖条橙色+协议名（VMESS/HYSTERIA2/ANYTLS/VLESS，灰 11px）+节点名（选中行橙色高亮+浅橙底，普通行黑字）；行高约 39px，行间细分隔线；虚拟滚动 |
| 流量图 | 高约 70px：双折线（下载蓝 #0A84FF、上传紫），无坐标轴，浅灰底 |
| 速率栏 | 两行：蓝色"↓下载:"+黑粗速率"180.2 KB/s"+右对齐灰色"总计: 816.3 MB"；橙色"↑上传:"+黑粗"12.1 KB/s"+"总计: 135.4 MB"，各带浅蓝/浅橙底色条 |
| 配色 | 窗口 bg 浅灰 #F5F5F5；主色橙（皮肤 retro-3d 下 primary 呈橙色系）；选中底色浅橙 |
| 断点 | 宽≤285 单列；流量栏高 100px；宽≤285 且高≤126 进入极小窗（列表卸载） |

### 4.2 设置抽屉（clashwin_drawer.png）

全屏绝对定位覆盖层（zIndex 100 等价：顶层子窗口/覆盖绘制），右上角独立 × 关闭按钮。左列 240px 固定宽卡片纵排，卡片白底圆角+细边框+粗体黑色标题；宽>285 时右侧挂 ConnectionsPanel。

| 卡片 | 内容规格 |
|------|---------|
| 订阅与节点配置（橙色标题） | 输入框占位"填入订阅链接..."+"📋粘贴"+"导入"按钮；下方当前订阅行：橙色勾+"默认主配置 (RdzAUK1VxD4t.yaml)"（橙色）+右侧 🔄/🗑 图标按钮；灰色副行"已同步 | 22 节点 | 1.8 MB" |
| 流量接管模式 | 三段开关 MAN/SYS/TUN（胶囊三格，选中格白底橙字粗体，未选灰字）；下方灰色状态行"SYS: 已接管系统代理 (127.0.0.1:10801)" |
| 分流路由偏好 | 三段开关 DIR/RUL/PRX（RUL 选中）；状态行"RUL: 智能规则分流 (推荐出厂默认)"；"+ 添加自定义分流规则"整宽按钮（打开 4.3 弹窗） |
| 基础设置 | 2×2 开关网格：开机自启(橙开)/Allow LAN(灰关)/启动最小化(灰关)/Allow IPv6(橙开)，iOS 胶囊开关；下排"Mixed Port"+深灰输入框"10801"+"保存"按钮 |
| 界面外观与物质感 | 皮肤 2 行 3 格：3D(选中，白底橙字)/Org/Mod、Fro/Cyber/Mono；主题三段 SYS/LIGHT/DARK；两条滑块："金条拟物景深 (Depth)" 0.6、"光泽饱和度 (Vibrancy)" 1.2，橙色滑轨圆点+右端数值 |
| 底部 | "📄 查看运行日志"与"💡 帮助与关于"两按钮；语言下拉与 6 格皮肤选择条（本皮肤下皮肤条即上方六格；高≤830 时皮肤条隐藏——未确认本截图高度是否触发） |

### 4.3 添加自定义分流规则弹窗（clashwin_modal_addurl.png）

- 模态居中小窗（约 360×290），白底圆角，遮罩半透明灰。
- 标题行："＋ 添加自定义分流规则"（黑粗体，＋橙色）+右上 ×。
- 说明行："请输入域名或 IP (例如: google.com 或 1.1.1.1):"
- 输入行：橙边框输入框（占位"openai.com"，实际为已输入内容）+"📋 粘贴"按钮。
- "分流策略偏好:"标签 + 三段开关 DIRECT/PROXY/REJECT（PROXY 选中，白底橙字粗体）。
- 右下"取消"（灰字白底）+"添加"（橙字白底）按钮对。
- 行为：写入自定义分流规则（addurl → rule_fallback 相关配置，具体落盘字段以 enhance/add-url-dialog 实现为对齐目标，未确认字段名）。

### 4.4 解锁检测弹窗（clashwin_modal_unlock.png）

- 居中弹窗（约 360×620），白底圆角+边框。
- 标题行："🎬 流媒体与 AI 解锁检测"（黑粗体，图标彩色）+右上 ×。
- 列表 10 行可见（12 项检测中本订阅展示 10 项）：每行白底圆角条目=平台名（黑粗体）+地区码（右对齐灰色小字，如 usUS/jpJP/sgSG/twTW/HkHK）+右侧状态徽章：绿色"☑ 已解锁"/红色"✖ 未解锁"。
- 平台清单（后端 12 项）：B站大陆/港澳台、ChatGPT、Claude、Gemini、YouTube Premium、Bahamut、Netflix、Disney+、Spotify、TikTok、Prime Video。
- 底部整宽按钮："🔄 重新检测全部平台"（橙字白底）。
- 结果缓存 clash_verge_unlock_results/clash_verge_unlock_time，重进显示缓存。

---

## 5. 待确认问题清单（提请用户裁定）

| # | 问题 | 选项/建议 |
|---|------|-----------|
| 1 | **JS 合并脚本引擎是否移植**：Clash Mini 的 Merge/Script 增强链含 JS 脚本（Script.js 必然存在，执行超时 3s），原生方案需嵌入 QuickJS/duktape | A. 嵌入 QuickJS 全量移植（推荐，行为同）；B. 先降级"不执行+告警"并标注差异 |
| 2 | **Monaco 编辑器替代**：Clash Mini 用 Monaco 编辑订阅/配置文件，纯原生无等价物 | A. 自绘简易文本编辑控件（Win32 Edit+语法高亮可选）；B. 只读查看+调用系统默认编辑器；C. 首版仅只读 |
| 3 | **13 语言去留**：上游 13 语言×11 命名空间；Clash Mini 实机可见语言为中文界面 | A. 先 zh/en 两语，架构预留全量；B. 13 语言全保留（翻译资产直接复用上游 JSON，只读引用） |
| 4 | **轻量模式等价实现**：上游靠销毁 WebView 省内存；ClashWin 无 WebView，主界面本就是轻量 HWND | A. 隐藏主窗口+销毁 Direct2D 资源+断开 WS+停刷新循环（对齐"省内存"意图）；B. 仅隐藏窗口（行为差异需标注） |
| 5 | **客户端自更新**（plugin-updater）是否移植，还是仅保留内核热升级 | 建议：仅内核热升级（P1），客户端更新待定 |
| 6 | **增强流量图（enhanced-canvas-traffic-graph）**：普查未确认其在 Mini UI 是否实际可见 | 待用户实机确认后决定 P1/P2 |
| 7 | **上游遗留死代码清单**（§P2 六项）是否维持"不移植"裁定 | 默认不移植 |
| 8 | **未确认细节的补充侦查**：core-upgrade-progress 事件名、scheme.rs 深链参数、sysproxy 注册表键写法、前端 WS 转发事件名、clash-verge-service-ipc 内部实现——是否需要安排第二轮只读侦查 | 建议需要，否则 G3 行为同验收有盲区 |

---

## 附：验收比对清单（供架构师/测试直接引用）

- 配置文件：clash-mini.yaml / clash-mini-check.yaml / profiles.yaml / verge.yaml / window_state.json / proxy_head_state.json 逐项 diff。
- 注册表：HKCU\...\Internet Settings（ProxyEnable/ProxyServer/ProxyOverride/AutoConfigURL）、HKCU\...\Run（"Clash Mini"键）。
- 服务：SCM 中 clash_verge_service 状态与启动参数。
- 端口：10801/10802/10803/9098/33335 监听状态。
- 行为常量：后端普查附录逐条核对。
