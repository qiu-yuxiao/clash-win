# 2026-11-07 源码出处标注核查记录（src-annotation-audit）

> **性质**：本轮「按主理人裁决更新总纲与过时文档」的只读出处核查台账。记录参数注释所引 `file:line` 是否实存、行号是否正确、Mini 侧是否有显式覆盖，以及 C-4 / C-5 的取证结论。
> **只读声明**：全程只读。Mini 工作区 `ClashVerge` 未改动 / 未移动 / 未覆写任何文件；Win 工作区本轮仅改指定文档（`移植基本准则.md`、`docs/01-目的与方针.md`、`docs/README.md`、`docs/06-验收状态台账.md`、`docs/evidence/2026-11-06-1to1-diff.md`、本文件），未碰 `src/`、`tests/`、`scripts/`、`CMakeLists.txt`、根 `README.md`；未执行任何 git 写操作（无 `add` / `commit` / `push` / `tag` / `-Record` / 打包 / 上传 / 重编译）。
> **取数时刻**：2026-11-07 18:52 +0800。Win `HEAD = f4a1e45`（`f4a1e45407915e90decca6966078e7084bf5e10b`），`git status --porcelain` 共 **31** 项（含本轮文档改动；源码由主修方并行修复）。**工作区仍在漂移，所有「Win 现状」按工作区读取、并在逐条注明与 HEAD 的差异；下一次写入后本快照即过期。**

---

## 1. 核查范围与裁决背景

主理人 2026-11-07 裁决（C-4 方案 2）：保留 Win Tooltip `100ms`，`node_modules/@mui/material/Tooltip/Tooltip.js:331` 是 Mini 工作区内真实 `file:line`，Mini `src/` 全仓 `<Tooltip>` 零 `enterDelay` 覆盖；属总纲 §六 第 8 条自决技术细节，不是 §3.2 冲突。据此要求把出处核查结论落台账，并核出 `use-custom-theme.ts` 中三组的准确行号供修正 `textfield.cpp:340` 标注。

**采信依赖源码的三条约束**（已回写总纲 §5.4「事实源口径澄清」）：① 文件实存；② 行号正确；③ Mini 侧（`src/**`）对该属性无显式覆盖。且不得用它替代 Mini 自身 `src/**` 的显式定义。

---

## 2. Mini 侧实证（逐条 `file:line` + 原文）

### 2.1 `::selection` 与两个 CSS 变量

`ClashVerge/src/assets/styles/index.scss:18-31`：

```scss
18→:root {
19→  --primary-main: #5b5c9d;
20→  --text-primary: #1f1f1f;
21→  --selection-color: #f5f5f5;
...
28→::selection {
29→  color: var(--selection-color);
30→  background-color: var(--primary-main);
31→}
```

- **`--primary-main` 缺省 = `index.scss:19`**（不是 `:20`；`:20` 是 `--text-primary`）。
- **`--selection-color` 缺省 = `index.scss:21`**。
- **`::selection` 规则 = `index.scss:28-31`**：选中文字色取 `--selection-color`，背景取 `--primary-main`，**不透明、无 alpha**。
- **作用域提醒**：`index.scss:299` 有 original 皮肤的 `!important` 覆盖：
  ```scss
  298→html[data-control-skin="original"] {
  299→  --primary-main: hsl(calc(239 * var(--control-skin-val2, 1.0)), 26%, 49%) !important;
  ```
  即 `--primary-main` 在 original 皮肤由 val2 驱动，**胜过** `use-custom-theme.ts:373` 的 JS inline style。写死色值前必须确认作用域（总纲 §六 补充判据已列此坑）。

### 2.2 Tooltip 默认 `enterDelay`

`ClashVerge/node_modules/@mui/material/Tooltip/Tooltip.js:325-337`：

```js
329→    disableInteractive: disableInteractiveProp = false,
330→    disableTouchListener = false,
331→    enterDelay = 100,
332→    enterNextDelay = 0,
333→    enterTouchDelay = 700,
```

- **`enterDelay = 100` = `Tooltip.js:331`**（同文件 `:435-436` 为消费点、`:761` 为 propTypes 声明，均非缺省定义）。
- **Mini `src/` 全仓 `enterDelay` 命中数 = 0**（`grep -rn "enterDelay" src/` 空）。
- **Mini `src/` `<Tooltip>` JSX 用法共 9 处**，逐条均未传 `enterDelay`：
  `src/components/base/base-search-box.tsx:126`、`src/pages/unlock.tsx:430`、
  `src/pages/_layout/components/active-node-card.tsx:281`、
  `src/pages/_layout/components/routing-preference-card.tsx:94/144/194`、
  `src/pages/_layout/components/takeover-mode-card.tsx:95/147/199`。
  注：`grep "<Tooltip" src/` 得 11 行，其中 `src/components/home/enhanced-canvas-traffic-graph.tsx:202/212` 是 `useState<TooltipData>` / `useRef<TooltipData>` 的**泛型类型参数** `<TooltipData>`，不是 `<Tooltip>` 组件，须剔除。
- **结论**：Mini 无显式覆盖 → Tooltip 延时取 MUI 缺省 `100ms`。Win `src/ui/tooltip_w.h:19` 的 `kShowDelayMs = 100` 与之一致，C-4 裁决方案 2 成立。

### 2.3 `use-custom-theme.ts` 运行时覆写 —— 准确行号表（交付主理人）

文件：`ClashVerge/src/pages/_layout/hooks/use-custom-theme.ts`。以下为本轮 `nl -ba` + `grep -n` 逐行核出的结果，**用于修正 `textfield.cpp:340` 的标注**：

| 变量 / 值 | 准确行号 | 原文 | 语义 |
|---|---|---|---|
| `selectColor` 亮/暗字面量 | **`:365`** | `const selectColor = mode === 'light' ? '#f5f5f5' : '#3E3E3E'` | **亮 `#f5f5f5` / 暗 `#3E3E3E` 这两个值的出处** |
| `--selection-color` 绑定 | **`:371`** | `rootEle.style.setProperty('--selection-color', selectColor)` | CSS 变量赋值动作 |
| `--primary-main` 绑定 | **`:373`** | `rootEle.style.setProperty('--primary-main', muiTheme.palette.primary.main)` | 运行时主色（非 `:371`，此前曾被误引） |
| `primaryHex` | `:374` | `const primaryHex = muiTheme.palette.primary.main` | 主色 hex 来源 |
| `primaryRgb` 缺省 / 计算 | **`:375`** / `:376-384` | `let primaryRgb = '91, 92, 157' // default fallback` / hex→rgb 解析 | 主色 rgb 三元组 |
| `--primary-color-rgb` 绑定 | **`:385`** | `rootEle.style.setProperty('--primary-color-rgb', primaryRgb)` | 供 `rgba(var(--primary-color-rgb), …)` 用（消费点 `:264/266/336/338`） |
| `--theme-bg`（original 分支） | **`:421`**（值 `:422`） | `'--theme-bg'` / `mode === 'light' ? '#f5f5f5' : '#121212'` | 主题底色 |
| `--theme-bg-base-rgb`（original） | **`:429`**（值 `:430`） | `mode === 'light' ? '245, 245, 245' : '18, 18, 18'` | theme-bg rgb |
| `--theme-panel-base-rgb`（original） | **`:433`**（值 `:434`） | `mode === 'light' ? '255, 255, 255' : '30, 30, 30'` | 面板 rgb |
| `--theme-bg`（else 分支） | **`:458`**（值 `:459`） | `mode === 'light' ? '#f0f5ff' : '#0f1423'` | 主题底色（非 original） |
| `--theme-bg-base-rgb`（else） | **`:466`**（值 `:467`） | `mode === 'light' ? '240, 245, 255' : '15, 20, 35'` | **theme-bg rgb（此前审计引用的 `:466-468` 即此块，行号正确）** |
| `--theme-panel-base-rgb`（else） | **`:470`**（值 `:471`） | `mode === 'light' ? '255, 255, 255' : '30, 36, 56'` | 面板 rgb |
| `--theme-glass-border-rgb` | **`:503`**（值 `:504`） | `mode === 'light' ? '0, 0, 0' : '255, 255, 255'` | 玻璃描边 rgb |
| 分支条件 | `:419` / `:456` | `if (controlSkin === 'original') {` / `} else {` | 上述两组 theme-bg 值**按皮肤二选一**，不可当全局常量 |

**行号结论与落地状态（2026-11-07 最终裁决后复核）**：
1. 主理人已按本台账把 Win 注释统一为**双行号精确引用** `use-custom-theme.ts:365 取值 → :371 setProperty`，并把 `textfield.h:98` 的 `index.scss:20` 修为 **`:19`**。当前 `textfield.cpp:340-341` / `textfield.h:99-100` 实读如下（见 §3），与 Mini 实证一致，**无需再改**；本轮文档实现未碰任何 `src/`。
2. 采信规则据此定死：亮 `#f5f5f5` / 暗 `#3E3E3E` 两个**值**只在 **`:365`**（`const selectColor`），CSS 变量**绑定动作**只在 **`:371`**（`setProperty('--selection-color', …)`），二者不得互相顶替；此前把值挂到 `:371` 属行号语义不精确，已纠正。
3. `--primary-main` 引 **`:373`**（不沿用 `:371`）；`index.scss:19` 是 `--primary-main` 缺省，`index.scss:20` 是 `--text-primary`，**不可互换**。
4. **G4-e 虚警核实**：Win `src/ui/theme/palettes.cpp:93` 为 `v.selection_color = light ? Color4f::hex(0xf5f5f5) : Color4f::hex(0x3E3E3E);`，与 Mini `use-custom-theme.ts:365` 的亮 `#f5f5f5` / 暗 `#3E3E3E` **逐值一致**，是合法移植、不是无出处常量，**不得改**。出处核查时须区分「值本身有 Mini 依据」与「注释行号写错」两类问题，前者不是缺陷。
5. HEAD 的 `textfield.h:96` 原为 `Color4f::hex(0x007AFF, 0.25f)`（「MUI selection 主色 25%」）且无 `index.scss` 出处——即 25% alpha 自始没有合法出处；工作区已改为 `theme::ThemeService::instance().primary()` 不透明主色底，方向正确。

---

## 3. Win 侧现状（只读取证，未改动；2026-11-07 最终态）

主理人按 §2.3 结论已把注释改为 `:365 取值 → :371 setProperty` 双行号精确引用、`textfield.h:98` 修为 `index.scss:19`。当前实读：

`ClashWin/src/ui/textfield.cpp:339-342`：

```cpp
339→  // src: index.scss:28-31 ::selection 背景 = var(--primary-main)（不透明），
340→  //      选中文字 = var(--selection-color)（use-custom-theme.ts:365 selectColor 亮 #f5f5f5
341→  //      / 暗 #3E3E3E，:371 setProperty('--selection-color')）
342→  selection_bg_ = theme::ThemeService::instance().primary();
```

`ClashWin/src/ui/textfield.h:98-101`：

```cpp
 98→  Color4f selection_bg_ = Color4f::hex(0x5B5C9D);  // src: index.scss:19 --primary-main 缺省 #5b5c9d
 99→  // src: index.scss:29 color: var(--selection-color)（:21 缺省 #f5f5f5；
100→  //      use-custom-theme.ts:365 selectColor 亮 #f5f5f5 / 暗 #3E3E3E → :371 setProperty）
101→  Color4f selection_text_ = Color4f::hex(0xF5F5F5);  // src: index.scss:21
```

- 与 HEAD 对比：`HEAD:textfield.cpp:339-340` 原为 `selection_bg_ = Color4f{p.border_focused.r, … 0.25f}; // ← MUI selection 主色 25%`；`HEAD:textfield.h:96` 原为 `Color4f selection_bg_ = Color4f::hex(0x007AFF, 0.25f)`。**工作区现为不透明主色底 + `--selection-color` 文字色，与 Mini `index.scss:28-31` 一致，且出处行号逐条可核。**
- **本轮文档实现未改任何 Win 源码**；上述注释精确化由主理人执行，此处只作只读取证与台账登记。

---

## 4. C-5「终端风日志去留」取证（供主理人决定是否动代码）

**差异具体指什么**：我最初在 C 类自编里报的 C-5 是「Win 的日志查看器自加了一层终端风视觉」——即把日志页画成**黑底 / 等宽字体 / 整行 ANSI 式着色**的控制台观感，且当时 `utility_modals.cpp` 里写死 `#FFFFFF` 白底 + `#1A1A1A` 深字（暗色模式下刺眼），并以 `// 豁免: Mini 无对应物 — 本查看器为 ClashWin 自加的终端风日志查看器` 自我登记。Mini 侧并无此物：日志是 MUI `Dialog`（paper `className='theme-panel'`）+ `<LogsPage />` 的普通主题化列表。按总纲 §一 `:21`「不新增页面」与 §六 `:154`「Mini 没有 → 就不加戏」，自加终端风属自编，故当时定级 P2、列为「需用户裁决是否删除」。

**Mini 侧 `file:line`**：
- `ClashVerge/src/pages/_layout/components/layout-dialogs.tsx:556-598`（Dialog）：`:562-563` `paper.className = 'theme-panel'`；`:565` `background: 'var(--theme-bg, var(--background-color))'`；`:596` `{logsOpen && <LogsPage />}`。**无黑底、无终端色字面量**。
- `ClashVerge/src/components/log/log-item.tsx:8-39`（日志项）：`:12` `borderBottom: 1px solid palette.divider`；`:14` `fontFamily: typography.fontFamily`（**非等宽**）；`:16-17` `.time` → `palette.text.secondary`；`:27-35` `.type` → `palette.error.main` / `warning.main` / `info.main`；`:36-38` `.data` → `palette.text.primary`（**逐段着色，非整行终端色**）。
- `ClashVerge/src/pages/logs.tsx`、`src/components/log/` 全目录 grep `monospace|consolas|terminal|#00ff|ansi` 仅命中 `logs.tsx:150` 的 `transition: 'transform 0.2s ease'`（与终端配色无关）。

**Win 侧现状（关键更正，请主理人据此定夺）**：**「终端风」视觉层在工作区及 HEAD 中都已被主修方移除，已不需要删代码。** `ClashWin/src/ui/scenes/utility_modals.cpp` 当前：
- `:1050-1062` 卡片底座走 `ThemeService::panel_paint()` 主题查表，注释明写「此前写死 `#FFFFFF` 白底 + `#1A1A1A` 深字 → 暗色模式下是刺眼的白卡」已修；
- `:1156-1160` 日志区底色 `vars().theme_panel_bg` + `theme_border`，注释「日志项底色随页面（无终端暗底）」；
- `:1179` `fontFamily: typography.fontFamily（非等宽终端字体）`；
- `:1187-1206` 时间 `pal.secondary_text` / 级别 `pal.error_color|warning_color|info_color` / 正文 `pal.primary_text` **逐段着色**，注释「无整行终端色」。
- 残留问题只剩 **`utility_modals.cpp:1042-1043` 那条过时且自相矛盾的豁免注释**：仍写「豁免: Mini 无对应物 — 本查看器为 ClashWin 自加的终端风日志查看器，Mini 日志页无终端配色字面量（出处: layout-dialogs.tsx:565-594 无终端配色）」。它既宣称「Mini 无对应物」，下方 `:1051`、`:1156`、`:1179`、`:1187` 却逐条引用 Mini `layout-dialogs.tsx` / `log-item.tsx` 并按其实现绘制；且「终端风」已不复存在。

**给主理人的判断建议**：C-5 的**实质代码偏差已闭环**（终端配色字面量零残留，绘制已按 Mini 主题化），无需删除视觉功能、也无需用户裁决豁免；**唯一待办是把 `:1042-1043` 的豁免注释改掉**（改为指向 `layout-dialogs.tsx:556-598` + `log-item.tsx:8-39` 的正向出处，或删除该「豁免」声明），否则 `ui_src_gate.ps1` 会把「豁免 / Mini 无对应物」当成合法放行依据，与真实实现对不上。此项属 `src/` 改动，**按本轮禁令我未动**，交主理人决定。

---

## 5. 结论与未覆盖项

1. **C-4 裁决成立**：`Tooltip.js:331 = enterDelay = 100`，Mini `src/` 零 `enterDelay` 覆盖（9 处 `<Tooltip>` 均不传），三条约束（文件实存 / 行号正确 / Mini 侧无显式覆盖）全部满足；属 §六 第 8 条，非 §3.2。
2. **`index.scss:19 / :21 / :28-31`** 与 **`use-custom-theme.ts:365 / :371 / :373 / :385 / :421 / :429 / :433 / :458 / :466 / :470 / :503`** 为准确行号，可用于修正 Win 注释；`index.scss:20` 为 `--text-primary`，不可作 `--primary-main` 出处。
3. **C-5 实质偏差已闭环**，仅余 `utility_modals.cpp:1042-1043` 过时豁免注释，建议主修方改为正向出处。
4. **未覆盖 / 需后续**：本台账只核上述几组标注，**未由本轮发起全仓出处复核**（主理人已令「文档写完后不再做全仓审计」）。qa-engineer 的门禁加固任务（#4）已跑通 `ui_src_gate.ps1 -VerifySources`，其输出的引用解析结论作为**外部旁证**增量登记于 §6，本轮仅复核与登记、不重跑全仓审计。冻结重跑完成后，主理人将安排一次独立复核。
5. 本轮所有文档改动均**未写入 git**（无 `add` / `commit` / `push`）；冻结发布对象仍未成立。按 2026-11-07 最终裁决的冻结顺序由主理人执行：**写完 → 单次冻结提交 → 从干净 HEAD 重建 → 全量重跑 → 重挂证据 → 发布**（「停止写入」与用户「一直做下去」直接抵触，不采纳）。

---

## 6. `-VerifySources` 输出复核（qa-engineer 提供，2026-11-07 增量登记）

> **来源**：qa-engineer 在门禁加固任务（#4）中跑通 `scripts\ui_src_gate.ps1 -VerifySources`（MiniRoot=`ClashVerge`，真实 `src/ui`，默认模式 PASS / EXIT=0）。**计数有两个时点**：`refPat` 修复前 `RESOLVED=622 / UNRESOLVED=59`，#4 修复后 `RESOLVED=657 / UNRESOLVED=24`（A/B 见 §6.2）；本节修复前的清单数据按「旧文保留作时间线」惯例保留，并逐处标明时点。本节为**对本台账 §2 的交叉验证与增量登记**：我方只读复核其两组异常数据、未改 `scripts/`、未改 Win 源码、未执行全仓审计。

### 6.1 7 条「引用范围末端超出 Mini 文件当前行数」——全部核实成立

`wc -l` 逐条与 qa 数据一致，Win 侧注释原文逐条实读。定性为**历史标注按当时版本行号书写、Mini 侧后续删行**导致的 `end` 越界；文件本身可定位（纯文件名索引命中），属 `UNRESOLVED_LINE` 而非 `UNRESOLVED_FILE`。**按 qa 建议：不必改 Win 源码，默认门禁不阻断。**

| Win 出处 | 引用范围 | Mini 文件（均在 `ClashVerge/`） | 实际行数 | 末端超出 |
|---|---|---|---|---|
| `src/ui/scenes/connections_panel.cpp:450` | `base-empty.tsx:19-45` | `src/components/base/base-empty.tsx` | 40 | +5 |
| `src/ui/scenes/node_list.cpp:246` | `scroll-top-button.tsx:10-40` | `src/components/layout/scroll-top-button.tsx` | 36 | +4 |
| `src/ui/scenes/node_list.h:65` | `scroll-top-button.tsx:10-40` | 同上 | 36 | +4 |
| `src/ui/scenes/quick_bar.cpp:709` | `button-styles.ts:1105-1123` | `src/utils/button-styles.ts` | 1122 | +1 |
| `src/ui/scenes/settings_drawer.cpp:293` | `pages/_layout/utils/style-helpers.tsx:241-467` | `src/pages/_layout/utils/style-helpers.tsx` | 464 | +3 |
| `src/ui/notice_handler.cpp:97` | `hooks/use-service-installer.ts:19-46` | `src/hooks/use-service-installer.ts` | 44 | +2 |
| `src/ui/switch_w.cpp:89` | `base-switch.tsx:15-556` | `src/components/base/base-switch.tsx` | 550 | +6 |

### 6.2 i18n 出处「`locales/zh/*.js` 约 30 处 Test-Path=False」（修复前快照）——门禁抽取层假红，非 Mini 缺文件；已由 #4 闭环

**结论：既不是 Mini 文件缺失，也不是 Win 注释写错后缀，也不是相对路径基准偏差，因此不登记为偏差、不修 Win 引用。**

- **Mini 真实 i18n 落点 = `ClashVerge/src/locales/zh/*.json`**（不是 `zh-CN`、不是 `src/assets/locales`、不是 `.js`）。目录下 `connections / shared / layout / proxies / tests / settings / logs / home / profiles / rules .json` 与 `index.ts` 全部实存；`src/locales/zh/index.ts` 以 `import connections from './connections.json'` 逐项聚合。qa 点名的 7 个 `Test-Path` 全为 True。
- **Win 侧 32 处引用全部为 `.json`、零 `.js`**，且 HEAD 与工作区一致（`locales/zh/*.json\b` = 32/32，`*.js\b` = 0/0，`grep -F '.js:'` = 0）→ **排除工作区漂移**。实读例：`help_menu.cpp:118` = `locales/zh/layout.json:32-39`、`connections_panel.cpp:105` = `locales/zh/connections.json:37-39`、`quick_bar.cpp:137` = `locales/zh/settings.json:557`。
- **门禁回退链本可解析**：`Test-SourceRef`（`ui_src_gate.ps1:97-104`）的 cands 第 2 项 `MiniRoot + 'src\' + norm` 恰为 `src/locales/zh/layout.json`，存在。
- **根因（Python 等价复现验证；行号按修复前版本）**：修复前 `ui_src_gate.ps1:50` 为 `$refPat` 定义行（311 行版本）；**当前树 `:50` 已是防回退注释、定义在 `:54`**（qa 加了 3 行注释后下移）。修复前定义为
  ```powershell
  $refPat = '([A-Za-z0-9_.@/-]+\.(?:scss|css|tsx?|jsx?|json|md|h|cpp))(?::(\d+)(?:-(\d+))?)?'
  ```
  扩展名 alternation 中 **`jsx?` 排在 `json` 之前**，`jsx?` 先吃掉 `.json` 的 `js` 两字符，token 被截为 `locales/zh/layout.js`，且 `:32-39` 因 `on` 阻隔无法被行号组捕获（`ln=None`），于是报表呈现「`.js` + File 未解析」。
- **最终修法与落地状态（2026-11-07，qa-engineer #4 已闭环；以 qa 最终措辞为准）**：`scripts/ui_src_gate.ps1:54` 最终**只把 `json` 提到 `jsx?` 之前**
  ```powershell
  $refPat = '([A-Za-z0-9_.@/-]+\.(?:scss|css|tsx?|json|jsx?|md|h|cpp))(?::(\d+)(?:-(\d+))?)?'
  ```
  防回退注释写在 `:50-53`。**未**拆分 `tsx|ts`、**未**加 `hpp`、**未**加 `(?![A-Za-z0-9_])` 断言 —— 理由：`tsx?` / `jsx?` 是单分支内可选 `x`，对 `.tsx`/`.ts`、`.jsx`/`.js` 天然无前缀截断（前缀截断只在「拆成独立分支且短分支在前」时发生）；且纯正则全量对照 **CUR/FIX 总 token 同为 1710**，证明最小修法只纠正 token 内容、未扩大匹配面，避免新增扩展名引入不可控假红 / 假绿。
  - 本台账上一稿曾建议「长后缀优先整体排序或加边界断言」，属**未采纳的备选**，此处按 qa 最终方案更正，不再作为建议保留。
  - 我上一稿写的「当前树仍未落地」**已作废**（该结论取自 qa 修复前的 311 行快照，与 qa 当轮修复消息并行）：经只读复核，当前 `refPat` 已为上述修复态、`:50-53` 防回退注释存在。**磁盘指纹**（本轮直判，与 qa 交付值完全一致、无并发覆盖）：SHA256 `4906880FDE2C77181D24BFED44824627AE32E178DE8320ABC5A3DC60FA3BC947`、317 行 / 18,268 B、mtime `2026-11-07 19:45:14.217`、`git status` 仅 ` M`。该假红**已闭环**，不再是存量弱项。
  - **时序说明**：qa 收到我「i18n `.js` 是门禁正则假红」那条后当轮即落地修复并给出 A/B；我此前两条「复核」消息与修复并行发出、落在修复之前，故其 `622 / 59 / xxx.js:-` 数据属 **OLD（旧 `refPat` 顺序）那一列**，不是当前树。本台账已按 qa 最终口径改到 NEW 列，历史数据仅作时间线保留并逐处标注。
- **A/B 效果（qa 实测，唯一变量 = `refPat` 顺序，同一脚本各跑一次 `-VerifySources`，显式 `-Root`/`-MiniRoot`）**：`RESOLVED 622→657 (+35)`、`UNRESOLVED_FILE 52→17 (−35)`、`UNRESOLVED_LINE 7→7`、`NON_MINI_REF 5→5`、`SHORTHAND 301→301`、清单总数 `59→24`、`locales/*.js` 假红 `31→0`、默认 `EXIT=0→0`。**修复后当前磁盘重跑的全量计数**（qa 提供）：`REFS RESOLVED=657 RESOLVED_NOLINE=55 UNRESOLVED_FILE=17 UNRESOLVED_LINE=7 NON_MINI_REF=5 SHORTHAND=301`，`UI_SRC_GATE: PASS / EXIT=0`；`LOCALES_JS_RESIDUAL=0`、`DASH_LN_TOKEN=0`（`xxx.js:-` / `:-` 形态彻底消失）。去重后 17 个文件一一对应 `.js→.json`，除 i18n 外还覆盖 `window_state.json` / `clashwin-skin.json` / `skin.json` / `last_non_large_size.json` / `shared.json` 等 Win 侧 JSON 配置引用（同族假红，一并修复）；修后 32 处 `locales/zh/*.json` **全部 RESOLVED**（qa 实测 `layout.json` 128 / `connections.json` 53 / `settings.json` 581 / `shared.json` 144 行等可指到行）。**结论**：这 32 处不登记为「历史相对路径偏差」，也不必修 Win 引用，按 §6.2 判断归「`-VerifySources` 抽取层假红，已由 #4 门禁加固闭环」。
- **qa 主动撤回早期定性（按 qa 要求不保留其旧定性）**：qa 已明确撤回「`locales/zh/*.js` 在 Mini 工作区根本不存在，属真实失效引用」一说，并确认我用「清单行号全丢反证 + `Test-Path` 实测」给出的结论正确 —— 这是 `$refPat` 抽取层假红，不是 Win 出处失效、也不是 Mini 缺文件；`MiniRoot\src\locales\zh\` 全 True 的实测在 qa 侧同样成立。**双方口径已一致收敛，台账不再保留该旧定性作为有效结论**；下条只保留取证方法与反证链的说明。
- **`-File` 一处不作缺陷登记**：`ui_src_gate.ps1:134-136` 的 `-File` 是索引构建的防御性对齐（门禁用的 `-Path` 本就无目录泄漏），非已发生缺陷 —— 按 qa 口径仅作说明，不列为缺陷。
- **qa 完整清单的决定性反证**（`%TEMP%\cw_final_verify_out.txt:60-121`，qa 2026-11-07 提供）：清单里全部 i18n `UNRESOLVED_FILE` 条目一律写作 `locales/zh/xxx.js:-` —— **token 后跟 `:-`，即行号组取 `-`（`ln=0`）**。而 Win 原文是 `locales/zh/layout.json:32-39` / `shared.json:52` 等**带真实行号**。若 Win 真写成 `.js` 且文件不存在，报表应保留 `:32` 而只报 File 未解析；**行号整体丢失恰恰只能由「`jsx?` 截断 `.json` 后，`:32-39` 被 `on` 阻隔、行号组无法捕获」解释**（与 §6.2 根因的 Python 复现 `ln=None` 完全吻合）。故其性质为：**解析口径问题（`$refPat` 抽取层），只是表现为 `UNRESOLVED_FILE`**；Win 引用本身合法、可解析。qa 已主动撤回早期「真实失效引用」旧定性，双方口径一致（见上文 A/B 段）。
- **用 qa 相同方法（`Test-Path`）直判磁盘的最终事实**（双方实测一致）：以 `MiniRoot=ClashVerge` → `MiniRoot\locales` = **False**（顶层无）、**`MiniRoot\src\locales` = True**、`MiniRoot\src\locales\zh` = True、7 个 `src/locales/zh/*.json` **全部 True**。即 Mini 的 i18n 落点在 `src/locales/zh/`，文件齐备；且门禁回退链第 2 项（`ui_src_gate.ps1:100` `MiniRoot + 'src\' + norm`）本就为命中该目录而设，token 修正后直接 RESOLVED、无需 basename 回退。qa 不做全局 basename 回退以免把 `docs/node_modules` 的 eslint `shared.js` 误判为出处，该取舍认可。
- **修复前 52 条其余 `UNRESOLVED_FILE` 的抽样核实（并非「Mini 下无对应物」；修复后已降至 17，见上文 A/B）**：
  | 清单 token | Win 原文实证 | 真实成因 |
  |---|---|---|
  | `action.h` | `connections_panel.cpp:531` = `bgcolor: 'action.hover'` | 正则把 `action.hover` 的 `.h` 当扩展名 `h` 截断 |
  | `layout.components.h` / `miniTable.h` | `settings_drawer.cpp:826` = `tr("layout.components.helpMenu.buttonAriaLabel")` | i18n key 中的 `.h` / `.miniTable.` 被误当文件名 |
  | `settings.js` / `layout.js` / `shared.js` | `quick_bar.cpp:30` = `settings.json:541-543`；`connections_panel.cpp:450` = `（shared.json:52）` | 与 30 处**同源**：`.json`→`.js` 前缀截断 |
  | `useSlider.js:326-370` | `slider.cpp:107` = `← MUI useSlider.js:326-370 createHandleHiddenInputKeyDown` | Mini `node_modules/.pnpm/@mui+material@…/Slider/useSlider.js` **实存**，是合法 MUI 出处；因 pnpm 符号链接 PS5.1 `-Recurse` 不跟随（脚本 `:80` 已自述）而索引漏检，与 §2.2 `Tooltip.js:331` 同类 |
  | `app.h` | `main_window.cpp:48` = `← T01 app.h consts::kAppWakeup` | 确为 Win 侧 `src/app/app.h` 自引用，qa 的 `NON_MINI_REF` 归类对这一条成立 |
  结论（**修复前快照**）：52 条中相当一部分是**抽取层误判**，不全是「缩写 / Mini 无对应物」；按裁决默认模式只披露不阻断，此处仅登记事实供独立复核，不升级定级。该族假红已由 #4 修复闭环（52→17、i18n 31→0），见 §6.2 A/B。
- **修后剩余 17 条 `UNRESOLVED_FILE` 全文（qa 提供，逐条在列；我方上表抽样判断逐条被确认）**：
  ```
  add_url_dialog.cpp:61        fade_.h:-
  connections_panel.cpp:531    action.h:-
  connections_panel.cpp:573    miniTable.h:-
  connections_view.cpp:39      action.h:-
  profile_import_card.cpp:40   action.h:-   （同文件 2 处）
  profile_import_card.cpp:155  action.h:-
  quick_bar.cpp:228            s.h:-
  settings_drawer.cpp:826      layout.components.h:-
  traffic_pane.cpp:348         action.h:-
  context_menu.h:40            action.h:-
  main_window.cpp:48           app.h:-
  main_window.cpp:169          window_state.json:-
  modal.cpp:80                 fade_.h:-
  slider.cpp:107               useSlider.js:326-370
  slider.cpp:119               useSlider.js:182
  title_bar.h:68               action.h:-
  ```
  **计数说明**：上方代码块为 16 行，其中 `profile_import_card.cpp:40` 标注「（同文件 2 处）」，故合计 **17 条**，与 `UNRESOLVED_FILE=17` 一致；不是漏项。
  `action.h:-` ×7 ← `bgcolor: 'action.hover'` 的 `.h` 被当扩展名；`layout.components.h:-` / `miniTable.h:-` ← i18n key；`useSlider.js` 两条 ← pnpm 符号链接致索引漏检，合法 MUI 出处、与 `Tooltip.js:331` 同类；`app.h:-` ← Win 侧自引用，`NON_MINI_REF` 归类成立。上一稿「未逐条追、不作断言」的三类现补齐落点：`window_state.json:-`、`fade_.h:-` ×2、`s.h:-`。**这 17 条仍是默认不阻断的存量弱项**；qa 按「默认只披露」口径未继续收窄抽取规则（避免扩大改动面、超出派单范围），与本台账判断一致，不升级定级。
- ⚠️ **取证陷阱留痕**：用 `grep -oE '\.js'` 或 `\.(js|json)` 统计会命中 `layout.json` 的前三个字符而给出「.js 32 处」的假象（本轮首次统计即踩此坑）；须用词边界 `\b` 或固定字串 `-F` 判定。这与 §2.2 剔除 `<TooltipData>` 泛型假命中属同一类「字面量前缀误判」。

### 6.3 交叉验证与门禁口径

- 修复后 **657 条 RESOLVED**（修复前 622）与 §2 的主理人勘误（`index.scss:19/21/28-31`、`use-custom-theme.ts:365/371/373`、`node_modules/@mui/material/Tooltip/Tooltip.js:331`）一致，可作「出处可验证」的外部旁证；本台账 §2 仍以逐行 `nl -ba` 实证为准。qa 另实核 `index.scss:19` 与 `Tooltip.js:331` 两条均成立，与 §2 逐行复核结果一致。
- **`node_modules` 必须直判磁盘**：`node_modules/` 被 `.gitignore` 忽略，Glob / `git ls-files` 会漏检，但 `Test-Path` = True（qa 实测口径）。§2.2 的 `Tooltip.js:331`、§6.2 的 `useSlider.js` 均属此类；出处校验不能只走文件索引，须按 `MiniRoot` 直判磁盘。本条与总纲 §5.4「事实源口径澄清」的三条约束配套。
- **UNRESOLVED 清单构成（修复前 → 修复后；当前值以修后为准）**（修复前快照 `cw_final_verify_out.txt:62-121`）：总数 `59 → 24`，**修后 `24 = 17 UNRESOLVED_FILE + 7 UNRESOLVED_LINE`**。`UNRESOLVED_LINE 7 → 7`（即 §6.1 表格，逐条对齐，**完整保留、未改 Win 源码**）；`UNRESOLVED_FILE 52 → 17`（其中约 30 条为 §6.2 的 `.json`→`.js` 截断假红，已随 `refPat` 修复转 RESOLVED；剩余 17 条全文见 §6.2，属抽取层误判的存量弱项）；另有 `NON_MINI_REF 5 → 5` **另计、不进 24**；`SHORTHAND` 简写行号 `301 → 301` **不是违规项**、另列；`RESOLVED_NOLINE=55` 为无行号的合法引用、不计入未解析。**原 `59 / 52 FILE / 622` 是修复前（OLD）值，标注为「修复前」而非当前值**；修后当前树 `RESOLVED=657 / UNRESOLVED=24`、`LOCALES_JS_RESIDUAL=0`、`DASH_LN_TOKEN=0`。**默认模式只披露、不阻断（PASS / EXIT=0 前后均 0），`-Strict` 下才阻断，不打断冻结重跑。**
- **`-Strict` 对当前树仍 FAIL**（存量弱项 `18+33+26+24`，末项随清单 59→24 同步下降），按 qa 补充口径：**不写入冻结前 CI 门禁**，默认模式 PASS 即为当前验收口径；本项不升级为发布阻断项，已由 qa 同步主理人。
- 本节涉及 `scripts/ui_src_gate.ps1` 的修复由 **qa-engineer 在其 #4 派单范围内执行并闭环**（全文件仅 `:54` 一处 `$refPat` 定义、`:50-53` 为防回退注释；最终 SHA256 `4906880FDE2C77181D24BFED44824627AE32E178DE8320ABC5A3DC60FA3BC947`、317 行 / 18,268 B、mtime `2026-11-07 19:45:14.217`、`git status` 仅 ` M`，可供冻结复核）；我方本轮未改该文件（`scripts/` 对我是禁改区），仅作只读取证与台账登记。#4 保持 completed、不重开、不返工。落地状态见 §6.2。
