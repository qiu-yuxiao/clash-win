# Win 和 Mini 的差别

> 生成日期：2026-11-09。审计范围：`clash_mini_agreements.md`（8 章 + 附录A 70 条 Bug ID + 附录B）逐条 × Clash Mini v3.2.2 源码 × ClashWin 源码；外加 `docs/bug_list.md` 13 条逐条核实。
> 判定口径：**Mini 源码为唯一真相源**（设计书条文与源码冲突时以源码为准）；唯一允许差异 = 显示名 Clash Mini → Clash Win。
> 本文档只记录问题，不记录一致项（一致项仅给统计）。**自 2026-11-09 起按用户指令逐条落地修复：已修复项在条末标注 ✅（代码已落地，待用户桌面复核），最高优先级「用户已确认」状态由用户重建后复核再标。**

## 总统计

| 判定 | 条数（去重后） | 说明 |
|---|---|---|
| ✅ 一致 | ≈182 | 含形态等价（技术栈不同、行为一致） |
| ❌ 确证缺失 | **9 项整条 + 2 项子条** | 见第二节，按用户可见影响排序 |
| △ 形态差异 | ≈50 | 绝大多数为技术栈等价；**8 项需关注**，见第三节 |
| ❓ 无法判定 | 16 | 见第四节，需人工/运行时复核 |

**最重要的一条交叉结论：`docs/bug_list.md` 全部 13 条用户报障（A1/A2/B3-B9/C10/D11-D13）在当前源码中均已与 Mini 一致或已含专项修复——病灶代码已不存在。强烈怀疑用户桌面运行的是旧构建（v0.1.0 基线之前的产物）。请先重新编译再复核现象；若复现再查运行时数据流。**（见第六节逐条证据）

---

## 一、❌ 确证缺失清单（Win 无对应实现，按用户可见影响排序）

### ❌1 §2.2/§2.8 宽窗三列节点卡片网格（影响最大、最直观）
- Mini：`src/components/proxy/use-render-list.ts:88`（`col = isMinimalWidth ? 1 : 3`）+ `proxy-render.tsx:88`（`repeat(3,1fr)`）——宽窗（内容宽 >285px）节点列表每行 3 张立体卡片，窄窗 1 列。
- Win：`src/ui/scenes/node_data.cpp:63-75` rows 平铺单列；`node_list.cpp:486-493` 单列虚拟滚动行卡（行高 26），全工程无 3 列分支。
- 用户可见：窗口拉宽后 Mini 是三列卡片墙，Win 仍单列窄行，一屏可见节点数约为 Mini 的 1/3，纵向长度约为 Mini 的 3 倍。
- **★ ✅ 已修复（代码已落地，待用户桌面复核）**：`src/ui/scenes/node_list.h/.cpp` 新增 `NodeGridRowWidget` 三列网格行（每行 ≤cols 张 `NodeRowWidget` 卡片复用既有卡片绘制）+ `kMinWidthForMultiCol=285.0f` 列数判定（内容宽 ≤285 单列，否则 3 列），列间 5px double 竖分隔（on_paint 先于子控件绘制，卡片透明区透出分隔线）；窄卡名称宽度下限保护（name_w<0→0）。← proxy-render.tsx:88 `repeat(3,1fr)` / use-render-list.ts:88 `isMinimalWidth`。

### ❌2 §5.6 延迟三级真源缺 `alive` 校验（历史事故复发窗口）
- Mini：`src/services/delay.ts:279-281`（`alive===false` → 一律 0）。
- Win：`src/ui/scenes/node_data.cpp:54-61` fetch_proxies 只存 history 的 delay/time、**丢弃 alive 字段**；`node_data.cpp:178-193` row_delay 回落 history 前不校验存活。
- 用户可见：内核判死节点在 30 分钟 TTL 过期后渲染为绿色旧延迟——正是设计书记录的 2026-10-24 事故（活跃栏 Timeout、列表绿色 106ms）在 Win 的复发路径。
- **★ ✅ 已修复（代码已落地，待用户桌面复核）**：`src/ui/scenes/node_data.h` `ProxyHistory` 增 `alive` 字段；`node_data.cpp::load()` 保留 `item.alive`（内核 alive 真源，← delay.ts:279-281）；`row_delay()` 在 `!alive` 时一律返回 0（Timeout），杜绝内核判死节点 TTL 过期后渲染绿色旧延迟（2026-10-24 事故复发路径已封死）。

### ❌3 §5.6 自愈失败不刷新前端代理缓存
- Mini：`monitor.rs:1131-1139`（无候选/切换失败同样 refresh_proxies）。
- Win：`src/core/auto_select.cpp:186` 无候选直接 return，不广播 refresh_proxies。
- 用户可见：全节点不可达时 UI 延迟/存活展示停在陈旧值，与 ❌2 叠加放大「断流节点绿色延迟」。
- **★ ✅ 已修复（代码已落地，待用户桌面复核）**：`src/core/auto_select.cpp::trigger()` 无候选（`!fastest`）分支新增 `events::EventBus::refresh_proxies()` 广播，刷新前端代理缓存（← monitor.rs:1131-1139 无候选/切换失败同样刷新），避免 UI 延迟/存活停陈旧值。

### ❌4 §5.1 探测按需降频缺失（拨测风暴风险）
- Mini：`monitor.rs:64`（HEALTH_DATA_FRESH_SECS=60）/ `:69`（DEAD_CONFIRM_MIN_INTERVAL_SECS=10）/ `:642-683` evaluate_failover 新鲜度门控。
- Win：`app.cpp:399-433` probe 钩子无门控，`monitor.cpp:327-334` 每 tick（15s/重试 3s）无条件全组拨测；重试模式最高 20 次/分钟 vs Mini 近似 0。
- 用户可见：复现 Mini 已修复的「探测自激误报」（monitor.rs:62-63 注释：高频拨测曾致 81ms 节点 17 秒后被误判死）风险，并放大内核与机场测速地址负载。
- **★ ✅ 已修复（代码已落地，待用户桌面复核）**：`src/app/app.cpp` `probe_metrics_with_fallback` 钩子新增按需拨测门控（← monitor.rs:64/69/673-683 evaluate_failover）：活跃节点 `alive` 且 history 新鲜 (<60s) → 免拨测；已判死且 10s 内刚复核 → 免重复拨测；其余才真正拨测。复现 Mini 修复的「探测自激误报」。

### ❌5 §2.10 设置开关失败回滚缺失（UI 状态 ≠ 后端状态）
- Mini：`use-minimal-settings.ts:77-79`（patch 失败 → `mutateInfo()` 权威值回滚，开关弹回）。
- Win：`main_window.cpp:909-920` 失败仅 push error 通知；开关停留在翻转位；`EventBus::kRefreshClash` 订阅（main_window.cpp:441-445）只 reload_nodes 不回写开关；sync_basic 唯一调用点在启动期（:1512）。
- 用户可见：patch 失败后「显示已开、实际未生效」持续到下次重启。
- **★ ✅ 已修复（代码已落地，待用户桌面复核）**：`main_window.cpp` `on_patch_verge` 三处失败分支新增 `resync_settings_ui()`，用 verge+clash 权威配置值回填抽屉开关（`sync_basic`/`sync_takeover` 的 `set_checked(…,false)` 不触发回调，避免二次 patch），对齐 Mini `use-minimal-settings.ts:77-79` patch 失败 → `mutateInfo()` 权威值回滚、开关弹回。

### ❌6 §2.10.1 开机自启 StartupApproved 覆盖写回缺失
- Mini：enable 时同步写 `StartupApproved\Run`=启用（任务管理器禁用后配置胜出重新启用）。
- Win：`src/core/autostart.cpp:28-42` 仅写 Run 值，无 StartupApproved 处理。
- 与 ❌7 叠加造成「开关显示开、开机不自启」错位。
- **★ ✅ 已修复（代码已落地，待用户桌面复核）**：`src/core/autostart.cpp` `WindowsAutostartAccess::enable/disable` 现同步写 `StartupApproved\Run` 键（REG_BINARY，首字节 0x02=批准启用 / 删除=禁用），使配置在任务管理器禁用后胜出重新启用（← Mini §2.10.1）。

### ❌7 §2.10.1 自启开关不读真实注册表状态
- Mini：`autostart.rs:65`（status = is_enabled 真实状态优先）。
- Win：`main_window.cpp:1512-1513` 传 verge 配置值；`autostart.cpp:137-140` get_launch_status 已实现但未接入 UI（BUG-293 的 cmd 层已有，UI 层未用）。
- **★ ✅ 已修复（代码已落地，待用户桌面复核）**：`main_window.cpp` `sync_basic` 调用点改为读 `AutostartManager::instance().get_launch_status()` 真实注册表状态（Run 键），不再用 verge 配置值（← Mini autostart.rs:65 真实状态优先）；`get_launch_status()` 原已实现。

### ❌8 §2.11 区域级错误降级缺失
- Mini：`AreaErrorFallback` 区域边界——单组件异常局部降级为一行提示，严禁整页白屏。
- Win：`src/ui` 全目录渲染路径无 try/catch 守卫（仅事件回调散落 catch）。任一 paint 路径抛异常即进程崩溃。
- **★ ✅ 已修复（代码已落地，待用户桌面复核）**：`src/ui/widget.cpp` `Widget::paint()` 的 `on_paint(rt)` 调用包入 try/catch，异常时局部绘制半透明红占位矩形（不向子控件冒泡），函数末尾照常 pop origin/clip/opacity，避免整树崩溃白屏（← Mini AreaErrorFallback 区域边界）。
- 备注：C++/D2D 渲染不抛异常的常态下风险面小于 Web，但第三方数据可触发 std::exception。建议至少对 node_list / connections / traffic 三个数据驱动重灾区在 paint 外层加 catch+占位绘制。

### ❌9 §2.12 「稍后配置」缺自动滑开抽屉指引
- Mini：`first-run-guide-dialog.tsx:469-473` 点「稍后」自动滑开左侧抽屉、指引订阅卡片位置。
- Win：`first_run_guide_dialog.cpp:611-615` finish() 仅置 completed + close，无抽屉动作。新用户失去位置引导。
- **★ ✅ 已修复（代码已落地，待用户桌面复核）**：`first_run_guide_dialog.cpp`「稍后」按钮 handler 在 `finish()` 后触发新增 `on_dismiss_later` 回调；`main_window.cpp` 将该回调接到 `settings_drawer_->set_open(true)`，完成引导后自动滑开左侧设置抽屉指引订阅卡位置（← Mini first-run-guide-dialog.tsx:469-473 handleDismissLater）。

### ❌10（子条）§8.5 自动更新闭环为空通道
- `release.yml:284` 明注不做 update_dev；`updater/app-update.json` 为静态空壳 `platforms: {}`。属 v0.1.0 已文档化的 Known Limitation (c)，非新遗漏；按 1:1 目标仍需补闭环。
- **★ ✅ 已修复（CI/代码已落地，待发版生效）**：`release.yml` 已落地「Generate updater manifest (app-update.json)」段落（lines 362-422，生成 `platforms.windows-x86_64` 的 url+sha256+signature），闭环非空通道；静态 `updater/app-update.json` 的 `platforms:{}` 由该段落于发版时回填真实数据（预期占位，← Mini §8.5 update_dev 生成段），客户端 `client_updater.cpp` 已消费该清单。

### ❌11（子条，✅已修复 commit 264624b）BUG-313③ 安装清理 Mini 残留
- 设计书 BUG-313③：初次安装强制清理 Clash Mini 残留进程/注册表/服务。
- Win 现状：`installer/clashwin.nsi:10` 明示服务名固定 `clash_verge_service`、与 Clash Mini **共用同一特权服务共存豁免**，且无清理 clash-mini/mini-mihomo 残留的 NSIS 段。与设计书方向相反，属有意架构决策分叉。
- **★ 用户裁决（2026-11-09，最高优先级，取代双方旧条文）**：Mini/Win 互斥共存方针——可同时安装、绝对避免同时运行；**启动时+安装时都杀对方全部进程（含服务）**；不删对方磁盘任何东西；服务名固定 `clash_verge_service` 两侧同名，各自安装时注册指向自己路径，需要时启动自家路径服务（接管注册）；强杀+按安装目录路径判定归属。
- **✅ 已修复（commit 264624b，仅本地）**：新增 `src/system/coexist`（按路径归属强杀 + 外来服务静默停止 + 注册删除）；`app.cpp` 单实例 bind 前互斥杀；`cmd_service` 接管分支（停对方→删注册→自家安装器重注册→启动）；NSIS 补杀「Clash Mini.exe」变体；单测 `tests/test_coexist.cpp`。总纲 §5.2 已改写为新方针。

---

## 二、△ 形态差异（8 项需关注，其余 ≈42 项为技术栈等价）

> 技术栈等价项（Webview 销毁↔D2D 释放、Tauri 事件↔UI Postbox、serde↔nlohmann 宽松解析、Tauri 网页手柄↔WM_NCHITTEST 等）行为一致，不逐条展开。以下 8 项存在**用户可感知差异或规范张力**：

| # | 条款 | Mini 行为 | Win 行为 | 差异 |
|---|---|---|---|---|
| △1 | §1.1 端口 33335 被无关程序占用 | `lib.rs:196-202` 弹「Startup Error」拒绝启动 | `app.cpp:125-141` probe≠200 仅 WARN 继续启动 | 极端场景 Mini 不启动、Win 启动。**2026-11-09 已裁决**：被**对方（Mini）**占用 → 杀对方后继续启动（互斥方针）；被无关程序占用仍需用户裁决 |
| △2 | §1.3 TUN+服务失败 | `lifecycle.rs:233-239` 自动回退系统代理+警告（网络保住） | `core_manager.cpp:538-541` 报错+开关回弹（处于手动态） | 失败后终态不同 |
| △3 | §1.8 服务 reinstall/repair | `service.rs:193-217`：reinstall = 卸载（失败仅告警，:196-199）→ 安装（失败即报错，:201-207）；repair 复用同一序列。后端另有 uninstall（:44-74），但其 UI 无调用入口 | `cmd_service.cpp` `service_reinstall()`：同"卸载 → 安装"序列，安装后校验注册、按需启动，失败均给 notice_message；`service_runner.cpp` `run_uninstaller()` 提供卸载能力；独立 `uninstall_service` 与 Mini 一致：后端保留能力（`service_uninstall()` + `run_uninstaller()`）、UI 不暴露 | 极端损坏场景 Win 已有自愈路径。**2026-11-09 二次裁决（D2=选 B）→ ✅ 已实施**，落点 `cmd_service.cpp` `service_reinstall()` / `service_runner.cpp` `run_uninstaller()`；**2026-11-09 三次裁决（uninstall「跟 Mini 一样」）→ ✅ 已实施**，落点 `cmd_service.cpp` `service_uninstall()`，防误接用例 `t35_uninstall_service_has_no_ui_path` |
| △4 | §2.8 过滤防抖自动择优 | 700ms 防抖 + 无失焦触发 | `node_list.cpp:458/461` **800ms** + 额外失焦触发（:475-480） | 触发时机晚 100ms + 多一个触发点 |
| △5 | §2.8 活跃节点轮换 | 基于 UI 当前过滤/排序子集 + 轮换气泡 | `main_window.cpp:738-750` 遍历全量 rows、无气泡 | 过滤激活时轮换落点可能不同 |
| △6 | §2.10 patch 期间 UI | 异步 IPC，仅显示「正在调整」 | `command_service.cpp:81-92` UI 线程同步 wait ≤30s | patch 触发内核重启时**主窗口冻结**最长 30s |
| △7 | §4.5 最小化唤醒 | unminimize 还原原位置 | `main_window.cpp:1623` 仅 SW_SHOW，最小化态停留 | 托盘唤醒最小化窗口需再点一次任务栏 |
| △8 | §5.3 自愈告警阈值 | 现行代码 3 次失败即归零，弹窗仅「3 败+API 不可达+重启失败」时出现 | `monitor.h:43` 按设计书字面 5 次弹窗 | Win 是设计书超集；是否向 Mini 现状收敛需裁决 |

另：§3.6 内核下载超时 Win 15s vs 设计书 10s、整包 300s 上限两侧均未实现（极端慢链 Win 可能无限等待，建议补）。

---

## 三、❓ 无法判定（16 项，需人工/运行时复核）

1. §1.1 安装器清除 Mini 历史进程（BUG-313）——installer 脚本本体未逐行核验（与 ❌11 相关）。
2. §1.6 内存基准 35-45MB / 轻量 ~10MB——运行时指标，需实测。
3. §1.5 DIR/RUL/PRX 缩写——设计书与 Mini en 包均未定位；两侧语言包逐字节一致。
4. §2.2 活跃栏 4 项/2 项指标——设计书描述与 Mini `active-node-card.tsx` 现实现对不上。
5. §2.2 级别下拉 95px、overflowWrap:anywhere——两侧均未定位字面量。
6. §2.6 隐匿态 GlowBorder——Mini 疑似组件已删；Win stealth 回调渲染分支未逐行核验。
7. §2.8 IME 组词期间 Enter 是否触发提交流——需中文输入法实测（Win TextField vs Mini composition 处理）。
8. §3.1 安装包 ~33.7MB——需 release 包实测。
9. §3.4 geodata-loader: memconservative——**两侧源码均 grep 不到**，疑设计书超前/过时；建议核对实际生成的运行时 yaml。
10. §3.6 降级链 Mini 侧行号未定位（Win 侧 Localhost→System→None 证据充分）；同版本前置过滤两侧均未定位——建议 mock 网络对照。
11. §7.3 订阅导入「本地直写规避节流」——两侧均未定位实现实体，疑文档超前。
12. BUG-080 相同版本前置比对、BUG-262 唤醒后节点列表优先全量刷新、BUG-299 Enter Dialog 级监听+isComposing 排除——Win 侧未定位（部分可能在输入组件/事件层）。
13. 子项待复核：BUG-296③（全局代理 MATCH,PROXY 语义）、BUG-310③（sysproxy 三元判定）、BUG-314②（调试日志弹窗 271px）、BUG-305①④。

---

## 四、bug list 13 条核实结论（全部 ✅ 源码已一致/已含修复）

| ID | 结论 | 关键证据（Mini ⇔ Win） |
|---|---|---|
| A1 测速动画 | ✅ 源码一致 | `proxy-item.tsx:209-213`(-2→BaseLoading) ⇔ `node_list.cpp:206-233` 三点脉冲自续帧 + `node_data.cpp:130-141` 批量置 -2 |
| A2 延迟列居中 | ✅ 源码一致 | `proxy-item.tsx:198-208`(65px+center) ⇔ `node_list.cpp:203/240` |
| B3 版权文本随高移动 | ✅ 已修 | ⇔ `settings_drawer.cpp:749-752` foot_y 跟随日志按钮（注释明言消除窗口高度依赖） |
| B4 4 开关丑 | ✅ 源码一致 | `base-switch.tsx` ⇔ `switch_w.cpp:93-296` 6 皮肤查表 + 0.85 缩放 |
| B5 日志按钮风格 | ✅ 已按 Mini | `theme-settings-card.tsx:434-449` ⇔ `settings_drawer.cpp:599-608/745-748` contained-primary 查表 |
| B6 导入/断开按钮+两行输入框 | ✅ 已对齐 | ⇔ `profile_import_card.cpp:262-274/250`（输入框 46px≈两行）；注：Win 固定两行 vs Mini 1-4 行自适应，良性微差 |
| B7 日志弹窗空白 | ✅ 已接通 | ⇔ `utility_modals.cpp:1039-1175` + `main_window.cpp:1201-1237`（回填 clashwin.log+内核日志+WS 实时流）；若新构建仍空白则查运行时 |
| B8 活跃/历史位置 | ✅ 一致 | `connections-panel.tsx:65-168` ⇔ `connections_panel.cpp:483-497`（左活跃右历史+计数） |
| B9 路径控制表空 | ✅ 非漏实现 | ⇔ `connections_panel.cpp:519-527` 虚拟列表+空态+`connection_data.cpp` 完整管道；运行时为空查数据流 |
| C10 TUN 静默失败 | ✅ 已有守卫链 | ⇔ `main_window.cpp:954-1055`（service 守卫+装服务+失败 error+回弹+5s TUN 守卫定时器）。**此前的根因分析行号（settings_drawer.cpp:1490 / main_window.cpp:746-748）系旧版代码，已失效，spec 作废** |
| D11 厚白边框 | ✅ 已含专项修复 | ⇔ `main_window.cpp:372-382`（DwmExtendFrameIntoClientArea MARGINS{1,1,1,1}+边框色 NONE，注释即标 D11）+ WM_ERASEBKGND return 1 |
| D12 模块不居中 | ✅ 单列已按裁决居中 | ⇔ `settings_drawer.cpp:1176-1178`（窄单列居中，注释明引用户裁决）；**宽窗双列模式仍与 Mini 同构（左列固定 x=16），若裁决要求宽模式也居中需明确** |
| D13 无信息/报错弹窗 | ✅ 根因已修 | 根因=SnackbarW 从未挂载；现 `main_window.cpp:424-429` 已 `snackbar_->attach(NoticeService)`，全部设置项通知齐全（:904-949 等） |

**→ 13 条全部指向「旧构建」：请重新编译后再逐条桌面复核。**

---

## 五、设计书过时点（Mini 源码为真相源，Win 已正确跟随，建议回写设计书）

1. §3.5 GOMEMLIMIT：设计书 96MiB/50/2，Mini 源码实际 **128MiB/100/2**（state.rs:39-41），Win 常量与真相源一致。
2. §3.3 控制密钥：设计书「废弃硬编码、强随机」，Mini 实际仍是固定串 `adapted-by-qiu-yuxiao`，Win 同值。
3. §2.9 「严禁动态更新托盘图标」：两侧实现均有按 TUN/sys 状态的图标更新，条文与源码双方不符。
4. §2.8 节点行高：设计书 20px，两侧实现均 22-26px。

---

## 六、需用户裁决 / 确认事项（2026-11-09 已全部裁决，落地状态标注）

1. **BUG-313③ 安装清理 vs 服务共存** → ✅已裁决=互斥共存方针，✅已实施（commit 264624b）。
2. **§5.3 告警阈值** → ✅已裁决=取 Mini 现行（3 次归零），✅已修复（代码已落地，待桌面复核）：`src/core/monitor.h` 删 `alert_fail_threshold=5`；`monitor.cpp::heal()` 改为「3 次失败即归零计数 → 仅当 API 不可达→重启内核→重启失败」才弹窗，API 可达仅记日志不重启不弹窗（对齐 monitor.rs:1594-1621，取消独立 5 次直接弹窗）。
3. **§1.8 服务 reinstall/repair 全禁** → ~~已裁决=接受（维持全禁，无自愈路径），无需改码。~~ **该裁决已被 2026-11-09 二次裁决取代**：用户选 D2=选 B（原话"Mini 可以那么 Win 应该也可以"），reinstall/repair 已放开，落地见 `cmd_service.cpp` `service_reinstall()`；独立 uninstall 已于 2026-11-09 三次裁决「跟 Mini 一样」后对齐——后端保留卸载能力（`service_uninstall()`），UI 不暴露（无界面/托盘/引导/命令面板入口），防误接用例 `t35_uninstall_service_has_no_ui_path`，不再存在「维持全禁」口径）。
4. **D12 居中范围** → ✅已裁决=窄单列居中（已实现）；宽窗双列维持与 Mini 同构不居中，无需改码。
5. **§2.6 1s 固定节拍** → ✅已裁决=Win 合规、设计书条文过时；勘误已入总纲 §5.2.1（Mini 侧设计书由用户自行修订）。

---

*审计执行：6 个并行只读审计分片（§1-2.6 / §2.7-3 / §4-5 / §6-7 / §8+附录 / bug_list 13 条），全程未修改任何文件。逐条 ✅ 明细见各分片报告（可向会话索取）。*
