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

**验证**：`clashwin_test` 741 cases / 0 failed；`ui_src_gate.ps1` UNRESOLVED=0。
两条锁死旧模型的断言已按新模型重写（`t36` 主题卡高 165.2；`t37` 改为断言「开关中心 = 标签中心」）。

---

## 二、待实证（代码层无法单方面定论，需实机同屏或 Mini 实跑量测）

| # | 项 | 现状 | 为什么不能只靠代码定论 |
| --- | --- | --- | --- |
| V1 | ~~端口输入框外框高~~ | **已定论**：23+2+2 = 27（见 P12/P13） | — |
| V2 | ~~订阅卡卡颈高~~ | **已定论**：输入框 48、卡颈 126.4（见 P14）。原"2 行 46"是巧合近似 | — |
| V3 | ~~MUI Chip 内距~~ | **已核对一致**：small Chip = 图标 `ml4` + 12px 图标 + `mr-4` + label `px8`，文字起点 4+12-4+8=20、右内距 8 —— 与 Win `4+icon+4+label+8` 逐项相等 | — |
| V4 | 文本墨迹垂直中心 | 全站 Label 以 `AlignV::Center` 盒居中 | DirectWrite 与 Chromium 的 half-leading 取整策略不同，逐字体可能差 0.5~1px（不可消除，仅可实测标定） |
| V5 | 弹窗 `py:1.35`=10.8 | 已按 10.8 落值（`add_url_dialog`） | 其余弹窗族仍取整为 11（差 0.2px×2），属不可见级，改动需连带重算各处卡高 |
| V6 | **多行输入框内部行距** | Win 的 `draw_text` 用 DWrite 默认行距（13px Segoe UI ≈ 17.3/行），Mini 是 23/行。外框几何已按 23 推导（P14/P16），但**框内文字行距仍偏紧** | 需在 `D2dRenderTarget::draw_text` 加 `SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, 23, …)`；leadingBefore 的取值要与 Chromium 的 half-leading 对齐，属渲染层改动，须同屏验证后再落 |

---

## 三、核对顺序（后续会话接着做）

1. 节点列表：`proxy-groups.tsx` / `proxy-head.tsx` / `proxy-item.tsx`（行高 22、字号 12/11、延迟列 65）——已抽查，未见结构性偏差。
2. 快捷栏：`active-node-card.tsx`（28 高、gap 12、断点 580/480/400、名称 maxWidth 120/180）——已抽查一致。
3. 弹窗族：`add-url-dialog.tsx` / `layout-dialogs.tsx` / `antivirus-whitelist-dialog.tsx` / `first-run-guide-dialog.tsx`。
4. 流量面板：`mini-traffic-panel.tsx`（100/135 两档高、内距 3/8）。
5. 皮肤逐套复核：`button-styles.ts` 的 `get3DCardStyle` / `get3DButtonStyle` / `get3DInputStyle` 六套分支。
