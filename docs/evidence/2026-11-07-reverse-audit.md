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

## 四、已查实、**尚未修**（需按轻重排期，个案讨论）

| # | 缺口 | Mini 证据 | 影响 |
|---|---|---|---|
| A | 通知覆盖面：Mini 121 处 `showNotice`，Win 仅 10 处 | `grep -rn showNotice src/` = 121 | 大量操作无任何反馈（复制地址、测速未执行/失败、服务安装进度、帮助菜单等） |
| B | 通知右键复制文案 | `notice-manager.tsx:150-156,196-200` | 用户无法一键复制错误信息 |
| C | 通知多行折行 | MUI Alert message 自动换行 | Win 当前单行省略号，长错误被截断 |
| D | MUI 过渡动画（Dialog/Menu Fade 等） | `_theme.tsx` transitions | Win 全部瞬时出现（长期全局性偏差，需统一决策） |

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
