# Clash Win Bug 跟踪台账（2026-11-07 起，攒批，待授权发版）

> **调试第一铁律（最高优先级）**：用户指出问题 → 先核实是否与 Clash Mini 当前版本完全一致（锚点 2026-11-10 起为 **v3.2.3**，此前登记条目按 v3.2.2 时点核实）。
> - 不一致 → 照 Mini 源码 `file:line` 改，不自行设计。
> - 一致但用户仍判错 → 提出来商量，不擅自改。
> Mini 源码只读工作区：`C:\Users\sun_y\Documents\AntiGravity_Projects\ClashVerge`（v3.2.3）。

## ★ Bug 状态规矩（2026-11-09 用户指定，覆盖本台账每一条）

每条 bug 必须且只能处于以下四个状态之一：

| 状态 | 含义 | 进入条件 |
|---|---|---|
| **未处理** | 已登记在案，尚未核实、尚未动手 | 用户报障登记时 |
| **处理中** | 正在对照 Mini 核实或正在修复 | 开始核实/修复时 |
| **已处理** | 源码层面已修复或已核实与 Mini 一致（含本地提交），**但未经用户桌面确认** | 附上证据（Mini/Win `file:line` 或 commit hash）后标 |
| **用户已确认** | 用户在重新编译后的桌面环境**亲自复核通过**（终态） | 仅用户明示通过后可标，AI 不得代标 |

规则：
1. 标「已处理」必须带证据，不允许无证据标完。
2. 「用户已确认」是唯一终态；发版前所有条目应至少达到「已处理」，攒批发版时逐条问用户确认升级。
3. 「已处理」后用户复现（新构建下）→ 退回「处理中」并查运行时数据流。

## 状态图例
- ⬜ 未处理 ｜ 🔧 处理中 ｜ ✅ 已处理（待用户确认）｜ 🟢 用户已确认 ｜ ⚠️ 需与用户商量

---

## 当前状态汇总（2026-11-09 审计后）

> **2026-11-09 全量审计结论**：13 条全部核实为「源码已一致 / 已含专项修复」，病灶代码在现行源码中不存在——强烈指向用户桌面运行旧构建（v0.1.0 基线之前的产物）。全部标 **✅ 已处理**；待用户重新编译后桌面逐条复核，复核一条升一条 🟢。逐条证据详见 [`evidence/2026-11-09-win-mini-diff.md`](evidence/2026-11-09-win-mini-diff.md) 第四节。

| ID | 模块 | 一句话 | 状态 | 证据（Win 侧） |
|----|------|--------|------|----------------|
| A1 | 节点列表 | 全节点测速无动画 | ✅ 已处理 | `node_list.cpp:206-233` 三点脉冲自续帧 + `node_data.cpp:130-141` 批量置 -2 ⇔ Mini `proxy-item.tsx:209-213` |
| A2 | 节点列表 | 延迟列未居中 | ✅ 已处理 | `node_list.cpp:203/240` 65px 列 AlignH::Center ⇔ Mini `proxy-item.tsx:198-208` |
| B3 | 设置-主题 | 著作权文本随高移动 | ✅ 已处理 | `settings_drawer.cpp:749-752` foot_y 跟随日志按钮（注释明言消除窗口高度依赖） |
| B4 | 设置-基础 | 4 开关丑，需统一滑块风 | ✅ 已处理 | `switch_w.cpp:93-296` 6 皮肤查表 + 0.85 缩放 ⇔ Mini `base-switch.tsx:21-128` |
| B5 | 设置-主题 | 系统日志按钮风格不对 | ✅ 已处理 | `settings_drawer.cpp:599-608/745-748` contained-primary 皮肤查表 ⇔ Mini `theme-settings-card.tsx:434-449` |
| B6 | 设置-订阅 | 导入/断开按钮 + 输入框两行高 | ✅ 已处理 | `profile_import_card.cpp:262-274/250`（输入框 46px≈两行）；良性微差：Win 固定两行 vs Mini 1-4 行自适应 |
| B7 | 设置-主题 | 系统日志弹窗空白 | ✅ 已处理 | `utility_modals.cpp:1039-1175` + `main_window.cpp:1201-1237`（log 回填+内核日志+WS 实时流）；新构建仍空白才查运行时 |
| B8 | 设置-路径 | 活跃/历史位置不对 | ✅ 已处理 | `connections_panel.cpp:483-497` 左活跃右历史+计数 ⇔ Mini `connections-panel.tsx:65-168` |
| B9 | 设置-路径 | 下方表空白（疑漏） | ✅ 已处理 | `connections_panel.cpp:519-527` 虚拟列表+空态 + `connection_data.cpp` 完整数据管道，非漏实现；运行时为空查数据流 |
| C10 | 内核 | Service 缺时切 TUN 无报错 | ✅ 已处理 | `main_window.cpp:954-1055` service 守卫+装服务+失败 error+回弹 + 5s TUN 守卫定时器 ⇔ Mini `_layout.tsx:1042-1114`。注：2026-11-07 旧根因分析行号系旧版代码，spec 作废 |
| D11 | 窗口 | 特定情况厚白边框 | ✅ 已处理 | `main_window.cpp:372-382` DwmExtendFrameIntoClientArea MARGINS{1,1,1,1}+边框 NONE（注释即标 D11）+ WM_ERASEBKGND return 1 |
| D12 | 设置页 | 4 模块不居中 | ✅ 已处理（附确认项） | `settings_drawer.cpp:1176-1178` 窄单列已按用户裁决居中；⚠️ 宽窗双列仍与 Mini 同构（左列固定 x=16），宽模式是否也居中待用户明确 |
| D13 | 设置页 | 所有调整无信息/报错弹窗 | ✅ 已处理 | 根因=SnackbarW 从未挂载；现 `main_window.cpp:424-429` 已 attach(NoticeService)，全键通知齐备（:904-949） |

**升级到 🟢 的操作**：用户重编译 → 桌面逐条复核 → 每条报「通过」后由我把该条改 🟢 用户已确认。

---

## 2026-11-11 v0.2.6 重启自启后新发现（对照 Mini v3.2.3）

> 用户报障要点：「关机前对代理节点设定了过滤条件，随机重启自动启动后，过滤条件消失了。窗口尺寸也变成启动默认值了，当然了，窗口的位置也没有记忆。基础设置模块里的那 4 个小开关，操作时弹出的提示框内容不对……行为也不对，点下去之后没有立刻反应位移……我不一一说了，你要举一反三，好好查一下。」
>
> **调试第一铁律核实结论：全部条目与 Mini v3.2.3 不一致 → 照 Mini 改，无「一致但被判错」需商量的条目。**
> 状态：✅ **已处理**（2026-11-11 commit `563111f` 源码修复完毕；回归 678 cases, 0 failed, EXIT=0；**尚未经用户在重新构建的桌面环境确认**，确认后逐条升 🟢）。

### 运行时事实（先排除「保存链路坏掉」）

| 文件 | 实测内容 | 结论 |
|---|---|---|
| `%APPDATA%\io.github.clash-win.clash-win\proxy_head_state.json` | `{"RRJHYS7EEXU3":{"PROXY":{"filterText":"日本","open":false,"showType":true,"sortType":1}}}` | **写盘正常**，缺的是「启动读回 UI」 |
| `%APPDATA%\io.github.clash-win.clash-win\window_state.json` | `{"height":615,"width":271,"x":1568,"y":7}` | **写盘正常**，另有安装器删除 + 恢复时序问题 |
| `logs\clashwin.log` 13:20:34 | `config.yaml` / `verge.yaml` / `profiles.yaml` 全部 `file not found`；`create_hwnd want=407x1020 got=407x1020 dpi=144` | 该次为全新数据目录初始化；文件时间戳显示 yaml 于 13:22~13:25 重建，非整目录被删 |
| `logs\clashwin.log` 完整时间线 | `13:20:34` 首次启动 → `13:29:27` `WM_QUERYENDSESSION`/`WM_ENDSESSION` + 7 步优雅退出（`Lifecycle::quit ... exit code: 0`）→ `14:00:45` 重启后第二次启动 `App::init 首实例` → `14:00:45.892` `[RULERFIX] create_hwnd want=407x1020 got=407x1020 dpi=144` → `14:00:46.299` `MainWindow: 创建完成（无框 271x680 ...）` | **坐实用户报障的就是这次重启**：14:00 启动时窗口仍以默认 271×680（物理 407×1020 @144 DPI）建出；`window_state.json` 当前时间戳为 14:16:35（重启**之后**才生成），即开机时磁盘上并无有效几何记录 → 与 E15「安装器删 `window_state.json`」完全吻合。14:00 启动日志中已无 yaml `file not found`，说明配置目录本身保留完好，**排除整目录删除**。 |

### 本批条目

| ID | 现象 | 根因（Win `file:line`） | Mini 依据（`file:line`） | 状态 |
|---|---|---|---|---|
| E14 | 过滤条件重启消失 | `cmd_network.cpp` 注册 `get_proxy_head_state` / `save_proxy_head_state` 但全仓无 UI 调用方；`node_list.cpp:489-502` 搜索框只设 placeholder；`node_list.h:185` `sort_type_ = 1` 硬编码 | `use-head-state.ts:68-99` 挂载即 `invoke` → `dispatch replace`；`proxy-head.tsx:212` 受控 `value={filterText}` | ✅ 已处理 `563111f` |
| E15 | 窗口尺寸回默认 271×680 | `installer/clashwin.nsi:730`（安装段）+ `:896`（卸载段）**无条件** `Delete "$APPDATA\${BUNDLEID}\window_state.json"`，把当前在用的状态文件删了 | `installer.nsi:921-924` / `:1100-1104` **只删两个历史连字符文件** `window-state.json` / `.window-state.json`；当前在用的是下划线 `window_state.json`（`constants.rs:48`） | ✅ 已处理 `563111f` |
| E16 | 窗口位置不记忆 | 同 E15（位置与尺寸同一文件）；另 `window_state.cpp:53-78` 脱屏判定用物理显示器矩形直接与逻辑 x/y 比较，`restore():259` 误判会把 x/y 置空强制居中 | `window_manager.rs:246-267` monitor 坐标/尺寸各除以 `scale_factor()` 换成逻辑坐标再比较 | ✅ 已处理 `563111f` |
| E17 | 4 开关提示框内容不对 | `main_window.cpp:952` 对 `enable_auto_launch` / `enable_silent_start` 成功仍 push「已更新」（注释写「保持静默」却自相矛盾）；`:939` `rule_fallback` 成功同样误弹 | `basic-settings-card.tsx:136,185` 裸 `patchVerge` 成功静默；`use-verge.ts:47-71` 仅失败 `showNotice.error`；`_layout.tsx:398-424` `handleRuleFallbackChange` 成功无 success | ✅ 已处理 `563111f` |
| E18 | 点下去没立刻位移 | `switch_w.cpp:14-21` 已即时置位并 `invalidate()`，但 `on_change` 同步走 `svc.invoke` → `command_service.cpp:79-92` 在 UI 线程 `fut.wait_for(≤30s)` 阻塞消息循环，重绘排不上队 | `use-verge.ts:48-51` 乐观更新 queryClient 缓存（0ms 切换）+ 异步 await IPC，UI 线程不阻塞 | ✅ 已处理 `563111f` |
| E19 | 举一反三：Allow LAN / IPv6 开机恒显示关 | `main_window.cpp:1530-1533` `sync_basic(..., false, false, ...)` 硬编码；而 `resync_settings_ui():1564-1571` 已有正确取法 | `basic-settings-card.tsx:164,200` checked 由 react-query 拉真值，无硬编码窗口 | ✅ 已处理 `563111f` |
| E20 | 举一反三：激活校准拿不到过滤词 | `cmd_profile.cpp:565` 用 `st.value("filter_text", ...)` 读**顶层键**，真实结构是 `{uid:{PROXY:{filterText:...}}}` → 恒空 | `use-head-state.ts` 状态按 `uid/group` 索引；`use-profiles` `activateSelected` 读当前组的过滤词 | ✅ 已处理 `563111f` |
| E21 | 举一反三：几何恢复后立刻回写污染 | `apply_geometry_restore()`（`:1634-1667`）置 `restore_done_=true` 后，`create()` 的 `:255` / `:264` 两次显式 `on_client_resized` → `persist_geometry()` 立刻回写；0.2.6 新增 `defer_render`（静默启动跳过 `init_render`）下 `dpi_scale()` 可能未换挡 | `window_manager.rs` 在窗口构建**前**一次性注入几何，不存在「恢复后立刻回写」 | ✅ 已处理 `563111f` |
| E22 | 举一反三：过滤状态回填会误触发自动测速 | `textfield.cpp:64-70` `set_text()` 无条件触发 `on_text_changed`；`node_list.cpp:503-523` 该回调启动 700ms 去抖 → `trigger_auto_select_visible()` | `base-search-box.tsx:95-97` `useEffect(()=>emitSearch(text),[text])` 走 `onSearch` 通道，**不经过 `onChangeText`、不启动 700ms 定时器** | ✅ 已处理 `563111f` |
| E23 | 举一反三：过滤词落盘被小写化 | `node_list.cpp:621` `gs.filter_text = lower`、`:643` `gs.filter_text = filter_`，落盘的是小写态；修好 E14 回填后，英文过滤词 `HK` 会显示成 `hk` | `use-head-state.ts` / `proxy-head.tsx:212` 的 `filterText` 保存**用户输入的原始文本** | ✅ 已处理 `563111f` |
| E24 | 举一反三：`cmd_profile` 另一处已用对、`:562` 用错 | `cmd_profile.cpp:138-148` 的 `ah.filter_text` lambda 已是正确读法 `get_group_state(st, uid, "PROXY")`；`:562-566` 却是 `st.value("filter_text", ...)` 顶层读 | Mini `use-head-state.ts` 按 `uid/group` 两级索引 | ✅ 已处理 `563111f` |

| E25 | 举一反三：D13 一刀切通知残留不只 4 个开关 | `on_patch_verge` 被 `settings_drawer.cpp:996/1552/1553/1563/1569/1572/1575/1578/1639/1652` 共 10 处调用，「其余 verge 键」分支 `:951-952` 对全部键成功都 push「已更新」；`takeover` 路径（`:1551-1554`）因此还会与 `main_window.cpp:963-1027` 的专门文案**重复弹通知** | Mini 对 `theme_mode`（`theme-settings-card.tsx:198/240/282` 裸 patchVerge）、`language`（`use-i18n.ts:32-38` catch 仅 `console.error`）、皮肤（`_layout.tsx:1693` 仅 localStorage）成功时**全部静默** | ✅ 已处理 `563111f` |

> **E23 的实现约束（交叉核对全部消费者后确认，防改出新 bug）**：落盘改为原文大小写后，`cmd_profile.cpp:565` 传给 `AutoSelect::calibrate_selected()` 的入参**必须仍是小写**——`auto_select.cpp:78-90` 形参即 `filter_text_lower`，函数内部不再小写化，只做 `to_lower(*selected).find(filter_text_lower)`。可复用 `core::extract_filter_text(state, uid)`（`core_manager.cpp:343-362`，自带 trim+lowercase）。另外两处消费者已核实安全、无需改动：`cmd_profile.cpp:137-148` 的 hook 被 `auto_select.cpp:146/202` 用 `to_lower()` 包了一层；`core_manager.cpp:364-373` 的 `read_filter_text_lower()` 经 `extract_filter_text()` 自带小写化。

### 修复落地（commit `563111f`，2026-11-11）

| 条目 | 修复位置（file:line 以 `563111f` 为准） | 要点 |
|---|---|---|
| E14 | `src/ui/scenes/node_list.cpp` `restore_head_state()` + `rebuild_visible()` 懒加载兜底；`node_list.h` `head_state_loaded_` | bind 挂载即读回 `proxy_head_state.json` 到 UI（一次性守卫防递归/防 profile 切换覆盖）；`sort_type_` clamp 0~2；`last_auto_select_filter_` 预置防误触发 |
| E15 | `installer/clashwin.nsi` 安装段+卸载段 | 删除对 `window_state.json` 的无条件 `Delete`，只保留两个历史连字符文件清理（← Mini installer.nsi:921-924/1100-1104） |
| E16 | `src/config/window_state.cpp` `is_position_on_screen()` + `monitor_scale()` | EnumDisplayMonitors 物理矩形按显示器 DPI 换算逻辑坐标（shcore 动态解析，回退链 GetDpiForMonitor→GetDpiForSystem→1.0；scale=1.0 时逐值与旧行为一致，测试契约不变） |
| E17/E25 | `src/ui/main_window.cpp` `on_patch_verge` | 成功通知收敛：仅 Allow LAN/IPv6 push「已更新」，其余全静默；takeover 重复通知消除 |
| E18 | 同上 | 全键异步化：UI 线程乐观置位 + wait_id，`std::thread` 后台 `svc.invoke`，收尾 `UiPostbox::post` 回 UI 线程（照 `on_takeover_change` 范式），失败仍 error + `resync_settings_ui()` 回滚 |
| E19 | `main_window.cpp` build_scene 启动回填 | `sync_basic` 的 allow_lan/ipv6 改读 `get_client_info()` 真值 |
| E20/E24 | `src/cmd/cmd_profile.cpp` 激活校准段 | `get_group_state(st,uid,"PROXY")` 取过滤词 + trim+tolower 后再传 `calibrate_selected`（满足 E23 约束） |
| E21 | `src/ui/main_window.cpp` `create()` + `on_client_resized(...,persist)` | 启动期两次显式重排传 `persist=false`，不回写 window_state.json |
| E22 | `src/ui/textfield.*` `set_text_silent()` + restore 路径 | 程序化回填不触发 `on_text_changed`，不误启 700ms 去抖自动测速 |
| E23 | `src/ui/scenes/node_list.cpp` `set_filter()`/`toggle_sort()` | 落盘改存用户输入原文（trim 后）；toggle_sort 只改 sort_type 不覆盖 filter_text；内存匹配口径仍 trim+tolower |

**同 commit 附带的测试修正**（非 E 条目）：
- `tests/test_t23_quit_recovery.cpp:512/535`：quit/restart 等待窗 300ms→1s。根因：全量 630+ 用例跑完后系统负载下 300ms 偶发不够（单跑必过、全量偶 FAIL 的 flaky），不改被测契约。
- `tests/test_t35_release_readiness.cpp`：`download_url.empty()`/`signature.empty()` 两条断言删除。根因：断言写死在「发布占位清单」状态上——CI 发布成功后 `458b53a` 自动回填真实产物链接，断言必然翻红（16:18 重编 exe 才暴露，与本批 E 修复无关）；改为「若有 url 必须指 `qiu-yuxiao/clash-win`」保住用例本意。

**回归**：`python3 scripts/run_tests_py.py` → 678 cases, 0 failed, EXIT=0。

### 已排除的岔路（否定性结论，防止后续重复排查）

- **不是换配置目录**：`kAppId` 在 `47fead1..HEAD` 期间未变，对 `dirs.h` 零改动；排除「0.2.6 换了 home 路径导致状态全丢」。
- **不是升级安装清空整个 AppData**：`clashwin.nsi:954-965` 的 `RMDir /r "$APPDATA\${BUNDLEID}"` 被 `${AndIf} $UpdateMode <> 1` 守住，更新模式不执行；该设计正确，**不改**。
- **`home_cards` / `collapse_navbar` 是 Mini v3.2.3 死字段**（仅注释/常量，无消费者），属历史残留，**不实现**；`clash-mini.yaml` 是派生文件，不算 UI 状态。
- **Mini 4 个开关本来就没有 Tooltip**：Win 侧无 Tooltip 是 1:1 对齐，**不要补**（补了反而分叉）；中文标签「开机自启」「静默启动」、`Allow LAN` / `Allow IPv6` / `Mixed Port`、提示文案「正在调整，请稍候…」/「已更新」均与 Mini 一致，**不改文案**。
- **`showType` / `open` 字段**：Mini 单组扁平列表无分组头，Win 存了但无消费者，不构成用户可见差异，**本批不扩散**。
- **不加 300ms 过渡动画**：Win `set_checked()` 是即时落位，比 Mini 的 CSS transition 更「立刻」；用户抱怨的是阻塞导致的完全不动。加动画会破坏现有 `anim()` 测试断言并在轻量模式（不跑帧泵）卡在中间态，**本批不改 `switch_w.*`**。

### 已排查但本批不扩散的遗留（⬜ 未处理，待用户桌面复核后再定）

- Mini `localStorage` 另有 `proxy-scroll-positions`、`clash-mini-last-enhanced-uid`、`clash-mini-depth-factor`、`clash-mini-vibrancy-factor`、`clash-mini-original-val2` 等 UI 状态；Win 侧已有 `clashwin-skin.json` / `clashwin-guide.json` / `last_non_large_size.json`，但滚动位置与 `last-enhanced-uid` 未见对应持久化。属同类「状态持久化」缺口，但需先确认是否为用户可见行为，避免一次性扩散改动引出新 bug。
- `src/ui/scroll_list.cpp` 只有内存态 `offset_`，未见 `proxy-scroll-positions` 对应落盘。

---

## 原始登记（现象存档，便于复核对照）

### A1 全节点测速无「正在测速」动画
- 现象：点击闪电小图标全节点测速时，测试过程中延迟数据列没有表示正在测速的动画。

### A2 延迟数据列未居中（疑似靠右）
- 现象：延迟数据列应居中，当前疑似靠右。

### B3 著作权文本随窗口高度移动
- 现象：主题设置模块下方著作权内容随窗口高度变动移动；Mini 不应如此。

### B4 基础设置 4 个小开关造型丑，需与主题设置滑块统一

### B5 主题设置「系统调试运行日志」按钮风格不对（应同流量接管模块）

### B6 订阅与机场配置：导入/断开按钮风格不对；输入框应两行高

### B7 「系统调试运行日志」按钮弹窗空白

### B8 路径控制「活跃 / 历史」位置关系不对

### B9 路径控制下方表为空（疑似漏实现）

### C10 Service 不存在/停止时切 TUN 模式无报错且实际未生效

### D11 窗口出现一圈厚白边框（特定情况）
- 截图：`C:\Users\sun_y\.workbuddy\clipboard-images\clipboard-2026-11-07T16-09-34-923Z-51684b81.png`

### D12 设置页 4 模块与窗口不居中（左小右大）
- ★ 用户预先裁决（优先于铁律）：即使 Mini 也不居中，Win 一律改成居中。

### D13 设置页所有调整均无信息/报错弹窗
