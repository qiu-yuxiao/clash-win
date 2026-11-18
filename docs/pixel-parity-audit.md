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
| P6 | 卡片固定高常量 | 内容驱动：基础卡 141.9 / 主题卡 165.3 / 组合卡 128.8 | 基础 156.4（注释把 `p:1` 当 16 **两侧各算一次**，多算 16）、主题 161.7、组合 128 | 141.9 / 165.3 / 128.8（组合内 60.4 + 52.4） |
| P7 | 主题卡标题 i18n | `theme-settings-card.tsx:164` `settings.components.verge.theme.title`（且同键作 `title=` 提示） | 复用 `...basic.fields.themeSetting`（文案同、键不同，未来漂移会错位） | 改用 `theme.title` + 同键 tooltip |
| P8 | 标题栏下边线颜色 | `_layout.tsx:1195` inline `borderBottom: 1px solid var(--divider-color)`；`use-custom-theme.ts:366-369` 注入 `rgba(0,0,0,.06)` / `rgba(255,255,255,.06)`，**与皮肤无关** | `--theme-border`（retro 浅色 #e2e8f0 / 深色 #333333，且逐皮肤不同） | 恒定 6% 黑 / 6% 白 |
| P9 | 连接面板纵向节奏 | `connections-panel.tsx:110-220`：列 gap 4；第 2/3 项各带 `mt:0.5`=4；搜索框为 MUI `size="small"` TextField（`OutlinedInput.js:113,162` 内距 8.5 → 高 40） | 表头 48、列表 68（搜索框按 20 高、无 mt） | 搜索行 y=28 高 40；表格 y=76；表头带 81..101；数据行 106 起 |
| P10 | 连接表表头 | `connection-table.tsx:80-113`：sticky 在 5px double 外框**内**、高 20、下沿 `borderBottom:5px double`、列间 2px divider、字号 12/字重 600/px 12 | 表头在外框**外**、下沿 1px、列间 1px、字号 11/bold、px 16 | 按左列逐项对齐 |
| P11 | 连接表空态 | `connections-panel.tsx:234-236` 空态渲染 `<BaseEmpty/>` **替代**整张表 | 空态仍画 5px double 外框 | 空态不画外框 |

**验证**：`clashwin_test` 741 cases / 0 failed；`ui_src_gate.ps1` UNRESOLVED=0。
两条锁死旧模型的断言已按新模型重写（`t36` 主题卡高 165.2；`t37` 改为断言「开关中心 = 标签中心」）。

---

## 二、待实证（代码层无法单方面定论，需实机同屏或 Mini 实跑量测）

| # | 项 | 现状 | 为什么不能只靠代码定论 |
| --- | --- | --- | --- |
| V1 | 端口输入框（Mixed Port）外框高 | Win 取 22.7（2+18.69+2） | MUI `OutlinedInput` 根节点内距为 0、fieldset 绝对定位，理论上不加高；但 `input` 行高由浏览器 computed line-height 决定，需实测确认 |
| V2 | 订阅卡（ProfileImportCard）卡颈高 | Win `kProfileCardChromeH`=124.4，其中导入框按 **2 行** 计 46px | Mini `profile-import-card.tsx:139-168` 是 `multiline minRows={1}`（无值、占位符 13 字在 215px 宽内不换行）→ 推测只有 **1 行 ≈ 26.7px**；若成立则卡颈高应为 ~105，整列下方全部上移 19px。**未擅改**：`maxRows={4}` 的 textarea 在 Chromium 下是否因 `rows` 属性回退到 2 行需实跑量测 |
| V3 | MUI Chip 内距（延迟胶囊） | Win 取 4 + icon + 4 + label + 8 | MUI Chip small 的 label/icon 内距是组件内部常量，需按 `@mui/material/Chip` 源码逐一核对 |
| V4 | 文本墨迹垂直中心 | 全站 Label 以 `AlignV::Center` 盒居中 | DirectWrite 与 Chromium 的 half-leading 取整策略不同，逐字体可能差 0.5~1px |

---

## 三、核对顺序（后续会话接着做）

1. 节点列表：`proxy-groups.tsx` / `proxy-head.tsx` / `proxy-item.tsx`（行高 22、字号 12/11、延迟列 65）——已抽查，未见结构性偏差。
2. 快捷栏：`active-node-card.tsx`（28 高、gap 12、断点 580/480/400、名称 maxWidth 120/180）——已抽查一致。
3. 弹窗族：`add-url-dialog.tsx` / `layout-dialogs.tsx` / `antivirus-whitelist-dialog.tsx` / `first-run-guide-dialog.tsx`。
4. 流量面板：`mini-traffic-panel.tsx`（100/135 两档高、内距 3/8）。
5. 皮肤逐套复核：`button-styles.ts` 的 `get3DCardStyle` / `get3DButtonStyle` / `get3DInputStyle` 六套分支。
