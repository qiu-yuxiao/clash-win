# ClashVerge（Clash Mini）前端全量普查报告（外观与交互真相）

> 侦查员 scout-frontend · 2026-10-24 · 参考源只读：C:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge
> 结论：这不是上游 Clash Verge Rev 的多页面结构，而是 **Clash Mini 定制版**——只有一个主窗口、两个路由，主内容直接由 `_layout.tsx` 渲染（无独立 Proxies/Profiles/Settings 页面，全部收纳进主界面+设置抽屉）。

## 1. 目录树普查（src/ 共 230 个文件）

```
src/
├── main.tsx                       # 入口：bootstrap → preloadAppData → render
├── index.html                     # 根 html（含 #initial-loading-overlay 初始加载遮罩、CSP nonce 读取逻辑在 main.tsx）
├── constants.ts                   # 窗口常量【唯一事实来源】MIN_WINDOW 271×126 / MAX_WINDOW 640×860 / 断点 285(宽)、126(极小窗高)
├── pages/
│   ├── _routers.tsx               # createBrowserRouter：2 条路由
│   ├── _theme.tsx                 # 亮/暗两套主题色默认值（iOS 风格色板）
│   ├── _layout.tsx                # ★ 主窗口骨架（1985 行，核心文件）
│   │   ├── components/ (12 个)     # active-node-card / add-url-dialog / basic-settings-card / connections-panel /
│   │   │                           # first-run-guide-dialog / help-menu-button / layout-dialogs / mini-traffic-panel /
│   │   │                           # profile-import-card / routing-preference-card / takeover-mode-card / theme-settings-card
│   │   ├── hooks/ (10 个)          # use-client-update / use-core-update / use-custom-theme / use-import-context-menu /
│   │   │                           # use-layout-events / use-loading-overlay / use-minimal-settings / use-nav-menu-order /
│   │   │                           # use-skin-controls + index.ts
│   │   ├── types.ts, utils/ (4 个) # initial-loading-overlay / notification-handlers / profile-coordination / style-helpers
│   ├── logs.tsx                   # 日志页（仅由 LayoutDialogs 弹窗内复用其逻辑组件，路由已不直接挂载——未确认是否死代码）
│   └── unlock.tsx                 # 流媒体解锁检测页（路由 /unlock）
├── components/
│   ├── base/ (15)                 # 通用基件：BaseDialog/Empty/ErrorBoundary/Fieldset/Loading/Page/SearchBox/
│   │                              # SplitChipEditor/StyledSelect/StyledTextField/Switch/TooltipIcon/MonacoEditor/VirtualList
│   ├── proxy/ (7)                 # proxy-groups(节点列表+虚拟滚动)/proxy-head(搜索排序工具条)/proxy-item(单节点)/
│   │                              # proxy-render/use-filter-sort/use-head-state/use-render-list
│   ├── connection/ (2)            # connection-table(@tanstack/react-table)/connection-detail(Popover 详情)
│   ├── layout/ (5)                # traffic-graph(Canvas 流量图)/window-controller(标题栏三键)/
│   │                              # notice-manager(Snackbar 通知)/resize-handles(自绘窗口拖拽边)/scroll-top-button
│   ├── home/ (1)                  # enhanced-canvas-traffic-graph(Canvas 增强流量图，含 Worker 数据)
│   └── log/ (1)                   # log-item
├── hooks/ (21)                    # use-clash/use-verge/use-profiles/use-connection-data/use-traffic-data/
│   │                              # use-traffic-monitor(Web Worker 采样)/traffic.worker.ts/use-mihomo-ws-subscription/
│   │                              # use-listen/use-i18n/use-icon-cache/use-clash-log/use-log-data/use-proxy-delay-state/
│   │                              # use-proxy-selection/use-service-installer/use-system-state/use-visibility/use-window
├── providers/                     # app-data-context.ts + app-data-provider.tsx（7 个 Context）、window/(WindowProvider)
├── services/                      # cmds.ts(IPC 封装)/mihomo-api.ts(插件 API 超时包装)/delay.ts(DelayManager)/
│   │                              # i18n.ts/notice-service.ts/preload.ts/query-client.ts/monaco.ts
├── locales/                       # 13 语言 × 11 文件
├── types/                         # clash/connection/profile/verge/traffic + generated/i18n-keys、i18n-resources
├── utils/ (17)                    # button-styles.ts(★皮肤系统核心)/mini-window/node/parse-traffic/profile-items/
│   │                              # resize-geometry/search-matcher/traffic-sampler/window-resizing/debounce/debug/
│   │                              # disable-webview-shortcuts/error-reporter/get-system/quick-routing/yaml.worker
├── assets/                        # image/app-icon.png、styles/{index,layout,page,font}.scss
└── polyfills/                     # matchMedia/WeakRef/RegExp（legacy 插件注入）
```

依赖（package.json）：React 19.2 + MUI v9 + @emotion + @tanstack/react-query v5 + @tanstack/react-table/virtual + react-router v7 + i18next v26 + tauri-api 2.11（插件：clipboard/dialog/fs/http/process/shell/updater）+ 自研 `tauri-plugin-mihomo-api`（link: crates/tauri-plugin-mihomo）+ monaco + dayjs + foxact + ahooks。构建：Vite 8，root=src，alias `@`→src，legacy targets edge>=109，无 sourcemap。

## 2. 页面与主窗口布局（_layout.tsx）

路由（_routers.tsx:8-35）：仅两条 — `/`（Home，Layout 直接渲染内容）和 `/unlock`（UnlockPage，Layout 内 `<Outlet/>` 渲染）。

主窗口垂直结构（_layout.tsx:1219-1783）：
1. `<ResizeHandles/>` — 自绘窗口边缘拖拽（覆盖系统 resize，绕开无边框窗口问题）
2. 自定义标题栏（30px，decorated 恒 false）：AppIcon 16px + "Clash Mini Ver.{version}" + 右侧 WindowControls（最小化/大尺寸/关闭）；极小窗闲置 10 秒后整体隐藏（点击恢复）
3. `layout-content`（高 = 100vh−30px）分上下两栏：
   - **上栏（80%+30px）**：右上角关闭设置按钮（drawerOpen 时）或 顶部 30px 快捷条（ActiveNodeStatusCard 当前节点卡 + 置顶图钉 + 设置齿轮）；下方 `ProxyGroups` 节点列表（isMiniStatus 时卸载）；设置抽屉为**绝对定位全屏覆盖层**（theme-panel, zIndex 100，非 MUI Drawer）——左列 240px 固定宽：ProfileImportCard → (TakeoverModeCard+RoutingPreferenceCard 同卡片) → BasicSettingsCard → ThemeSettingsCard；右侧自适应 ConnectionsPanel（宽≤285 时不挂载）；底部 Language 下拉 + 6 格皮肤选择条（高度≤830px 时隐藏）
   - **下栏（固定 135px；窄窗 100px）**：MiniTrafficPanel（流量图+上传/下载速率，Canvas）
4. 弹窗群：ConnectionDetail(Popover)、Profile 右键菜单、导入框右键菜单、LayoutDialogs（编辑订阅/客户端更新/内核更新/日志 4 弹窗）、FirstRunGuideDialog

首次启动引导（_layout.tsx:341-355 + first-run-guide-dialog）：无任何 remote/local 真实订阅且 localStorage `clash-mini-first-run-completed` ≠ 'true' 时，延迟 500ms 弹出两步向导（input→ready 轮询就绪，20s 超时降级）；"稍后"则打开设置抽屉。

窗口状态机（WindowProvider+constants）：默认 271×680；宽≤285 → isMinimalWidth（窄窗 1 列布局）；宽≤285 且高≤126 → isMiniStatus（极小窗：DOM 卸载+数据精简）；640×860 = "大尺寸模式"（自定义 toggleMaximize，OS maximize 被禁用），尺寸持久化于 localStorage `last-non-large-size`。

## 3. 组件清单→UI 区域映射

- `components/base/*`：全局通用件（弹窗/空态/错误边界/搜索框/开关/Monaco/虚拟列表）
- `components/proxy/`：主窗口中部节点列表。proxy-groups 用 @tanstack/react-virtual 虚拟滚动（1000+ 节点优化）；proxy-head 过滤/排序/延迟测试工具条；proxy-item 单节点卡片（延迟色/图标）；use-render-list 构建渲染项（Mini 单 PROXY 组架构）
- `components/connection/`：设置抽屉右侧连接表。connection-table 用 @tanstack/react-table（列管理/排序/右键菜单：复制/断开）；connection-detail 单连接 Popover 详情
- `components/layout/`：traffic-graph — Canvas 折线流量图（10fps 动画，maxPoint 30）；window-controller — 标题栏右端三键（min/大尺寸/close，28×28）；notice-manager — 全局 Snackbar 通知（useSyncExternalStore 订阅 notice-service）；resize-handles — 8 方向自绘拖拽边（computeResizeGeometry 钳制到 271×126~640×860）；scroll-top-button
- `components/home/enhanced-canvas-traffic-graph`：增强 Canvas 流量图（Web Worker 采样数据，10 分钟原始+60 分钟压缩数据）
- `components/log/log-item`：日志行（LayoutDialogs 日志弹窗用）
- `pages/_layout/components/`：active-node-card（顶栏当前节点状态卡：名称/协议/延迟/信号图标/服务器地址，点击弹组列表）、profile-import-card（订阅导入+列表+右键菜单）、takeover-mode-card（手动/系统代理/TUN 三段切换）、routing-preference-card（direct/proxy/addurl 三段）、basic-settings-card（混合端口/allow-lan/ipv6/开机自启）、theme-settings-card（主题三段+深度/鲜艳滑块+打开日志）、help-menu-button（帮助菜单：检查更新×2/导出诊断/打开目录/Wiki/重看向导）、layout-dialogs（4 个弹窗）、add-url-dialog（MATCH 兜底 URL）、mini-traffic-panel、connections-panel、first-run-guide-dialog

## 4. 状态管理

**没有 zustand/recoil**。方案为四层：
1. **@tanstack/react-query v5**（staleTime Infinity, retry 3/delay 5s）。queryKey 全集：`getProxies`/`getClashConfig`/`getProxyProviders`/`getRuleProviders`/`getRules`/`getSystemProxy`/`getRunningMode`/`appUptime`/`getVergeConfig`/`getProfiles`/`getRuntimeConfig`/`getVersion`/`getClashInfo`/`isServiceAvailable`/`getSystemState`/`getAutoProxy` + WS 缓存键 `getClashConnection-{date}`/traffic/log 键、`get_icon_cache_*`
2. **Context**：app-data-context 拆 7 个（Proxies/Rules/ClashConfig/System/Uptime/CoreDataStatus/Refreshers，SystemContext 含 `isSettingsOpen` 即抽屉开关）；WindowContext（decorated/isLargeMode/isDecorationsHidden/isMinimalWidth/isMiniStatus/minimize/close/toggleMaximize）；ThemeModeProvider（仅 'light'|'dark'）
3. **模块级单例**：DelayManager（延迟缓存 TTL 30min，一对多 listener，NODE_DELAY_MIN 30ms/MAX 2000ms，历史回落上限 10min）、notice-service（最多 50 条，success 3000/info 5000/error 8000ms）、TrafficWorkerClient（引用计数，闲置 5 分钟销毁）
4. **localStorage**：`clash-mini-first-run-completed`、`clash-mini-last-enhanced-uid`、`clash-mini-control-skin`、`clash-mini-original-val2`、`verge-language`、`proxy-head-state`（经后端 get/save_proxy_head_state 持久化）、`mihomo_connection_date`、`last-non-large-size`、`clash_verge_unlock_results/time`

关键 hook：use-verge（乐观更新 patchVerge）、use-profiles（含 activateSelected：selected 校准防线——过滤词子集校验+失败回写内核实际节点）、use-clash/use-clashInfo、use-system-state（启动 10s 宽限轮询 Sidecar/Service/Admin）、use-visibility（窗口可见性 1000ms 防抖，控制 WS 断连）、use-mihomo-ws-subscription（共享 WS 订阅+引用计数+重连）。

## 5. IPC 契约（必须原样复刻）

**A. Tauri command（src/services/cmds.ts，全带 withIpcTimeout 包装）**：
`frontend_log`、`get_profiles`、`trigger_auto_select`(profileUid,nodeNames,sortType,select)、`enhance_profiles`(→ValidationOutcome: valid/busy/invalid/skipped)、`patch_profiles_config`、`view_profile`、`read_profile_file`、`save_profile_file`、`import_profile`(url,option{with_proxy,self_proxy})、`update_profile`、`delete_profile`、`patch_profile`、`get_clash_info`、`get_runtime_config`、`patch_clash_config`、`patch_clash_mode`、`get_clash_logs`、`get_verge_config`、`patch_verge_config`、`get_sys_proxy`、`get_auto_proxy`、`restart_core`、`open_core_dir`、`open_logs_dir`、`export_diagnostic_bundle`、`show_in_folder`、`download_icon_cache`、`get_running_mode`、`get_app_uptime`、`install_service`、`uninstall_service`、`app_is_admin`、`is_port_in_use`、`get_proxy_addr`、`open_web_url`、`get_auto_launch_status`、`check_core_update`、`start_core_upgrade`、`get_unlock_items`、`get_proxy_head_state`/`save_proxy_head_state`。超时梯度：mihomo 探针类 10s/15s，配置 patch 30s，内核/导入/测速 60s。

**B. tauri-plugin-mihomo 插件 command**（crates/tauri-plugin-mihomo/guest-js/index.ts，前缀 `plugin:mihomo|`）：`update_controller`、`update_secret`、`get_version`、`flush_fakeip`、`flush_dns`、`get_connections`、`close_all_connections`、`close_connection`、`get_groups`、`get_group_by_name`、`delay_group`(已废弃未用)、`get_proxy_providers`、`get_proxy_provider_by_name`、`update_proxy_provider`、`healthcheck_proxy_provider`、`healthcheck_node_in_provider`、`get_proxies`、`get_proxy_by_name`、`select_node_for_group`、`unfixed_proxy`、`delay_proxy_by_name`、`get_rules`、`get_rule_providers`、`update_rule_provider`、`get_base_config`、`reload_config`、`patch_base_config`、`update_geo`、`restart`、`upgrade_core`、`upgrade_ui`、`upgrade_geo`、`clear_all_ws_connections`、WS 四通道 `ws_traffic`/`ws_memory`/`ws_connections`/`ws_logs`(level) + `ws_disconnect`。WS 数据经 Tauri **Channel**（onMessage）回传，不是事件。

**C. 后端→前端事件（listen）**：
- `verge://refresh-clash-config`（250ms 防抖 → revalidate getProxies/getVersion/getClashConfig/getClashInfo/getProxyProviders，use-layout-events.ts:63）
- `verge://refresh-verge-config`（250ms 防抖 → getVergeConfig/getSystemProxy/getAutoProxy/getRunningMode/isServiceAvailable/getSystemState，:88）
- `verge://notice-message`（payload [status,msg]，:94）
- `verge://backend-delay-results`（{group,results:[[name,delay]]} → DelayManager.injectBatchResults，:101）
- `profile-changed`（payload profileUid，800ms 节流，app-data-provider.tsx:307）
- `verge://refresh-proxy-config`（200ms 防抖 refetch proxies，:321）
- `core-upgrade-progress`（{status,progress,message}，use-core-update.ts:36）

**D. 前端→后端 IPC（Tauri API 直调）**：窗口事件 `onResized`/`onThemeChanged`/`startDragging`/`setSize`/`setPosition`/`setTheme`/`setMinimizable(false→maximizable false)`/`innerSize`/`outerPosition`/`currentMonitor`；插件 `@tauri-apps/plugin-updater`（客户端更新 check/download/install）、`plugin-shell open`、`plugin-clipboard-manager readText/writeText`、`plugin-dialog`、`plugin-fs`、`convertFileSrc`（图标缓存）。Web 端还有 traffic/log/connections 三类 WS 订阅（connections 用 snapshot+delta(epochId/sequenceId 校验) 协议）。

## 6. 主题与样式

- **MUI v9 + Emotion**（cache key 'mui'），shadows 全部置 none，typography 基准 fontSize 12/htmlFontSize 14
- **两套色板**（_theme.tsx）：light primary #007AFF / bg #F5F5F5，dark primary #0A84FF / bg #2E303D（iOS 风）；primary 未配置时取系统 AccentColor（CSS 变量探测），original 皮肤强制 hsl(239,26%,49%)
- **6 套控制皮肤（controlSkin）**：retro-3d(默认,'Trump-3D')/original/modern-flat('Modern')/frosted-glass('Frosted')/cyberpunk/monochrome，持久化 localStorage，经 `data-control-skin` 属性 + utils/button-styles.ts 的 get3DButtonStyle/get3DCardStyle/get3DInputStyle/get3DSegmented* 按 skin 分支生成样式；皮肤切换派发 `clash-mini-skin-changed` window 事件
- **CSS 变量系统**（use-custom-theme.ts:360-527 运行时写 documentElement）：--divider-color/--background-color/--selection-color/--scroller-color/--primary-main/--primary-color-rgb/--window-border-color/--scrollbar-*/--user-background-image/--background-opacity/--theme-bg/--theme-panel-bg/--theme-popover-bg/--theme-border/--theme-input-*/--depth-factor/--vibrancy-factor（3D 内阴影按系数缩放）等；另有注入 style#verge-theme 全局样式（6px 滚动条/背景图/MuiDialog 底色）
- 图标：@mui/icons-material（Rounded/Outlined 系）+ assets/image/app-icon.png（标题栏）+ favicon logo.ico；订阅节点图标走后端 icon 缓存（download_icon_cache + convertFileSrc）
- 背景可配 background_image + blend_mode + opacity（theme_setting），窗口背景 transparent（vibrancy radial-gradient 极光动画在 layout.scss `.layout::before` aurora-move 25s）

## 7. i18n / 文案

- 路径：`src/locales/{lang}/{ns}.json`，13 语言：zh/en/ru/fa/tt/id/ar/ko/tr/de/es/jp/zhtw；每语言 11 文件：10 命名空间（connections/home/layout/logs/profiles/proxies/rules/settings/shared/tests）+ index.ts 聚合。fallback=zh；`zh-cn`→`zh`、`zh-tw`→`zhtw` 归一（services/i18n.ts）
- 语言键缓存 localStorage `verge-language`；switchLanguage 同时 patchVerge({language})
- 类型安全：types/generated/i18n-keys.ts、i18n-resources.ts；NoticeManager 用 TranslationKey 强校验

## 移植重点提示（外观行为硬指标）

1. 单一 PROXY 组架构贯穿全部逻辑（calcuProxies 只取 groups[0]，selected 仅一项）
2. 窗口断点：271×126 最小 / 285 宽 / 126 极小窗 / 640×860 大尺寸；流量栏 135/100px；标题栏 30px
3. 长操作引用计数（beginLongOperation）抑制 IPC 超时通知风暴；250ms 事件防抖、200/800ms 刷新节流
4. 唤醒测速：visibilitychange→全节点静默测速，5 分钟冷却，互斥锁
5. 配置持久化细节：首次导入自动激活、enhanced-uid 防重复 enhance、激活选择三重校准防线

未确认项：pages/logs.tsx 是否仍被路由外引用；`enable_hover_jump_navigator`/`quick-routing.ts` 的实际消费方；`home_cards`/`test_list` 等 verge 字段在 Mini UI 中是否可见（疑似上游遗留）。
