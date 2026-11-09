# Mini v3.2.2 → v3.2.3 增量对照审计（只读，未改产品码）

- **日期**：2026-11-10
- **触发**：用户指定事实源锚点升级至 Mini v3.2.3（已核实：`ClashVerge` 工作区 `package.json:3` / `src-tauri/tauri.conf.json:2` / `src-tauri/Cargo.toml:3` 均为 `3.2.3`，git tag `v3.2.3`，release commit `b7d108fe` 2026-11-09）。
- **范围**：`git diff v3.2.2..v3.2.3` 全部 13 文件（+238/−26）。实质行为变更 = BUG-314 三条；其余为版本号、格式重排、agreements 补记（+65 行）、updater manifest。
- **Win 基线**：`dev` HEAD `7834255`（审计时点，工作树干净）。
- **方法**：Mini 差异逐行读 + Win 侧逐点 grep/源码核对。本文只记录差距与一致结论，不改码。

---

## 一、BUG-314① 过滤自动并发测速择优（Mini `proxy-head.tsx` / `base-search-box.tsx` / `proxy-groups.tsx`）

**Mini v3.2.3 语义**（`proxy-head.tsx:62-122`，v3.2.3 行号）：
- 过滤框停输 **700ms** 防抖 → 再延迟 **60ms**（等 React 过滤计算完成）→ `onCheckDelay()` 对当前过滤可见子集并发测速择优（`select=true`，最优自动激活）；
- **Enter 即时触发**，IME 保护 `e.key==='Enter' && !isComposing && keyCode!==229`（`base-search-box.tsx:127-135`）；
- `lastTriggeredFilterRef` 去重：**同一过滤词不重复触发**；
- 清空（trim 为空）只重置不触发；空匹配短路（`proxy-groups.tsx:253-258`）。

**Win 现状**：主体链路**已实现**（比 2026-11-09《Win和mini的差别》△4 记录的认知更完整）：
- 防抖触发测速择优：`src/ui/scenes/node_list.cpp:503-518`（800ms + seq 作废旧定时）；
- Enter 即时：`node_list.cpp:519-522` → `trigger_auto_select_visible()`；按键源 `src/ui/textfield.cpp:148-159`（`'\r'` → on_submit）；
- 子集并发测速 + select=true 激活最优：`node_list.cpp:660-683` → `node_data.cpp:132-164/267-286` → `src/core/auto_select.cpp:102-210`（`test_group("PROXY", 子集)` → `pick_fastest` → `select_node("PROXY", fastest)`）；
- 清空/空匹配短路：`node_list.cpp:505` + `:661`。✔ 一致

**差距清单（按调试第一铁律应照 Mini 改，待实施）**：

| # | 差距 | Mini 证据（v3.2.3） | Win 现状证据 |
|---|------|--------------------|--------------|
| G1 | 防抖 **800ms → 700ms** | `proxy-head.tsx` `setTimeout(..., 700)` | `node_list.cpp:509` `milliseconds(800)` |
| G2 | **失焦触发是 Win 加戏**，Mini 无此通道 | `proxy-head.tsx` 仅 onChangeText/onEnter 两通道 | `node_list.cpp:523-528` `on_focus_lost` 触发 |
| G3 | **IME 组词保护缺失**：Enter 在中文输入法组词期可能误触发 | `base-search-box.tsx:127-135` `!isComposing && keyCode!==229` | `main_window.cpp:2160-2165` WM_IME_* 仅更新候选窗位置；`textfield.h:68-75` set_composition 预留接口全仓无调用方；Enter 判定无 composing 守卫 |
| G4 | **同词去重缺失**：输入后又删回原词会再触发一轮测速 | `proxy-head.tsx` `lastTriggeredFilterRef` 比对 trimmed | `node_list.cpp:603` set_filter 同词早退只跳过列表重建，防抖仍重新计时；触发端无「上次已测词」记录 |
| G5 | 触发后 60ms 延迟未复刻 | `proxy-head.tsx` `triggerAutoDelayTest` setTimeout 60ms | Win 立即执行。**判定：React 状态时序补偿，非用户可见语义**（Win 过滤在 UI 线程同步完成），按总纲 §5.5 实现层差异登记，不改 |

注：交接报告待决项 **D5**（800 vs 700ms + 失焦触发）自本审计起**不再是待裁决项**——Mini v3.2.3 已明确 700ms、无失焦通道，按铁律照改（G1/G2）。

## 二、BUG-314② 日志弹窗锁定 271px（Mini `layout-dialogs.tsx` / `logs.tsx`）

**Mini v3.2.3 语义**：宽 = `MIN_WINDOW_WIDTH`（271px）固定、`maxWidth:100%`、`m:'0 auto'` 居中，高 480px；header `flex:0 0 44px`、`pr:5 / pl:1.5`、标题 15px/600；关闭钮 top/right 10；工具栏 gap 0.5（4px）、暂停/升降序图标 18px + 按钮 p:0.5、清空按钮 11px 字号/24px 高/px:1 py:0.2；级别下拉 `width:95, minWidth:90`；级别下拉 + 搜索框同行（`logs.tsx:180-208`，行高 39px、mx:10px）；日志条目 `overflowWrap:'anywhere'` 自动折行、无横向滚动条。

**Win 现状**（`src/ui/scenes/utility_modals.cpp` LogViewerModal）：
- 外框已对齐：宽 `min(area.w, 271)`、高 `min(480, area.h-16)`、水平垂直居中（`:1014-1021`）。✔（但 271 为字面量，未引用 `src/ui/breakpoints.h:17` `kMinWindowW`，建议顺手收敛）
- 搜索框存在 = **一致**（Mini `logs.tsx:203-208` 有 BaseSearchBox；审计中曾疑为加戏，已核实排除）。✔

**差距清单**：

| # | 差距 | Mini 证据（v3.2.3） | Win 现状证据 |
|---|------|--------------------|--------------|
| G6 | header 高 ≈30 → **44px**；标题 13.5 Bold → **15px/600**；padding pr5/pl1.5 | `layout-dialogs.tsx:571-580` | `utility_modals.cpp:1082-1083`（13.5 Bold）、`:1104`（分隔线 cy+30） |
| G7 | 关闭钮 top/right **6 → 10** | `layout-dialogs.tsx:594-595` | `utility_modals.cpp:1024`（cx+cw-26, cy+6） |
| G8 | 级别下拉宽 **76 → 95**（minWidth 90） | `logs.tsx:190` | `utility_modals.cpp:1031-1033`（76×28） |
| G9 | 工具按钮：文本字形（⏸/▲/清除，10.5 字号、22px 高）→ **18px 图标 + p:0.5**；清空按钮 **11px/24px 高** | `logs.tsx:114-170` | `utility_modals.cpp:1023-1033/1119/1127-1130` |
| G10 | 日志文本 **ellipsis 截断 → anywhere 折行**（271px 下无横向滚动、自动换行） | `logs.tsx` LogItem `overflowWrap:'anywhere'`（agreements §2.2 新增条文） | `utility_modals.cpp:1194-1195` `wrap=false; ellipsis=true` |

## 三、BUG-314③ 安装器全周期置顶（Mini `installer.nsi`，agreements 新增 §8.10 铁律）

**Mini v3.2.3 语义**：四处显式 `BringToFront + SetWindowPos($HWNDPARENT, -1, 0,0,0,0, 3)` —— `myGuiInit`、`un.myGuiInit`（新增 UNGUIINIT 绑定）、`Section EarlyChecks`、`Section Install`、`Section Uninstall`。

**Win 现状**（`installer/clashwin.nsi`）：置顶机制 `KeepTopMost`（:416-423）/ `un.KeepTopMost`（:445-452）比 Mini 更强（含 WS_EX_TOPMOST 样式 + SetForegroundWindow），覆盖面更广：
- ✔ `myGuiInit`（:425-427，接线 :126）、`un.myGuiInit`（:454-456，接线 :153）
- ✔ 各页面 Show（Welcome/Directory/InstFiles/Finish/un.Confirm/un.InstFiles，:429-460）
- ✔ `Section "Install"` 首行（:659-660）

**差距清单**：

| # | 差距 | Mini 证据（v3.2.3） | Win 现状证据 |
|---|------|--------------------|--------------|
| G11 | **卸载执行段无显式置顶重申**（仅靠先前 WS_EX_TOPMOST 样式被动维持，被其它 topmost 窗口抢占后无重夺机制） | `installer.nsi` `Section Uninstall` 首行 BringToFront+SetWindowPos | `clashwin.nsi:852-945` `Section "Uninstall"` 无 KeepTopMost/BringToFront 调用 |
| G12 | **早期检查阶段无置顶**（Mini 在 EarlyChecks 前置；Win 对应阶段为 .onInit，首个显式置顶在 myGuiInit） | `installer.nsi` `Section EarlyChecks` 首行 | `clashwin.nsi:393-413` `.onInit` / `:812-825` `un.onInit` 无置顶调用。**判定备注**：.onInit 时 GUI 尚未构造、$HWNDPARENT 未必可用，落点需实施时验证（可能等效于 myGuiInit 已覆盖，属形态差异候选） |

## 四、agreements 新增条款核对（+65 行）

| 条款 | 性质 | Win 侧结论 |
|------|------|-----------|
| §1.1 BUG-313 注记 + 附录 A BUG-313 | 端口/密钥 1:1 + 安装清洁 | 端口部分已 1:1（`constants.h:22-35`）；**「清除 Mini 残留/清理服务」表述与用户 2026-11-09 互斥共存方针冲突，见第五节** |
| §1.8 服务自举正文 | **补记既有行为**（文中明言「不是新增需求…此前遗漏，2026-11-05 补记」） | Win 已有完整链路（C10 守卫 + 服务安装/接管，见 bug_list C10、互斥方针落地），无新增缺口 |
| §2.2 日志弹窗规范 / §2.8 过滤择优 | BUG-314 的 agreements 化 | 即本文第一、二节 |
| §8.6 配置迁移框架 | 补记既有行为 | ✔ 已对齐：`src/config/migrate.cpp:15-37`（detect_version、`.bak.v<from>` 同名只备一次）、`src/config/clash.cpp:356-395`（**检测先于模板合并**顺序铁律有注释锁定、备份失败中止迁移）、v2→v3 日志数 8→40（`migrate.cpp:61-64`） |
| §8.7 日志保留与诊断包 | 补记既有行为 | ✔ 已对齐：`src/system/diagnostic_bundle.cpp`（脱敏、2MB 尾截断 `:36`、60 文件上限 `:37`）、`src/cmd/cmd_support.cpp:530-535` show_in_folder（explorer /select）、帮助菜单入口 `src/ui/help_menu.cpp` |
| §8.8 前端单测基线 | Mini 侧 vitest 纯逻辑测试 | 形态差异：Win 以 C++ 回归套件覆盖同语义（过滤/流量换算/假节点/延迟格式化均在 tests/ 有对应），不引入 vitest，登记即可 |
| §8.9 i18n 硬约束 | 补记既有行为 | ✔ 13 语种齐全（`assets/locales/` ar/de/en/es/fa/id/jp/ko/ru/tr/tt/zh/zhtw）；t() 等价物与 key 纪律由 Win i18n 框架承接（`src/i18n/i18n.h`）；TS 专属条文（@ts-expect-error/i18n:types）不适用 |
| §8.10 安装器置顶铁律 | BUG-314③ 的 agreements 化 | 即本文第三节 |
| 附录 A BUG-314 | 上述三条的追溯登记 | — |

## 五、★ 冲突上报（总纲 §3.2，裁决前暂停相关改动）

**agreements v3.2.3 新增文本** vs **用户 2026-11-09 当场裁决**：

- **agreements 侧**（`ClashVerge/clash_mini_agreements.md` v3.2.3，§1.1 注记原文）：「Clash Win 安装包负责在安装时**清除宿主机所有 Clash Mini 历史进程、服务与孤儿程序**，营造绝对清洁环境」；附录 A BUG-313② 进一步写「彻底清理…自启注册表项及 Windows 服务」。
- **用户裁决侧**（2026-11-09 互斥共存方针，已固化总纲 §5.2 与 MEMORY）：「**不删除对方磁盘上的任何东西**（安装目录/配置/资产全保留）」；服务走接管注册而非清除；杀进程仅限运行时 TerminateProcess。
- **冲突点**：「清除孤儿程序/清理注册表项」若按字面实施会违反「不删对方磁盘任何东西」；「清理服务」与「同名服务接管」语义不同。
- **处理**：按 §3.2 不得自行裁决。本文登记双方出处，**涉及该表述的安装器改动暂停**，待用户裁决。倾向性说明（仅供裁决参考，不作执行依据）：用户 2026-11-09 裁决在时间上晚于该 agreements 文本的原始形成（BUG-313 注记是对旧架构修正的追记），且总纲 §5.2 已明确「取代旧设计」；但 agreements v3.2.3 是 2026-11-09 14:21 发布的，时间交叠，须用户明示。

## 六、结论汇总

- **一致 / 无需动**：过滤测速主体链路、清空/空匹配短路、日志弹窗外框 271×480 居中、搜索框、安装器 GUI 初始化/Install 段置顶、§8.6/8.7/8.9 补记条款全部已有对应。
- **待实施差距 12 项**：G1–G4（过滤细节）、G6–G10（日志弹窗排版）、G11–G12（安装器置顶两处）；G5 登记为实现层差异不改。
- **待用户裁决 1 项**：第五节 BUG-313 清洁表述 vs 互斥共存方针。
- 本文全部 Mini 行号为 **v3.2.3 tag** 时点；Win 行号为审计时点 HEAD `7834255`，实施时以现行源码复核。