# Clash Win 移植项目文档索引

一句话定位：把 **Clash Mini 3.2.2**（React/TS/Tauri/Rust）按 1:1 语义移植为 **C++20 + Win32 + Direct2D** 的原生 Windows 客户端，显示名唯一例外为 `Clash Win`。

## 篇目

| 文件 | 导读 |
| --- | --- |
| [01-目的与方针.md](01-目的与方针.md) | 移植目标、与 Mini 的共存契约（端口/服务名/互斥项）、版本起点、三条决策方针 |
| [02-方法与流程.md](02-方法与流程.md) | 读源码到写 C++ 的固定顺序、`ui_src_gate` 出处门禁、验收证据链、小步重编译纪律 |
| [03-踩坑与避坑清单.md](03-踩坑与避坑清单.md) | 14 条已实际发生的错误：DPI 二次缩放、use-after-move 启动崩溃、抽屉吃标题栏、端口硬编码等 |
| [04-工程技巧.md](04-工程技巧.md) | 隔离测试、DPI 探针、PrintWindow+OCR、并排对照、SEH 崩溃钩子、单实例探测、WMI 启动 |

历史设计资料保留在 `docs/phase1/`（源码普查、PRD）与 `docs/phase2/`（C++ 架构、任务拆解、用户裁决）。

## 当前状态

**独立可用性（P0）— 此前遗漏、2026-11-05 补记**
- 要求：在**从未安装过 Clash Mini 的干净机**上全功能运行（含 TUN、系统代理）；服务自举是 Mini 既有行为，属 1:1 移植完整性。
- 依据与链路：`clash_mini_agreements.md` §1.8（附 Mini `file:line`）；`检测（未安装/STOPPED/未就绪）→ UAC 安装 → 启动 → IPC 就绪 → SYSTEM 内核`。
- 状态：代码侧已补"已注册但 ImagePath 失效 → 重新安装"、"启动失败/就绪超时 → 可见提示"，安装器在安装时已装服务（`clashwin.nsi:405-431`）。**干净机无服务 → UAC 安装 → 三项 URL 通**的实测需一次 UAC，登记在"明日一次点击清单"。

**已验收**
- 共存：Mini 与本程序可同时运行；本程序端口 `10811/10812/10813/10814`、单实例 `33337`（`include/clashwin/config/constants.h:24-29`）；控制面实际走命名管道 `\\.\pipe\mini-mihomo-win`（`src/util/dirs.cpp:210`），`9108` 仅配置值、未监听（实机 `Get-NetTCPConnection` 只见 10811/33337）；`/proxy.pac` 404 属预期（未开 PAC）。
- TUN：`verge.yaml enable_tun_mode=false`，默认路由已回 WLAN/直连，`10811` 出网 204。
- P0-1～P0-6 均已改完并通过隔离模式全量测试（531 cases / 0 failed）。
- 抽屉改为挂载在标题栏下方内容区（与 Mini `src/pages/_layout.tsx:1254-1424` 同构），标题栏常驻可点；关闭钮、主题卡（标题/Depth/Vibrancy）、日志入口 `LogViewerModal` 已实机验证。卡片/面板渐变已按源码 `button-styles.ts:538-539`（4 停靠）与 `index.scss:126/147`（3 停靠）重写（`src/ui/scenes/settings_drawer.cpp:133-173`），135° 单渐变，无竖向条纹；不再以单点采样判色。
- quickbar 胶囊：名称可收缩省略、延迟固定宽完整显示、gap 12px、字号 12/11 DIP；实测 CJK 字高 16～18 物理像素（≈12 DIP，与 Mini 同高 17 物理像素一致）。
- 节点行点击已修复（`NodeRowWidget::on_mouse` Down 未返回 true 导致 capture 被祖先截走，`src/ui/scenes/node_list.cpp:154-160`）；实机点行可切换，切节点后流量总量保留（47.8M→47.8M 不清零）。

**已闭环**
- 启动崩溃 `0xC0000005`：**use-after-move 已修复并闭环**（`main_window.cpp:241-252` move 前取裸指针）。全仓扫描补出 1 处真隐患（`core_manager.cpp:62-64` 环境块 move 后取 `size()`）；`Widget::add_child/insert_child` 已加空指针断言（`src/ui/widget.cpp:37/44`）。60.5 秒六类操作两轮复验无崩溃、无 `crash_*.log`、WS 连接失败有单次告警日志（`src/mihomo/ws_manager.cpp:370-377`）。

**2026-11-05 差异台账清零（本轮）**
- #3 连接面板文案：表头 `域名/地址` → `链接目标`，路由列改为中文 `直连/封锁/代理`
  （`src/ui/scenes/connections_panel.cpp`，依据 `locales/zh/connections.json:35/37-39`）。实机 OCR 命中 x≈421「链接目标」、x≈878「路由」、行内「直连」。
- #2 quickbar wifi 图标字号 13 → 12 DIP（`src/ui/scenes/quick_bar.cpp`，依据 `active-node-card.tsx:348-351`）。
  实机实测图标盒 18 物理像素 = 12 DIP，字形 17 像素。
- #1 dark 选中竖条按 MUI `primary.light`：`lighten(main, 0.2)`（`src/ui/scenes/node_list.cpp`）。
  **同时修正 palettes**：Mini 运行时 primary 由 CSS `AccentColor` 解析（`use-custom-theme.ts`
  `resolvedPrimary = setting.primary_color || getSystemAccentColor() || dt.primary_color`），
  本机实测 `#0075FF`，故 `src/ui/theme/palettes.cpp` 的 light/dark `primary_color` 由 `#007AFF`/`#0A84FF` 改为 `#0075FF`。
  实测我方竖条 `rgb(51,145,255)` vs Mini `rgb(51,144,255)`（ΔG=1，浮点取整），宽度 4 物理像素、高 38 像素，两侧一致。
- #5 `VK_APPS`：Mini **未实现**（全仓 `*.tsx` 只有 `onContextMenu`，无 `ContextMenu` 键判定；
  键盘处理仅 `first-run-guide-dialog.tsx:423-441`（Enter）与 `basic-settings-card.tsx:233-237`（Enter），
  均为 Enter）。故本程序不为该键单独加逻辑。

**2026-11-05 安装器与验收（本轮）**
- 0.1.0 包已按本轮源码重打并签名：`dist\ClashWin_0.1.0_x64-setup.exe`
  （18,270,488 字节，`Status=Valid / Subject=CN=ClashWin`）。
- `/KEEP` 连续两次静默安装实测：`exit=0`，安装后 `Program Files\ClashWin\clashwin.exe` 时间戳 = `installer\stage` 的 03:41:42；
  配置目录 `%APPDATA%\io.github.clash-win.clash-win` 的 10 个文件 SHA256 在两次安装前后**完全一致**
  （基线 `hash_before.txt` / 一次后 `hash_after.txt` / 二次后 `hash_after2.txt`，`Compare-Object` 输出为空）。
- UAC 发布者：安装包 Authenticode `Subject=CN=ClashWin`，UAC 提示的发布者即取自该 Subject；
  secure desktop 无法程序化截屏，此项以签名链为准。

**安装器交互：Clash Mini 真实行为与本程序映射**

Mini 侧没有任何自定义 NSIS 配置 —— `src-tauri/tauri.conf.json` 的 `bundle`（`:4-20`）里
**没有 `windows.nsis` 段**，因此全部走 Tauri 默认。逐条：

| 项 | Mini 实际值（依据） | 本程序 |
| --- | --- | --- |
| 安装作用域 | `currentUser`（Tauri 默认，`node_modules/@tauri-apps/cli/config.schema.json:3054-3056`） | `SetShellVarContext all`（机器级，需 UAC）—— 与 Mini 不同，见「明日一次点击清单」 |
| 语言选择器 | `false`（schema `:3083-3085` 默认） | 开启 `MUI_LANGDLL_DISPLAY`（`clashwin.nsi:167`） |
| 旧设定（升级） | **总是保留** `%APPDATA%`；Mini 无「全新安装」入口 | 新增「安装选项」向导页（`clashwin.nsi:110`），默认**全新安装**，勾选「保留原设定」则等同 `/KEEP` |
| `deleteAppDataOnUninstall` | 未配置 = `false` | 卸载确认页复选框，默认不勾（`clashwin.nsi:123/242`） |
| `installerHooks` | 无 | 无 |
| 更新产物 | `createUpdaterArtifacts:false`（`:19`） | 无 updater |
| 打包参数 | `tauri-action` + `--target x86_64-pc-windows-msvc`，无 NSIS 覆盖（`release.yml:145-159`） | `installer/build_installer.ps1` + makensis |

结论：Mini 的「旧设定」处理是**永远保留**且不可选；按用户口径，本程序保留「保留原设定」可见勾选项
（文案 `clashwin.nsi:131-140`，与 Mini 卸载页「同时删除配置数据」同一套用语），默认全新安装。
`/KEEP` 命令行参数仍保留用于自动化（`:188-192`），与勾选框等价。

**窗口尺寸口径**

- 以 Mini 源码默认值为准：`src-tauri/src/utils/resolve/window.rs:18-19`
  `DEFAULT_WIDTH = 271.0 / DEFAULT_HEIGHT = 680.0` → 150% 下 406×1020 物理像素（本程序 `src/ui/breakpoints.h:21-22` 一致）。
- Mini 现役窗口实测 407×1022（逻辑 271.33×681.33），属 DPI 取整，**不作为对齐目标**。
- `window_state.json` 恢复路径新增归一：`src/config/window_state.cpp:246-257` 把
  `(680, 681.5]` / `(271, 272.5]` 的 1px 级残留归一到默认 271×680，
  防止历史 681 被一次次带回来。现有两份 `window_state.json` 均为 `{"width":640,"height":820}`
  （测试时的宽窗状态），**无 681 残留**，无需额外覆盖。

## 四栏总台账（2026-11-05 夜间收口）

**已一致**（现值已达源码值）
- 帮助菜单文案结构、13 bold / 48 / 17 / `padding 8px 16px` / 135° 4 停靠（`settings_drawer.cpp:957-1021`）
- 主题卡三行 745/785/820（`settings_drawer.cpp:556-564`）
- 开关 28×14 / thumb 14 / 位移 14，行内 23.8×11.9（`switch_w.cpp:65-109`、`settings_drawer.cpp:447`）
- 卡片渐变 4 停靠 135°（`settings_drawer.cpp:148`）；面板 3 停靠不透明（`:160-173`）
- 选中卡 高 28 / 圆角 6 / `m:0`+容器 8（`quick_bar.cpp:241,42`）
- 抽屉无遮罩：Mini 抽屉是条件渲染 `theme-panel`，非 Dialog（`_layout.tsx:1403-1424`）；
  本程序 `mask_` 恒隐藏（`settings_drawer.cpp:596-600,736`）——**一致，不是口径偏差**
- 连接详情无可见遮罩（Mini 为 Popover，`Popover.js:337` `invisible`）：
  `connection_detail_dialog.cpp:109-111` 不画遮罩，点外部关闭 `:214-216`
- 连接详情按触发行锚定（Popover `anchorOrigin` bottom/left + `transformOrigin` top/left、
  `marginThreshold` 16）：首选贴在该行左下，下方不足上翻、右侧不足左翻，卡片 332×324
  （`connection_detail_dialog.cpp:88-137`；证据图 `out/conn_detail_03_popover.png`）

**已修差异**（本轮改完）
- 帮助菜单纯标题 → 动态后缀 `(v0.1.0)` / `(Ver.1.19.27 Mihomo)`（`settings_drawer.cpp:990-1021`）
- 主题卡标题行盒 18 → 20.4（半行距 1.2）（`settings_drawer.cpp:556-564`）
- 选中卡 29/9/8 → 28/6/0+12（`quick_bar.cpp:241,42`）
- 抽屉半透明 → 不透明（`settings_drawer.cpp:160-173`）；渐变竖纹 → 单次 135°（`:148`）
- 日志/客户端更新/内核更新弹窗遮罩 0.45 → **0.5**（MUI Backdrop 默认，`Backdrop.js:48`；
  `utility_modals.cpp:351,588,860`；解锁检测模态壳同步 `unlock_dialog.cpp:157`，ClashWin 自加豁免）
- 客户端/内核更新在下载/检查中点遮罩与 Esc 不关闭（对齐 Mini `onClose` 守卫，
  `utility_modals.cpp:484-487,495-501,717-723,731-740`）
- 安装器补 **Russian**（`clashwin.nsi:128-130`），三语与 Mini 一致；`makensis` 编译通过
- `updater/app-update.json` GitHub 地址 → `local://…`

**既定口径偏差（不追）**
- 窗口 407×1020 vs Mini 407×1022：271×680 DIP @150% 取整（`main_window.cpp:192-223`，源码 `window.rs:18-19`）
- 开关行内以布局 23.8×11.9 等价 `scale(.85)`，非 CSS transform；实测 ±1 px AA
- 字体度量不逐字宽（DirectWrite vs WebView2 字体栈）

**刻意偏差（用户指令，保留）**
- 安装默认「全新安装」会整目录 `Rename` 为 `%APPDATA%\io.github.clash-win.clash-win.bak-N`
  （非删除；Mini 安装从不移动配置）。生成规则：`clashwin.nsi:389-403` 从 `bak-1` 起取第一个空号。
  恢复：退出程序 → `%APPDATA%` 下把 `.bak-N` 改名回去（保留当前目录留底），详见
  `ACCEPTANCE-20261105.md` 10.5 的 6 步操作。

**待明日 UAC**
- 21 条僵尸 PID（`clashwin`×6、`clashwin_test`×15，threads=0）→ 重启清理
- 签名、完整打包、`/KEEP`×2、卸载、UAC 发布者目视 → 均需一次 UAC（见下表）

## 明日一次点击清单（需要 UAC）

| # | 动作 | 命令 | 预期 |
| --- | --- | --- | --- |
| 0 | 重启清理僵尸 PID | 正常重启一次 | `Get-Process clashwin*` 无 `threads=0` 残留 |
| 1 | 签名（读 `LocalMachine\My` 私钥） | `powershell -File scripts\sign.ps1 -Paths dist\ClashWin_0.1.0_x64-setup.exe` | 一次 UAC；`Get-AuthenticodeSignature` 返回 `Valid / CN=ClashWin` |
| 2 | 完整打包（含卸载器签名钩子） | `powershell -File installer\build_installer.ps1` | 一次 UAC（脚本自提升，`build_installer.ps1:27-37`）；产出签名包 |
| 3 | `/KEEP` 重装实测 | `dist\ClashWin_0.1.0_x64-setup.exe /S /KEEP` ×2 + 哈希比对 | 一次 UAC；两次后配置目录 SHA256 不变 |
| 4 | 卸载实测 | `"C:\Program Files\ClashWin\uninstall.exe" /S` | 一次 UAC；程序文件移除、配置目录保留 |
| 5 | UAC 发布者目视确认 | 双击安装包，看提示框顶部 | 显示「ClashWin」 |

**待办**
- 逐页并排图覆盖：已产出 主页/抽屉首屏/抽屉下半/连接面板/日志/编辑订阅(remote)/帮助菜单
  （`pair_07_help.png`，自绘 `ContextMenu` 已实拍，含动态版本后缀）；
  首次引导、添加网址、编辑订阅(local)、解锁整页——逐项结论见 `ACCEPTANCE-20261105.md`。
- 全量测试：隔离模式 **531 cases / 0 failed**（2026-11-05 夜间全量重建后复跑，`tests_full.log`）。
- 启动自检：`scripts\run_app_test.ps1 -AppDir build\bin\Release -Runs 3` → **PASS ×3**，
  Post **clean**、**NET-RESTORED**；21 条 `threads=0` 僵尸 PID 不计存活。
- 未签名包：`dist\ClashWin_0.1.0_x64-setup.exe`（**18,215,213 B**，
  SHA256 `75F66429DDC637D66FCB7B56C4BADD4D8CD56F9DC1CF6C625A054FEB37482447`，`NotSigned`）；
  签名版保留为 `dist\ClashWin_0.1.0_x64-setup-signed.exe`（18,270,488 B，`Valid`）。
- **不对外发布**：不建 Release、不推 tag、不上传资产；包只留本地 `dist\`。
- 窄窗抽屉高度差已归零：订阅卡 117 → 180 为主因（+94.5 px），主题卡标题行盒 18 → 20.4（半行距 1.2），
  三行 OCR **745/785/820** 与 Mini 零差（`settings_drawer.cpp:556-564`）。

## 关键路径速查

- 常量单一事实源：`include/clashwin/config/constants.h`
- UI 门禁：`scripts/ui_src_gate.ps1`；全量测试：`scripts/run_tests_v4.ps1`
- Mini 参照源码：`C:\Users\sun_y\Documents\AntiGravity_Projects\ClashVerge`
