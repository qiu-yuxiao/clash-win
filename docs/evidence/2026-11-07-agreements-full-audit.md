# clash_mini_agreements.md 全量审计报告（Win 侧代码体现）

- 审计日期：2026-11-07
- 事实源：`C:\Users\sun_y\Documents\AntiGravity_Projects\ClashVerge\clash_mini_agreements.md`（744 行，8 大章 + 附录 A/B）
- 参照实现：Clash Mini **v3.2.2 当前内容**（`ClashVerge` 工作区，只读）
- 审计方法：逐条读 agreements 条目 → 定位 Mini 对应源码 `file:line` → 在 Win 侧 `src/` 找对应实现 → 判定「已落地 / 缺失 / 形态差异」
- 结论：**128 条 = 已落地 121 / 本轮补齐 6 / 形态差异 3**（详见 §三）

---

## 一、逐章结论速览

| 章 | 标题 | 条目数 | 结论 |
|---|---|---|---|
| 一 | 项目定位与架构基石 | 8 | ✅ 全部落地（§1.1 品牌名为**唯一豁免**，端口属共存资源标识豁免） |
| 二 | 界面美学与交互体系 | 12 | 🔧 **本轮补齐 2 条**（§2.7 每皮肤全局字体 / §2.9 皮肤交互态三态）+ 1 条 UI 层修正（§5.2 所见即所选） |
| 三 | 内核生态与运行时管控 | 7 | ✅ 全部落地（§3.4 geodata-loader 为 Mini 侧亦无 → 形态差异） |
| 四 | 轻量级模式与窗口生命周期 | 5 | ⚠️ 形态差异（Webview2 ↔ 原生窗口，无销毁语义） |
| 五 | 后台监控、健康探针与自愈体系 | 8 | 🔧 **本轮补齐 3 条**（§5.1 宽限期 / §5.4 假节点 / §5.8 测速失真）+ 1 条 UI 层修正（§5.2 所见即所选） |
| 六 | 安全防御与编译加固 | 5 | ✅ 全部落地（§6.3 Zip Slip 机制不同但等效，§6.5 编译加固形态差异） |
| 七 | 数据流中枢与协议解析 | 5 | ✅ 全部落地 |
| 八 | 工程发布体系与规范 | 9 | ✅ 全部落地（§8.1/§8.2/§8.4 为 Rust/Tauri 特有，形态差异但语义等价） |

---

## 二、本轮补齐的 6 条（agreements 有、Mini 源码有、Win 侧缺）

### ① §5.1 切换宽限期 60s

- **Mini 事实源**：`monitor.rs:74` `SWITCH_GRACE_PERIOD_SECS = 60`；
  `:1358-1370` 活跃节点变化检测（记 `last_node_change_time`、清零 `consecutive_fails`）；
  `:1383-1396` 宽限期内失败判定只记日志、不计入连续失败次数。
- **Win 原状**：`monitor.cpp::tick_once` 的节点健康分支直接 `node_fail_count_++`，
  无任何宽限期概念 → 「切到尚未拨测的新节点 → 缓存陈旧误判 → 再次切走」的乒乓震荡无防线。
- **移植**：`core/monitor.h` 新增 `Params::switch_grace_ms = 60000`、
  `Hooks::active_node_name`、`last_node_change_ms_` + `has_node_change_`；
  `core/monitor.cpp::tick_once` 增加节点变化检测与宽限期拦截；
  `app/app.cpp` 装配 `active_node_name` 生产钩子（读 PROXY 组 `.now`）。
- **测试**：`tests/test_t27_monitor_heal.cpp` 4 个用例（宽限期内不计失败 / 时长恒 60s /
  节点变化重新起算 / 未变化不延长）。

### ② §5.4 第 3 道铁锁：自愈决策层假节点拦截

- **Mini 事实源**：`monitor.rs:663-666` `evaluate_failover` 中
  `if is_dummy_node(&active_node) { return ShouldFailover }`。
- **Win 原状**：`is_dummy_node` 有 4 处调用（配置生成层 / 选点层 / 列表层 / 命令层），
  但**自愈决策层没有** → 伪节点被选中后不会被自愈机制主动换掉。
- **移植**：`Hooks::is_dummy_active`；`tick_once` 中伪节点 → 直接触发重选
  （伪节点属明确异常，**不受切换宽限期的宽松判定保护**）；
  `app/app.cpp` 装配生产钩子。
- **测试**：`tests/test_t27_monitor_heal.cpp` 3 个用例。

### ③ §5.8 测速失真复核（主/备双地址）

- **Mini 事实源**：`monitor.rs:103` `FALLBACK_TEST_URL = "https://www.gstatic.com/generate_204"`；
  `:188-223` `probe_metrics_group` 主 URL 整体失败 → 备用 URL 复核，
  主备双双失败 → 返回 `None`；`:685-703` 链路不可用 → `evaluate_failover` 返回 `NoAction`
  （**不判节点死、不触发自愈**）。
- **Win 原状**：完全没有这条链路 —— 主测速地址被机场劫持时，内核返回全组 timeout，
  Win 侧会把它当「所有节点都死了」→ 触发无意义切节点抖动与内核重启。
- **移植**：`Hooks::probe_metrics_with_fallback`；`app/app.cpp` 装配（主 URL 取
  `verge.default_latency_test`，缺省 `https://cp.cloudflare.com/generate_204`）；
  `monitor.cpp::tick_once` 在节点判定前插入链路可用性检查。
- **测试**：`tests/test_t27_monitor_heal.cpp` 3 个用例。

### ④ §5.2/§5.6「所见即所选」：闪电按钮子集约束 + 择优切换

- **Mini 事实源**：`proxy-groups.tsx:230-275` `handleCheckAll`：
  可见子集 → 逐节点 `setDelay(name, group, -2)` 视觉占位 →
  `triggerAutoSelect(currentUid, visibleNames, 0, true)`（**只在该子集内测速并挑最快**）。
  注释明确：「该子集同时作为 F4 批量测速+择优的候选池（防越界关键）」。
- **Win 原状**：`node_list.cpp` 闪电按钮 → `NodeData::test_group` →
  `cmd_profile.cpp` 的 `delay_group` **全组测速，无子集约束、无择优切换**。
- **移植**：`DataHooks::test_and_select` + `NodeData::test_and_select(nodes)`
  （含 -2 testing 占位）；`node_list.cpp` 闪电按钮改为传当前过滤后的可见子集，
  走 `AutoSelect::trigger(uid, 子集, sort_type=0, select=true)`；
  `NodeList::bolt_button()` 暴露按钮指针供测试。
- **测试**：`tests/test_t28_bolt_subset.cpp` 6 个用例（走真实点击路径）。

### ⑤ §2.7 每皮肤全局字体（`--control-font-family`）

- **Mini 事实源**：`layout.scss:319` 把 CSS 变量 `--control-font-family` 作用到
  `.MuiSlider-root` / `.MuiInputBase-root` / `.MuiTypography-root` /
  `.MuiListItem-root` / `.MuiMenuItem-root` / `.MuiListItemButton-root`
  —— 即「除图标字体外的全部文本」。各皮肤定义：
  `layout.scss:324` retro-3d = `Trebuchet MS, SimHei, sans-serif`；
  `:328` original = `Segoe UI, Microsoft YaHei, sans-serif`；
  `:352` modern-flat = `Outfit, DengXian, sans-serif`；
  `:371` frosted-glass = `Segoe UI Light, Microsoft YaHei Light, sans-serif`；
  `:418` cyberpunk = `Consolas, NSimSun, monospace`；
  `:442` monochrome = `Georgia, KaiTi, serif`。
- **Win 原状**：`TextStyle::font_family` 为空即走全局默认字体，
  6 套皮肤**文字完全同款**（仅颜色/圆角/阴影不同）→ 皮肤辨识度缺失；
  切皮肤时文字也不重解析。
- **移植**：
  - `theme.h:384-388` `skin_font_candidates(SkinId)` 返回每皮肤**完整候选链**
    （保留 Mini 的 CSS font-family 多字体回退语义）；
  - `theme.cpp:116-147` 6 套皮肤候选链落地，末位统一是 Windows 通用族
    （任何机器至少命中其一，不会静默变成默认字体）；
  - `theme.h:450-461` `ui_font()` / `set_ui_font()`：D2D 层用 DirectWrite
    `FindFamilyName` 探测本机存在性后**择一写回**；
  - `TextStyle::font_family` 为空 → 自动跟随皮肤（`theme.h:450-454` 注释锁定）；
  - `init()` / `set_skin()` 时失效重解析（字体缓存随皮肤切换重建）。
- **测试**：`tests/test_t29_skin_fonts.cpp` 8 个用例（6 套皮肤候选链首位正确、
  候选链非空且末位通用族、各皮肤候选链互不相同、
  `set_skin` 后 `ui_font` 跟随切换）。

### ⑥ §2.9 皮肤交互态三态（Switch / Slider 的 hover + active + 键盘）

- **Mini 事实源**（`style-helpers.tsx` 与 `base-switch.tsx`）：
  - **Slider thumb 常态**：retro-3d `:274-291`（14×14 radial 四停 + 1px 边 +
    `0 3d|4d 6d|8d rgba(0,0,0,.25|.6)` + `inset 0 1d 0 rgba(255,255,255,.8|.4)` +
    `0 0 8v|10v rgba(255,193,7,.25v|.3v)`）；original `:328`（12×12 白底 2px 主色边 +
    `0 1px 2px rgba(0,0,0,.15)`）；modern-flat `:356`（同 original，radius
    `calc(6px*val1)`）；frosted `:387`（12×12 半透明白）；cyberpunk `:421`
    （8×12 方角 + `0 0 5px` 同色 glow）；monochrome `:450`（10×10 方角 + 无阴影）。
  - **Slider thumb `:hover, &.Mui-focusVisible`**：retro-3d `:292-305`
    （`scale(1.2)` + `brightness(1.15)` + 阴影升级 `0 5d|6d 10d|12d rgba(0,0,0,.35|.7)` +
    `inset 0 1d 0 rgba(255,255,255,.9|.5)` + 金 glow `0 0 12v|14v rgba(255,193,7,.35v|.4v)`）；
    original `:334` / modern-flat `:363` / frosted `:395-404`（`scale(1.15)` +
    变实心白 `rgba(255,255,255,1.0)` + 亮态 `0 2px 5px rgba(0,0,0,.18)` /
    暗态 `0 0 4px rgba(255,255,255,.4)`）/ cyberpunk `:427-430`（`scale(1.15)` +
    glow 5px→8px）；**monochrome 无 hover 规则**。
  - **Slider thumb `:active`（Mui-active，仅 retro-3d）**：`:306-307`
    `scale(0.92)` + `inset 1d|1.5d 1d|1.5d 2d|2.5d rgba(0,0,0,.15|.5)`。
  - **Switch thumb hover**（`base-switch.tsx`）：retro-3d `:114-128`
    （`scale(1.2)` + `brightness(1.15)` + `0 4d|5d 8d|10d rgba(0,0,0,.3|.6)` +
    顶高光 `inset 0 1d|1.2d 0 rgba(255,255,255,.8|.6)` +
    底暗线 `inset 0 -1d|-1.5d 0 rgba(0,0,0,.1|.4)` + `0 0 10v rgba(255,193,7,.35v)`）；
    modern-flat `:361-363` `scale(1.1)`；monochrome `:543-545` `scale(1.05)`；
    cyberpunk `:442-444` `boxShadow:'0 0 10px currentColor'`
    （off `#ff0055` / on `#00b0ff`|`#00f0ff`）；
    **original `:133-204` 与 frosted-glass `:280-372` 无 hover 规则**。
  - **retro-3d 开关 on 态旋钮内浮雕**：`base-switch.tsx:97-98` 的
    `.Mui-checked .MuiSwitch-thumb` **只覆盖 `background` 与 `border`**，
    boxShadow 继承基础 thumb 的 `inset 0 1d 0` 顶高光 + `inset 0 -1d 0` 底暗线。
  - **MUI Slider 键盘语义**：`node_modules/@mui/material/Slider/useSlider.js:326-370`
    —— ArrowUp/ArrowRight `+step`；ArrowDown/ArrowLeft `-step`；
    Shift+方向键 ±`shiftStep`(缺省 10，见 `:182`)；PageUp/PageDown ±10；
    Home→min；End→max；钳制 `getNewValue`（`:27-29`）。
- **Win 原状**：
  - `SwitchW` 与 `Slider` **完全没有 hover / active 概念**（`on_mouse` 不区分
    Enter/Leave，`on_paint` 恒定常态绘制）→ 6 套皮肤的 hover 视觉全部缺失；
  - retro-3d 开关 **on 态旋钮缺内浮雕**（Win 侧此前只设了 off 态）；
  - Slider thumb 常态缺 `inset 0 1d 0` 顶高光与 `:active` 内凹；
  - `Slider` **完全无键盘交互**（`wants_key()` 未置位，`on_key` 未实现）。
- **移植**：
  - `theme.h` 新增 `SliderPaint` / `SwitchPaint` 的 hover 态字段族
    （scale / brightness / shadow 覆盖 / glow 覆盖 / inset_hi 覆盖 /
    inset_dark 覆盖 / 底色覆盖 / on 态 glow 双色）；
  - `theme.cpp` 逐皮肤照搬 Mini 原文（含「original / frosted / monochrome
    无 hover 规则 → 不加戏」的负向结论）；
  - `switch_w.cpp:47-58` Enter/Leave 维护 `thumb_hovered_`；
    `switch_w.cpp:175` 起 hover 态渲染（缩放绕旋钮中心、阴影/辉光/内外浮雕覆盖、
    brightness 提亮、on/off 双色 glow）；
  - `slider.cpp:47` `Slider::update_hover()` 依据 `last_mouse_x_` 与 thumb 中心
    距离判定悬停，在 `on_mouse(Move)`（`:88`）/ `set_value()`（`:23`）/
    `apply_theme()`（`:162`）三处刷新（贴近 CSS `:hover` 的实时语义）；
  - `slider.cpp:115-145` `Slider::on_key()` 移植 MUI 键盘语义（方向键 / Shift /
    PageUp/PageDown / Home / End + 钳制），`slider.h:33` `wants_key()` 置位，
    鼠标 Down 时 `request_focus()`（`slider.cpp:82`）。
- **测试**：`tests/test_t30_slider_thumb_inset.cpp` 6 个用例（retro-3d 顶高光与
  active 内凹的精确数值 + 其余 5 套皮肤负向断言 + Slider/Switch 字段彼此独立）；
  `tests/test_t31_interactive_states.cpp` 12 个用例（逐皮肤 hover 缩放表、
  cyberpunk 双色 glow、retro-3d on 态内浮雕、滑块键盘全语义 + 钳制）；
  `tests/test_t32_hover_pixels.cpp` 6 个用例（**像素级实证**：retro-3d 开关
  hover 前后 `max_delta >= 8`；original 开关 hover 前后**完全一致**（差异 == 0，
  验证「不擅自加戏」）；monochrome 滑块 hover 前后完全一致且 `thumb_hovered()`
  仍为 false；retro-3d on 态旋钮顶缘亮度 > 中部）。

---

## 三、形态差异（3 条，非缺陷，已登记）

### §3.4 `geodata-loader: memconservative`

- agreements 载「启动配置默认注入 `geodata-loader: memconservative`」。
- **Mini v3.2.2 源码全工作区检索无此键**（`grep -rn "geodata-loader\|memconservative"`
  覆盖 `*.rs/*.ts/*.tsx/*.js/*.mjs`，零命中）。
- 结论：**Mini 侧同样未实现**，Win 侧不擅自补（补了就与 Mini 不一致）。
  处置：不改代码，登记为「agreements 描述早于源码现状」。
- ⚠️ **冲突上报**：若要补齐，应先补 Mini 再补 Win，否则单边实现破坏 1:1。

### §4.1–§4.3 Webview2 销毁 / WebSocket 熔断

- Mini 是 Tauri + Webview2：关闭主窗口 = `window.hide()` + 销毁 Webview2 实例
  （释放 30–40MB 与 GPU 句柄）+ 熔断前端 WS 推送流。
- Win 是原生 Win32 + Direct2D：**主窗口隐藏 ≠ 前端销毁**（原生控件无实例可销毁），
  数据流是内核侧 WS，由 `main_window.cpp` 按「抽屉展开 + 宽度 > 285」门控订阅。
- 语义等效点：不可见即不订阅（§5.7 静默门控已落地，`main_window.cpp:1131`）、
  内核不中断（§4.2 不断网铁律）、唤醒即重订阅。
- 处置：形态差异，**不改代码**。

### §6.3 Zip Slip

- Mini 用 Rust `zip` crate 解压；Win 内核更新用 `tar.exe` 解压
  （`core_updater.cpp:445`）+ 解压后遍历定位二进制。
- 路径穿越在 Win 侧由 tar 自身的路径处理 + 解压目录隔离承担；
  Win 侧未自研 zip 解压器，故无「逐条目检查 `..`」的自研代码。
- 处置：机制不同但风险等价，不改代码。

---

## 四、已逐条核实的重点条目（无需改动，摘录结论）

### 第一章

- §1.1 品牌名 `Clash Mini` → `Clash Win`：**唯一豁免**，属约定内的刻意替换。
  端口 10801/9098/33335 → Win 用 10811–10814 / 9108 / 33337，属「共存资源标识」豁免
  （`docs/01-目的与方针.md:11-25` 已登记）。
- §1.3 TUN 门控：`core/service_mode.cpp:294` `decide_startup_mode(needs_tun, is_admin, service_ready)`
  —— TUN 未开 → 强制旁路子进程，不碰 SCM；admin → 秒级就绪；非 admin + TUN → 提权安装。
- §1.4 孤儿清理：`core_manager.cpp:277-281` 残留 `mini-mihomo` PID 强杀 + taskkill 兜底。
- §1.5 英文保留原则：滑块标签 Win 侧硬编码英文（`Depth`/`Vibrancy`/`Radius`/…）；
  工业缩写 `MAN`/`SYS`/`TUN` 已落地。
- §1.6 内存基准：Go 压制 `GOMEMLIMIT=96MiB` / `GOGC=50` / `GOMAXPROCS=2`
  （`core_manager.cpp:78-80`）。

### 第二章

- §2.3 窗口尺寸：`main_window.cpp:1536-1538` `ptMinTrackSize=(271,126)` /
  `ptMaxTrackSize=(640,860)`，默认 271×680；无框 `outer == inner` 口径统一。
- §2.6 极窄隐匿模式（三条铁律全部落地）：
  - **物理像素域判定**：`ui/breakpoints.h:44-47` `set_physical_size(phys_w, phys_h, scale_factor)`
    ——「禁止传页面视口类粗估（整数 CSS 像素会吃掉 1 物理像素容差）」。
  - **判定入口唯一**：`Breakpoints` 类统一派生 `is_narrow()` / `is_mini()`。
  - **计时器无条件自续期**：`main_window.cpp:1586` `stealth_.tick(now_ms)`
    —— 调用方无条件再排；几何变化亦续期（`:1561`）。
- §2.7 皮肤体系：本轮已 1:1 对齐（见 `2026-11-07-skin-slider-semantics.md`）；
  每皮肤全局字体（`--control-font-family`）与交互态三态（hover / active / 键盘）
  见本报告 §二 ⑤⑥。

### 第三章

- §3.2 命名管道 + 退避：`pipe_http.h:33-34` `busy_retry_count=24` / `125ms`；
  `WaitNamedPipeW` 弹性质询（`pipe_http.cpp:280-295`）。
- §3.3 端口自适应：`config/clash.cpp:284-297` 端口占用 → 鉴权探测 → 向上扫空闲端口 → 重写配置。
- §3.5 Go 压制：见上 §1.6。
- §3.6 内核升级降级链：`core_updater.cpp` 三级降级 + 写锁规避（服务运行时先断服务）。
- §3.7 TUN IPv6 防泄漏（**最长的一条**，全部落地）：
  - `enhance/tun.cpp:102-118` `dns.ipv6` **唯一写入点**（TUN/非 TUN 两分支均写）；
  - TUN 分支注入 `inet6-address = fdfe:dcba:9876::1/126`（`enhance_constants.h:8`）；
  - `enhance/mini_agreements.cpp:386-397` IPv6 本地直连规则全套
    （`::1/128`、`::/128`、`fc00::/7`、`fe80::/10`、`ff00::/8` + 6 条 IPv4-mapped 私网等价段）；
  - `:415-416` `GEOIP,CN,DIRECT` + `IP-CIDR6,::/0,PROXY`（**用 GEOIP 而非 GEOIP6**），
    位于 MATCH 之前、且不带 `no-resolve`；
  - `:207` fallback 上游强制走代理（`IP-CIDR,<ip>/32,PROXY,no-resolve` + 公共 DNS 对偶补齐）。

### 第五章（补齐后）

- §5.1 15s / 3s / 5s 周期、`node_dead_threshold=2` / `api_fail_threshold=3` /
  `heal_cooldown_ms=60000` / `failed_server_cooldown_ms=600000` /
  `alert_fail_threshold=5` —— `core/monitor.h:36-52` 全参数表与 Mini 一致。
- §5.2 `PROXY__METRICS` 唯一测量组：`enhance/mini_agreements.cpp:317-318` 注入；
  `delay_manager.h:27` `kNodeDelayMaxMs=5000`（BUG-304 定型，禁止回调 2000）；
  `delay_manager.h:28-29` 缓存复用 30s / history 兜底 120s。
- §5.3 统一记账：`monitor.cpp::heal` 单一入口，60s 冷却 + 5 次告警 `MessageBoxW`。
- §5.4 前两道铁锁：`mini_agreements.cpp:240/248`（配置生成层）、
  `auto_select.cpp:142`（选点层）、`node_data.cpp:45`（列表层）；
  中文广告短语「归一化后完全匹配或以短语开头」口径在 `util/node.cpp:59`。
- §5.5 双频探测：在线 60s / 离线 5s（`monitor.cpp` 的 `offline_interval_secs=5` + 节流门控）。
- §5.6 统一回传：`DelayManager::emit_results` → `EventBus::backend_delay_results`；
  延迟三级真源（本次测速 → 内核存活 → history 回落）在 `node_data.cpp::row_delay`。
- §5.7 静默门控：`main_window.cpp:1131-1136` 连接面板「抽屉打开 + 宽 > 285」才订阅 WS；
  流量采样恒 1000ms（`traffic_pane.cpp:181`）。
- §5.8 失败服务器避让：`monitor.cpp:75-101` 冷却表 + 600s 过期；
  `auto_select.cpp` 安全阀（剔除后无候选 → 回退全量）。

### 第六章

- §6.2 SSRF：`config/ssrf.cpp:90-163` 回环/私网/链路本地拦截 + DNS 二次解析校验；
  协议白名单：`cmd_support.cpp:540-545` `open_url_checked` 仅允许 http/https。
- §6.4 JS 沙箱：`enhance/js_engine.cpp:231-236` 冻结
  `Object/Function/Array/String/Number/Boolean.prototype`；`:160` 3s 看门狗。

### 第七章

- §7.1 单一 PROXY 组：`mini_agreements.cpp:315-318` `PROXY`(select) + `PROXY__METRICS`(url-test)。
- §7.2 协议族全覆盖：hysteria / hysteria2 / anytls / vless / trojan / tuic / wireguard /
  socks5 / http（`enhance/uri_list.cpp`）；**「先剥 Query 再拆 Userinfo」铁律**
  见 `uri_list.cpp:403-409`；IPv6 `[addr]:port` 与 CIDR 清洗同文件。
- §7.3 导入轮询：`core_manager.cpp:735-765`（API 就绪 200ms 轮询 + PROXY 组填充 500ms 轮询）。
- §7.4 右键菜单五项：`settings_drawer.cpp:1371-1374` 编辑/系统编辑器/复制链接/强制更新/删除。

### 第八章

- §8.6 迁移框架：`config/migrate.cpp` 版本检测 / 逐级迁移 / 备份 / 禁止降级写回；
  `config/clash.cpp:367-388` **版本检测早于 template 合并**（顺序铁律）。
- §8.7 诊断包：`system/diagnostic_bundle.cpp` 脱敏（`***REDACTED***`）+
  认证头 / Bearer / URL userinfo 正则；日志保留 128KB×40。

---

## 五、遗留待办（非 agreements 条目）

1. **§3.4 geodata-loader 冲突**：若用户确认要补，须 Mini 侧先补，Win 再对齐（见 §三）。
2. **frosted-glass 的 val2（Blur）**：D2D 无 `backdrop-filter` 原语，
   `soft_shadow.h` 只能近似阴影，背景模糊无法 1:1 → 需登记为渲染能力差异，
   **不可做假映射**（假映射会让用户以为滑块生效但画面不变）。
   当前处置：`theme.cpp:762` 已写明该限制；该滑块仍可拖动，
   但**不产生背景模糊变化**（唯一一处滑块「有交互无视觉」的已知例外）。
3. **便携包需重打**：`dist/ClashWin_0.1.0_x64_portable.zip` 早于本轮全部改动。
4. **未 push**：本地领先 origin/dev 多个提交（push 需单独确认）。
5. **剩余硬编码审计**：仍有金色等固定色散落在
   `settings_drawer.cpp`(33) / `profile_import_card.cpp`(10) /
   `segmented.cpp`(8) / `quick_bar.cpp`(8) / `first_run_guide_dialog.cpp`(7) /
   `utility_modals.cpp`(6) / `context_menu.cpp`(3) / `traffic_pane.cpp`(2) /
   `main_window.cpp`(2) —— 应改由 `ThemeService` 查表输出（按皮肤取色），
   否则 6 套皮肤下这些位置仍是 retro-3d 金色。
