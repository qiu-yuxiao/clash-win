# 像素级 1:1 代码核对台账

> **性质**：不靠截图，只在**源码层**逐项核对「尺寸 / 绝对位置 / 相对位置 / 字体与字号」四维，
> 记录已确认偏差与修正、待实证项。基线 = Clash Mini **v3.5.3**（`ClashVerge/` 工作区，只读）。
> **口径**：Mini 前端是 React + MUI；`Box` 等原生 `div` **没有全局 `box-sizing: border-box` 重置**
> （`main.tsx`/`_theme.tsx` 未引 `CssBaseline`，`index.scss` 无 `*{}` 规则，MUI `Box`/`styled` 亦不注入），
> 因此 `height:22 + p:'1px' + border:1px` 的外框 = **26**，`height:20` 的外框 = **24**，
> `width:118` 的外框 = **122**。MUI 组件（Button/TextField 等）自带 border-box，按各自组件默认值算。

---

## 一、已确认并修正

| # | 位置 | Mini 事实（file:line） | 修正前 | 修正后 |
| --- | --- | --- | --- | --- |
| P1 | 主题卡页脚虚线 | `theme-settings-card.tsx:457-460` 页脚 `pt:1`(=8) + `borderTop:1px dashed`，页脚是 `p:1` 卡片的**内容子盒** | 虚线画在页脚文字上方 5px，且横跨**整卡宽** | 上方 8px；左右各内缩 8px（只跨内容宽） |
| P2 | 基础卡 4 个开关垂直定位 | `basic-settings-card.tsx:45-53` `gridItemSx` 的 `alignItems:'center'` + `:245-249` `scale(.85)` 配 `transformOrigin:'right center'` | 开关**底沿**对齐标签**墨迹底沿**（DirectWrite 实测，偏离 Mini 约 2px，且随字体/语言抖动） | 开关视觉盒垂直中心 = 行内容盒中心（cy+1.2+21.58/2），与标签盒中心同线 |
| P3 | 基础卡 TUN 嗅探行标签宽 | `basic-settings-card.tsx:361-378` 行内 px 4 + 标签 + gap 4 + 段 122 + px 4 | 标签宽按 −126 预留（少算 4px gap） | −134 |
| P4 | 分段控件（4 处）盒模型 | `theme-settings-card.tsx:186-193`(118×20)、`basic-settings-card.tsx:385-390`(118×20)、`takeover-mode-card.tsx:73`/`routing-preference-card.tsx:66`(height 22)，均 `p:'1px'` + 容器 `border:1px` | 段外框按 118×20 / 高 25.2 布局；描边压在框线上；滑块内缩 1 | 段外框 122×24 / 高 26；描边画在框内；段盒 = padding box 三等分；滑块在段盒内再内缩 1（=外框内缩 2），与 `get3DSegmentedActiveStyle` 的 `left/top:1px` 一致 |
| P5 | 主题卡头行高 | 头行 = max(标题 20.41, 段外框 24) = 24（`theme-settings-card.tsx:152-160`） | 20.4（按标题算，段溢出） | 24 |
| P6 | 卡片固定高常量 | 内容驱动：基础卡 146.2 / 主题卡 166.3 / 组合卡 128.8 | 基础 156.4（注释把 `p:1` 当 16 **两侧各算一次**，多算 16）、主题 161.7、组合 128 | 146.2 / 166.3 / 128.8（逐子项回代，见 P12~P14） |
| P7 | 主题卡标题 i18n | `theme-settings-card.tsx:164` `settings.components.verge.theme.title`（且同键作 `title=` 提示） | 复用 `...basic.fields.themeSetting`（文案同、键不同，未来漂移会错位） | 改用 `theme.title` + 同键 tooltip |
| P8 | 标题栏下边线颜色 | `_layout.tsx:1195` inline `borderBottom: 1px solid var(--divider-color)`；`use-custom-theme.ts:366-369` 注入 `rgba(0,0,0,.06)` / `rgba(255,255,255,.06)`，**与皮肤无关** | `--theme-border`（retro 浅色 #e2e8f0 / 深色 #333333，且逐皮肤不同） | 恒定 6% 黑 / 6% 白 |
| P9 | 连接面板纵向节奏 | `connections-panel.tsx:110-220`：列 gap 4；第 2/3 项各带 `mt:0.5`=4；搜索框为 MUI `size="small"` TextField（`OutlinedInput.js:113,162` 内距 8.5 → 高 40） | 表头 48、列表 68（搜索框按 20 高、无 mt） | 搜索行 y=28 高 40；表格 y=76；表头带 81..101；数据行 106 起 |
| P10 | 连接表表头 | `connection-table.tsx:80-113`：sticky 在 5px double 外框**内**、高 20、下沿 `borderBottom:5px double`、列间 2px divider、字号 12/字重 600/px 12 | 表头在外框**外**、下沿 1px、列间 1px、字号 11/bold、px 16 | 按左列逐项对齐 |
| P11 | 连接表空态 | `connections-panel.tsx:234-236` 空态渲染 `<BaseEmpty/>` **替代**整张表 | 空态仍画 5px double 外框 | 空态不画外框 |
| P12 | **MUI 输入框行高（铁律）** | `InputBase.js:76-78` 根 `...typography.body1` + `lineHeight:'1.4375em'`：em 在**根 16px 字号**下结算为 **23px 绝对值**，输入框 `font:inherit` 继承该绝对值 —— 子元素 inline `fontSize:13px` **不会**把行高缩回 18.7 | 各处按 `fs×1.4375` 反推行高（13→19、14→20、13→18.69） | 统一按 23px 行盒推导所有输入框高度；缺省 small TextField 高 40 = 8.5×2+23 由此得证 |
| P13 | Mixed Port 输入框 | `basic-settings-card.tsx:508-522` inline `width:80 / pt2 pb2 pl4 pr4 / fs13`；输入框是 content-box | 80 × 22.7 | **88 × 27**（80+pl4+pr4；23+2+2），行高 29.4 |
| P14 | 订阅卡导入框 | `profile-import-card.tsx:138-167` multiline + small：根 `p 8.5×2`（`OutlinedInput.js:111-114`）＋文本域 padding 归零（`:167-170`），TextareaAutosize 令 height = shadow 单行 23 + 补回 padding 8 = 31 | 46（按"2 行 13px 文字"估） | **48**（31+17），卡颈 124.4→126.4 |
| P15 | 主题卡页脚虚线 | `theme-settings-card.tsx:431-460` 页脚 `borderTop:1px dashed` 与 `pt:1` 同时存在 | 版式未给虚线预留 1px，版权行顶 = 按钮底+8 | 版权行顶 = 按钮底+**9**；虚线画在页脚盒顶（`copyright.y-9`）；卡高 165.3→166.3 |
| P16 | 新增订阅弹窗几何 | `add-url-dialog.tsx:228-268`：multiline 非 small → 根 `p 16.5×2` + 4 行×23 = **125**；按钮 border-box 高 = `1.75×fs + py×2 + skinBorder×2`；标题 `lineHeight 1.35×15=20.25`、`py 10.8` | 输入框 88（4×20+8）、按钮 34/30、标题 42、py 11 | 输入框 125、按钮 36.35/35.08 + `2×ButtonPaint.border_w`、标题 41.85、py 10.8；卡高 248→294~298（随皮肤边框宽） |
| P17 | **多行输入框框内行距（V6）** | `InputBase.js:76-78` 根 `lineHeight:'1.4375em'` → 每行 **23px**；`OutlinedInput.js:111-114` 根内距 8.5/16.5，文本域自身 `padding:0`。P14/P16 的外框高本来就是按 23/行推的 | `draw_text` 走 DWrite 默认行距（13px ≈ 17.3/行），框内文字比几何紧 5.7px/行；光标步进 19、选区高 19 | `TextStyle.line_h` + `SetLineSpacing(UNIFORM, 23, …)`，基线 `= 首行 baseline + (23 − 首行高)/2`（Chromium 半行距）；订阅卡导入框 / 新增订阅弹窗 4 行文本域、占位符、选区高统一按 23，光标按 23 步进而高取 lineHeight 折算值（13→19）在行盒内居中 |
| P18 | **弹窗族三段几何统一** | `popup-styles.ts:191-246`：DialogTitle `px 1.75(14) / py 1.35(10.8) / borderBottom 1`；DialogContent `px 1.75 / py 1.5(12)`；DialogActions `px 1.75 / py 1.35 / gap 1(8) / borderTop 1`。标题内层 Box 一律 `fontSize 15.5 / lineHeight 1.35`，故标题带 = `10.8×2 + 20.925 + 1 = 43.525`。按钮高 = `fontSize×1.75 + py×2 + 皮肤边框×2` | EditProfile 用 `py 10/12`、标题 15px、输入框 36、按钮 28（两者同高）、卡高写死 372；ClientUpdate 版本行 13px/18、日志框 12.5px+radius 4+内距 8、进度条自造 10% 黑白底且无描边、卡高写死 392；CoreUpdate 版本信息**没有**内嵌盒（`)` 少一层 `p 1.25` 卡）、文字 y 写死 50/72 | EditProfile：标题 15.5/43.525、字段组 `21.5 + 40`、按钮 13px→`37.55` / 12.5px→`33.28`（retro）、gap 8、卡高按内容 376.45；ClientUpdate：12.5px/17.5、日志框 `12px × lineHeight1.5 = 18` + radius 6 + `p 10`、进度条 `subtleSurfaceBg + 1px subtleSurfaceBorder`、卡高 = 内容高（日志框封顶 160）；CoreUpdate：补 `p 1.25 / radius 6` 版本盒 61.8、首行 secondaryText、次行 primaryText bold |
| P19 | **六皮肤 `get3DButtonStyle` 分支缺漏** | `button-styles.ts`：`:428-439` monochrome `error` 有独立色阶（`#333333`/`#cccccc`）；`:230-246` modern-flat 的 hover 与 border 对 `error` 都有分支；`:165` hover = `palette.error.dark`（MUI `augmentColor` → `darken(main, 0.3)`）；`:198` original primary hover 是 `brightness(0.95)`；`:66-68` retro `error` 的 glow 是 `0.15×vibrancy`（非 error 才 0.2） | monochrome `error` 落到 default 分支（白/黑底，与 hover 的 `#ccc/#333` 脱节）；modern-flat `error` hover 跳到 `#e5e7eb`、边框跳灰；`error.main` 三处硬编码亮色 `#FF3B30`（暗色应 `#FF453A`）；`error.dark` 用了 Material 红 900 `#B71C1C`；modern primary hover 只压 0.15（应 0.3）；original primary hover 无变化；retro error glow 用 0.2 | 全部按源码分支补齐：`err = palette_.error_color`、`err_dark = lerp(err, #000, 0.3)`、monochrome/modern 补 error 分支、modern primary hover 0.30、original primary hover `×0.95`、retro error glow `0.15×v` |
| P20 | **`outlined` 变体未接进按钮绘制** | 次要按钮在 Mini 里一律 `get3DButtonStyle(theme,'outlined','default')`（`layout-dialogs.tsx:430` / `add-url-dialog.tsx:262` / `help-menu-button.tsx:257` / `antivirus-whitelist-dialog.tsx:301,313,328` / `first-run-guide-dialog.tsx:790,1060`）。retro-3d 下 outlined 是 `to bottom` 的 **2 站半透明浅金**（`:59-63`）+ 深金描边，与 contained 的实心金渐变完全不同；hover 又变成 **4 站不透明浅金**（`:109-111`） | `Button` 只有 `theme_color_type_`，没有 variant；`utility_modals` 的 EditProfile 用 `button_paint("default")`（contained）→ 取消/保存两个按钮在 retro 下长得一样，只差文字 | `Button::theme_outlined_` + `ButtonPaint::grad_hover_count`（常态 2 站 / hover 4 站）。EditProfile 的 Cancel 改 outlined；插值按较多一侧站数统一、少的一侧用末站补齐 |
| P21 | **连接详情浮层（Popover）结构** | `connection-detail.tsx:56-62`：paper = `getPopupCardStyle` + `maxWidth 520 / maxHeight 480 / overflowY auto / p 2(16)`；`:207-247` 内容只有字段行（根 `fontSize 12` / body1 行高 1.4 / `gap 0.75`）与关闭区（`:196-199` `mt 8 + pt 10 + borderTop 1px` + 右对齐 `size="small"` 按钮）。**没有标题、没有分隔线、没有第二个「关闭」按钮** | 卡内距 14、行高 24（12×1.5+6）、`36 + 13×24 + 34` 的神秘内容高；自绘「查看详情」标题 + 分隔线；底部两个按钮（关闭连接 + 关闭），红色手绘 `#EF5350/#D32F2F`、字号 11 | `p 16`、行盒 `12×1.4 = 16.8` + `gap 6`、删标题与分隔线、删独立关闭按钮（点外部关闭，a11y 子项 3→2）、关闭连接改走 `button_paint("error")`（6 皮肤红底/描边/浮雕/字色）、按钮 13px + `padding 4×10` 右对齐、卡高 = 内容高并封顶 480 |

**验证**：`clashwin_test` 741 cases / 0 failed；`ui_src_gate.ps1` PASS（`UNRESOLVED_FILE=0 / UNRESOLVED_LINE=0`）。
两条锁死旧模型的断言已按新模型重写（`t36` 主题卡高 165.2；`t37` 改为断言「开关中心 = 标签中心」）。

---

## 二、待实证（代码层无法单方面定论，需实机同屏或 Mini 实跑量测）

| # | 项 | 现状 | 为什么不能只靠代码定论 |
| --- | --- | --- | --- |
| V1 | ~~端口输入框外框高~~ | **已定论**：23+2+2 = 27（见 P12/P13） | — |
| V2 | ~~订阅卡卡颈高~~ | **已定论**：输入框 48、卡颈 126.4（见 P14）。原"2 行 46"是巧合近似 | — |
| V3 | ~~MUI Chip 内距~~ | **已核对一致**：small Chip = 图标 `ml4` + 12px 图标 + `mr-4` + label `px8`，文字起点 4+12-4+8=20、右内距 8 —— 与 Win `4+icon+4+label+8` 逐项相等 | — |
| V4 | 文本墨迹垂直中心 | 全站 Label 以 `AlignV::Center` 盒居中 | DirectWrite 与 Chromium 的 half-leading 取整策略不同，逐字体可能差 0.5~1px（不可消除，仅可实测标定） |
| V5 | ~~弹窗 `py:1.35`=10.8~~ | **已定论并铺开**：Add-URL / EditProfile / ClientUpdate / CoreUpdate / AntivirusWhitelist 五处已全部按 10.8 落值（见 P18/P16），标题带统一 43.525 | — |
| V6 | ~~多行输入框内部行距~~ | **已定论**：行盒 23px，渲染层 `SetLineSpacing` + 半行距基线（见 P17）。尚欠一次实机同屏目视复核（几何/步进/选区已按 23 落值，代码层无残项） | — |
| V7 | **Help 入口形态** | 本地 `help_menu.cpp` 用 Win32 `TrackPopupMenu` 原生菜单；Mini 是 `help-menu-button.tsx:123-270` 的 Dialog（标题 15.5 + `MenuList` 菜单项 + 动作按钮，吃 `popup_card_paint`）。结构不是"尺寸偏差"而是"组件不同"，且要接 i18n 菜单项与打开链接动作 | 改成自绘 HelpDialog 是一次交互组件新建（新增 modal 类 + 菜单项 + 外链动作 + a11y），需实机走一遍打开/关闭/点击外链，不适合在纯代码核对里顺手改 |
| V8 | **按钮 hover 态阴影/外发光未表达** | `button-styles.ts` 各皮肤 `&:hover` 的 boxShadow 比常态更深：frosted `0 6px 16px .12`（常态 4/12/.08）、modern-flat `3v/6v/.12v`（常态 2v/4v/.06v）、cyberpunk `0 0 10d`（常态 5d）、retro 额外 `0 4d 4d rgba(0,0,0,.2~.3)`；`.Mui-disabled` 还有各自的 borderColor | `ButtonPaint` 只有一套 `shadow_*` / `glow_*`，`Button::draw_button_bg` 的 hover 只插值底色，阴影与发光不随 hover 变化；disabled 态直接不画边框。要补需给结构体加 `shadow_hover` / `glow_hover` / `border_disabled` 三组字段并让三个绘制点（`button.cpp`、`utility_modals` 的 `paint_btn`、`add_url_dialog`）同步 |

---

## 三、核对顺序（后续会话接着做）

1. 节点列表：`proxy-groups.tsx` / `proxy-head.tsx` / `proxy-item.tsx`（行高 22、字号 12/11、延迟列 65）——已抽查，未见结构性偏差。
2. 快捷栏：`active-node-card.tsx`（28 高、gap 12、断点 580/480/400、名称 maxWidth 120/180）——已抽查一致。
3. ~~弹窗族：`add-url-dialog.tsx` / `layout-dialogs.tsx` / `antivirus-whitelist-dialog.tsx` / `first-run-guide-dialog.tsx`~~ **已完成（P18/P21）**：三段几何与按钮高公式统一，EditProfile / ClientUpdate / CoreUpdate / AntivirusWhitelist / ConnectionDetail 逐处重算。剩 `help-menu-button.tsx` 的 Dialog 形态（V7，需整体重写组件）与 `LogViewerModal`（Mini 无对应物，属 Win 侧增量，不在 1:1 范围）。
4. 流量面板：`mini-traffic-panel.tsx`（100/135 两档高、内距 3/8）。
5. ~~皮肤逐套复核：`get3DCardStyle` / `get3DButtonStyle` / `get3DInputStyle` 六套分支~~ **button 六套已逐条复核（P19/P20）**；card/input/segmented/slider/switch 的六套分支在 P1~P11 与 BUG-384/395 中已落值。剩 hover 态阴影/发光与 disabled 边框（V8）。
