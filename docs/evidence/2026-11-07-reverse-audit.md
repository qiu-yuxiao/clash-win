# 反向审计台账（Mini 每一行 → Win 是否有对应表现）

- 日期：2026-11-07
- 参照源码：`ClashVerge` 工作区当前内容（Mini v3.2.2）
- 审计方向：**反向**——不再只问「Win 的数值来自哪一行」，而是问「Mini 的每一处源码表达的对象，Win 是否都有对应表现」
- 口径：显示名 `Clash Mini → Clash Win` 之外，语义 1:1；确实属于 Mini 自身多余代码的，逐条例外讨论

## 一、可量化覆盖（客观可复核）

| 维度 | Mini | Win | 结论 |
|---|---|---|---|
| Tauri 命令（`generate_handlers!` 全集，`src-tauri/src/lib.rs:120-165`） | 44 | 44 | **44/44 全有**，逐条 grep 命中（`src/cmd/*.cpp`） |
| Rust 后端规模 | 94 文件 / 23,308 行 | 模块级对应 | 无「整块缺失」模块 |
| 前端（ts/tsx） | 137 文件 / 27,892 行 | `src/ui/` 253 文件 / 48,011 行 | 结构对应见第二节 |
| 回归用例 | — | 566 条，0 失败 | `python3 scripts/run_tests_py.py` |

命令级复核方法：从 `lib.rs` 的 `generate_handler![...]` 抄出 44 个名字，逐个
`grep -r '"<name>"' src/cmd/*.cpp`，缺失数 0。

## 二、前端对象 → Win 对应物（逐类核对）

| Mini | Win 对应 | 状态 |
|---|---|---|
| `proxy/proxy-groups.tsx` | `scenes/node_list.*` + `scenes/node_data.*` | 有 |
| `proxy/proxy-head.tsx`（⚡/排序/搜索） | `node_list` 工具条 | 有 |
| `proxy/proxy-item.tsx` | `NodeRowWidget` | 有 |
| `layout/scroll-top-button.tsx` | `scene::ScrollTopButton` | **本轮补**（原缺失） |
| `layout/resize-handles.tsx`（8 向拉伸 10px） | `main_window` `WM_NCHITTEST` + `kResizeBorder=10` | 有 |
| `layout/notice-manager.tsx` | `ui/snackbar_w.*` | **本轮补挂载+重写**（原存在但从未挂载） |
| `layout/traffic-graph.tsx` / `home/enhanced-canvas-traffic-graph.tsx` | `ui/canvas_graph.*` + `scenes/traffic_pane` | 有 |
| `layout/window-controller.tsx` | `ui/title_bar.*` | 有 |
| `base/base-empty` / `base-loading` | `node_list` 空态（Inbox + No Proxies）/ 行内三点动画 | 有 |
| `base/base-search-box` | `ui/textfield.*`（含清空按钮） | 有 |
| `base/virtual-list.tsx` | `ui/scroll_list.*`（overscan 15） | 有 |
| `connection/connection-table` / `connection-detail` | `scenes/connections_view` / `connection_detail_dialog` | 有 |
| `pages/logs.tsx`（`log/log-item.tsx`） | `utility_modals` 日志查看器 + `text_viewer` | 有 |
| `pages/unlock.tsx` | `scenes/unlock_dialog` | 有 |
| `_layout/components/*`（13 个） | `scenes/quick_bar`、`settings_drawer`、`first_run_guide_dialog`、`add_url_dialog`、`profile_import_card`、`connections_panel` | 有 |
| `utils/mini-window.ts`（285/126 阈值 + 1px 容差） | `ui/breakpoints.h` | 有（阈值/容差逐条对照） |
| `utils/quick-routing.ts` | `core/quick_routing.*` | 有 |
| `services/notice-service.ts` | `events/notice.*` | 有（语义一致：3/5/8s、上限 50、push/hide/subscribe） |

## 三、本轮查实并已修的缺口

1. **`is_dummy_node` 漏判 DIRECT / 空串**（Mini `utils/node.ts:83,86`）
   → 首装裸配置 `PROXY.all=[DIRECT]` 时把 DIRECT 当节点渲染。
   修：`util/node.cpp` 补两条判定；`scene/node_data.cpp::load()` 按
   `calcuProxies` 同款 filter；`t06`/`t13` 各补断言。
2. **滚动置顶按钮整个缺失**（`scroll-top-button.tsx` + `proxy-groups.tsx:171-176,212-220`）
   修：新增 `scene::ScrollTopButton` —— 40×40 圆、bottom/right 20、底色
   light `rgba(0,0,0,.1)`/dark `rgba(255,255,255,.1)`、hover `.2`、
   `<Fade>` 225/195ms、阈值 `scrollTop>100`、点击回顶、淡出后点击穿透。
3. **通知条从未挂载**（`SnackbarW` 有实现、无任何 `add_child`）
   → `NoticeService::push` 的提示（检查更新/内核更新/导入结果/错误）全部静默。
   修：`main_window` 挂 `root_`（z=1500）+ 按 `notice-manager.tsx` 重写为
   MUI `Alert variant="filled"`（容器 20/20、gap 10、maxWidth 360；
   条目 radius 4、padding 6/16、icon 22、action 关闭按钮）；
   位置读 `verge.notice_position`（缺省 `top-right`）。
4. **版本位仍写 3.2.2** → 按长期裁决统一改 `0.1.0`（标题栏/诊断包/安装包名）。
5. **`EventBus::notice_message` 全仓零订阅者**（本轮最大缺口）
   → 后端所有「状态串 + 明细」（`import_sub_url::*`、`config_validate::*`、
   `install_service::*`）发出去没人接，用户侧表现为**「点导入没反应、切配置没反应、
   装服务没反应」**。
   修：新增 `ui/notice_handler.*` —— 1:1 复刻
   `pages/_layout/utils/notification-handlers.ts` 的 27 条 status 表，补 Win 侧
   自有的 `install_service::*` / `profile_switched*` / `update_subscription::*` /
   `profile_deleted::ok` / `copied` / `ipc_timeout`，共 **37 条规则**；
   文案走 `locales`（13 语言），语义按 `notice-service.ts:169-280`：
   `key`+明细 →「译文 + 空格 + 原文」，参数型 → `t(key, {param})`，无 key → 原文。
6. **校验错误状态无细分**（`validate.rs:531-583` 的 `notice_key`）
   → Win 一律 `config_validate::error`，用户看到的是通用文案。
   修：`core/validate` 新增 `ValidationNoticeTarget` + `notice_status()` +
   `handle_validation_notice()`（Invalid 细分 26 种、Busy 只记日志、Skipped 通用）；
   `save_profile_file` 按文件种类（脚本 / 合并 / YAML）选 target，与
   `save_profile.rs:110-140` 一致。
7. **`save_profile_file` 两处偏差**
   - 「内容未变」短路缺失（`save_profile.rs:46-56`）→ 每次保存都重写+重校验；
   - 判断「是否影响运行时」用 `current == item.file`（`current` 存的是 uid，永不相等）
     → **编辑当前配置后从不刷新运行时**。改为 `profile_affects_runtime()`
     （current 自身或其 merge/script/rules/proxies/groups 子项，见 `save_profile.rs:88-105`）。
8. **快捷规则弹窗只加第一条**（`add-url-dialog.tsx:54-72` 是批量）
   → Win 走单条接口，多行/逗号输入只生效一条，且成功/无变化都无提示。
   修：改用 `add_quick_routing_rules()` 批量；`count>0` → success「已添加 {{count}} 条」、
   `count==0` → info「没有新增规则」（原先只有内联红字，源码是 toast）。
9. **命令超时无任何反馈**（`cmds.ts:84-110` `withIpcTimeout`）
   → Win 只写日志；UI 卡住时用户毫无线索。
   修：`CommandService::invoke` 超时时按 label 5s 节流推 `ipc_timeout` 通知
   （原文 `[label] IPC call timed out after Nms`）。
10. **帮助菜单导出诊断包无反馈**（`help-menu-button.tsx:156-180`）
    → 修：常驻 info「正在生成诊断包…」→ success「诊断包已导出：{{path}}」。
11. **基础设置开关无反馈**（`use-minimal-settings.ts:71-83`）
    → Allow LAN / IPv6 改完静默。修：info「正在调整」→ success「已更新」/ error 原文。
12. **配置增删改切换无反馈**（`_layout.tsx:929-963`）
    → 切换 / 更新订阅（info→success）/ 删除 全部无声。已按源码补 4 条状态。
13. **通知条跨线程改控件树**（新增风险，顺手修）
    → `SnackbarW::refresh` 可能在后台线程被调（命令超时、更新检查）。
    修：`UiPostbox::mark_ui_thread()/on_ui_thread()`，非 UI 线程转投；
    未标记（测试）恒直接执行，不引入消息循环依赖。

## 四、已查实、**尚未修**（需按轻重排期，个案讨论）

> 2026-11-07 第二轮：A/C/D/F 已全部闭环（见第七节），B 已 key 级清零。
> 仅剩 E（MUI 过渡动画）为长期全局性偏差，需单独定策略。

| # | 缺口 | Mini 证据 | 影响 |
|---|---|---|---|
| E | MUI 过渡动画（Dialog/Menu Fade、Snackbar Grow 等） | `_theme.tsx` transitions | Win 全部瞬时出现（长期全局性偏差，需统一决策：Fade 只需 opacity 图层，Grow 需 scale 变换——`IRenderTarget` 目前只有平移 origin，无任意变换，须先补 `push_transform`） |

## 五、测试环境纪律（本轮踩坑）

- 开发期 `build\bin\Release\clashwin.exe` 是**便携模式**：配置在
  `<exe>\.config\io.github.clash-win.clash-win`（`.config\PORTABLE` 标记），
  不是 `%APPDATA%`。旧清理脚本只清 `%APPDATA%` → 假干净，实测表现为
  「首装引导不弹 + 节点列表非空 + 开机即有节点代理」。
- 已修 `scripts/reset_test_env.ps1`：覆盖四个便携根目录；目录被内核占用
  （TUN 模式下服务以 SYSTEM 起 `mini-mihomo` 锁 `cache.db`）时退化为逐项搬移。
- **首启自动接管系统代理**是 1:1 行为（`first_run_guide_dialog.cpp:482` ←
  Mini `_layout.tsx:398`，BUG-310），会把注册表代理写成 `127.0.0.1:10811`。
  若进程被强杀 → 10811 无监听 → 全系统断网。**测试必须从托盘退出**，
  异常退出后先跑清理脚本（内含关闭系统代理 + REFRESH 广播）。

## 六、验证记录（本轮实测）

| 项 | 结果 |
|---|---|
| 干净环境首启 | 首次引导弹窗弹出（STEP 1/2，「Clash Win へようこそ」） |
| 窗口尺寸 | 407×1020 物理 @144dpi = **271.3×680.0 逻辑**（Mini `resolve/window.rs:18-19` 271×680） |
| 标题栏 | `Clash Win Ver.0.1.0` |
| 节点列表 | 空态（无 DIRECT、无任何行） |
| UI 语言 | 日语 —— 本机 Culture=ja-JP，Mini `sys_locale::get_locale()` 出厂行为，非缺陷 |
| 回归 | 566 用例 0 失败 |

## 七、第二轮闭环（2026-11-07 续）

| 项 | 修复内容 | 源码锚点 |
|---|---|---|
| A | 延迟 Chip 可点单点测速：`-2` 显 spinner → `trigger_auto_select(select=false)` → 写回；≥500ms 加载动画；控制面失败不伪造 Timeout，恢复原值 + 三种提示 | `active-node-card.tsx:108-136`、`delay.ts:340-348` |
| B | 通知 key 级缺口清零（逐文件核对 Mini 100 处 `showNotice.*`）：<br>· 端口保存 3 条（portInUse / portSaved / 失败原文）<br>· 解锁检测 15s 超时 → detectionTimeout<br>· TUN 自动关闭 autoDisabled / autoDisableFailed（此前 Win 无该行为）<br>· 内核就绪超时 clashNotReady<br>· 复制订阅链接 profileLinkCopied / copyFailed<br>· `clientInstallCompleted` 核实**不可达**（`updater.rs:843-854` 先 `exit(0)`） | 见各条注释 |
| C | 通知条右键 → 复制该条全文 + `copySuccess` 1000ms（关闭按钮上右键同样冒泡复制，不关闭） | `notice-manager.tsx:150-156,178-200` |
| D | 通知长文案折行：条目高度按折行行数动态算（行高 20.02 = 14×1.43，单行仍 48）；图标/action 随高度居中；bottom 锚点按各自实际高度堆叠 | `notice-manager.tsx:178-212`（MUI Alert message `minWidth:0;overflow:auto`） |
| F | 复制订阅链接补提示（菜单项本已存在，缺的是 toast） | `_layout.tsx:987-998,1833-1839` |

## 八、第二轮顺带修掉的实现缺陷

1. **Mixed Port 保存从不写内核**：原实现只 `patch_verge_config{verge_mixed_port}`，
   内核 `mixed-port` 不变 —— 端口「改了不生效」。改为占用检查
   （`is_port_in_use`）+ 双写（`patch_clash_config{mixed-port}` 与
   `patch_verge_config{verge_mixed_port}`）+ 失败/占用回滚输入框，
   ← `use-minimal-settings.ts:87-107`。
2. **解锁检测在 worker 线程直接改控件状态**（`items_` / `is_checking_` +
   `invalidate()`）→ 与绘制竞争。改为结果经 `UiPostbox` 投递回 UI 线程。
3. **数据竞争之外的行为缺口**：`UnlockDialog::start_check` 无超时、无失败提示，
   `check_all` 抛错即静默。补 15s（`UNLOCK_TIMEOUT_MS`）+ 异常捕获 +
   `detectionTimeout` 提示。
