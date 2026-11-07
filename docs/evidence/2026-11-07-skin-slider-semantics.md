# Mini 皮肤双滑块（val1 / val2）语义对照表

> 事实源：`ClashVerge` 工作区 **v3.2.2 当前内容**（只读参考，不得改动）
> 本表是 Clash Win 移植「滑块拖动产生实际效果」的唯一依据。

## 0. 变量注入（`pages/_layout/hooks/use-skin-controls.ts:70-93`）

```ts
document.documentElement.style.setProperty('--depth-factor',      depthFactor.toString())
document.documentElement.style.setProperty('--control-skin-val1', depthFactor.toString())
document.documentElement.style.setProperty('--vibrancy-factor',   vibrancyFactor.toString())
document.documentElement.style.setProperty('--control-skin-val2', vibrancyFactor.toString())
```

**四个 CSS 变量两两同值**：`--depth-factor ≡ --control-skin-val1`，
`--vibrancy-factor ≡ --control-skin-val2`。

### 但有两个覆盖例外（`assets/styles/layout.scss`）

| 皮肤 | 覆盖 | 后果 |
|---|---|---|
| `modern-flat` | `--depth-factor: 0 !important`（:329） | val1 **只**经 `--control-skin-val1` 生效（圆角/半径），**不**影响 `--depth-factor` 消费的阴影深度 |
| `original` | `--depth-factor: 0 !important`（:443） | 同上 |

即：这两个皮肤下 **val1 = 纯几何参数**；其余 4 套皮肤下 **val1 同时是几何参数与深度参数**。

### 缺省值（`use-skin-controls.ts:10-24` `readFactor`）

```
saved = localStorage['clash-mini-{skin}-val1']  → 有则用
skin === 'retro-3d' 时回退 legacy 'clash-mini-depth-factor'，再回退 0.3
其余皮肤无历史值 → 1.0
```

- retro-3d：val1 缺省 **0.3**、val2 缺省 **1.0**
- 其余 5 套：val1 缺省 **1.0**、val2 缺省 **1.0**

### 持久化键（`use-skin-controls.ts:54-68`）

```
clash-mini-{skin}-val1 / clash-mini-{skin}-val2        （按皮肤分槽）
clash-mini-depth-factor / clash-mini-vibrancy-factor   （仅 retro-3d 的 legacy 镜像）
```

切皮肤时 `handleSkinChanged`（:37-46）**重新读取新皮肤自己的因子**。

## 1. 逐皮肤语义（`pages/_layout/components/theme-settings-card.tsx:59-134`）

| 皮肤 ID | val1 标签 | val1 max | val2 标签 | val2 max | val1 实际含义 | val2 实际含义 |
|---|---|---|---|---|---|---|
| `retro-3d` | Depth | 1.0 | Vibrancy | 5.0 | 3D 深度（阴影/浮雕位移） | 活力（金色辉光强度） |
| `original` | Radius | 3.0 | Accent | 3.0 | **圆角半径** | **主色色相**（Accent） |
| `modern-flat` | Roundness | 3.0 | Shadow | 5.0 | **圆角半径** | **阴影强度** |
| `frosted-glass` | Opacity | 5.0 | Blur | 5.0 | **玻璃不透明度** | **背景模糊半径** |
| `cyberpunk` | Glow | 5.0 | Speed | 5.0 | **霓虹辉光强度** | **脉冲动画速度** |
| `monochrome` | Radius | 3.0 | Border | 5.0 | **圆角半径** | **边框宽度** |

## 2. val1 / val2 的逐文件消费点

### 2.1 `assets/styles/index.scss`

| 行 | 归属 | 消费 |
|---|---|---|
| 77 | 基线 `.theme-panel` | bg alpha `1 - 0.3*d*v`；blur `16px*d*v` |
| 80-83 | 基线 `.theme-panel` | inset 白 `1d 1d 2d` α`0.12d`；inset 黑 `-1d -1d 2d` α`0.3d`；外阴影 `0 8d 32d` α`0.2d`；外辉光 `0 0 12v` α`0.15v` |
| 88-95 | 基线 `.theme-crystal-card` | bg α`1-0.3d*v`；blur `12px*d*v`；边 α`0.12+0.1d`；inset 白 α`0.15d`；inset 黑 α`0.35d`；外阴影 `0 4d 16d` α`0.1d`；辉光 `0 0 8v` α`0.12v` |
| 128-131 | retro-3d `.theme-panel`（暗） | inset 白 α`0.2d`；inset 黑 α`0.55d`；外阴影 `0 8d 32d` α`0.3d`；辉光 `0 0 12v` 金 α`0.2v` |
| 135 | retro-3d `.theme-crystal-card` | 边框金 α`0.25 + 0.15d` |
| 138-141 | retro-3d `.theme-crystal-card`（暗） | inset 白 α`0.25d`；inset 黑 α`0.6d`；外阴影 `0 4d 16d` α`0.2d`；辉光 `0 0 8v` 金 α`0.18v` |
| 149-152 | retro-3d light `.theme-panel` | inset 白 α`0.65d`；inset 暗金 α`0.3d`；外阴影 α`0.15d`；辉光 α`0.18v` |
| 158-161 | retro-3d light `.theme-crystal-card` | inset 白 α`0.8`（**不乘 d**）；inset 暗金 α`0.35d`；外阴影 α`0.08d`；辉光 α`0.15v` |
| 166 | modern-flat | `--border-radius: calc(8px * var(--control-skin-val1))` ← **val1 是圆角** |
| 173 | modern-flat `.theme-panel` | 外阴影 `0 4v 16v` α`base*v` ← **val2 是阴影** |
| 180-181 | modern-flat `.theme-crystal-card` | 圆角 `calc(6px*val1)`；外阴影 `0 2v 8v` α`base*v` |
| 187-188 | frosted `.theme-panel` | bg α`0.02 + 0.1d` ← **val1 是不透明度**；blur `16px*v` ← **val2 是模糊** |
| 194-195 | frosted `.theme-crystal-card` | bg α`0.01 + 0.06d`；blur `12px*v` |
| 205-206 | cyberpunk keyframes 红（0%） | inset `0 0 4d` α`.25`；外辉光 `0 0 6d` α`.15` ← **val1 是辉光** |
| 211-212 | cyberpunk keyframes 红（100%） | inset `0 0 14d` α`.6`；外辉光 `0 0 20d` α`.45` |
| 220-221 | cyberpunk keyframes 青（0%） | inset `0 0 3d` α`.2`；外辉光 `0 0 4d` α`.1` |
| 226-227 | cyberpunk keyframes 青（100%） | inset `0 0 12d` α`.5`；外辉光 `0 0 16d` α`.35` |
| 236 | cyberpunk `.theme-panel` | 动画周期 `2.5s / (v + 0.001)` ← **val2 是速度** |
| 243 | cyberpunk `.theme-crystal-card` | 动画周期 `2.5s / (v + 0.001)` |
| 258 | monochrome | `--border-radius: calc(8px * var(--control-skin-val1))` ← **val1 是圆角** |
| 268 | monochrome `.MuiChip-root` | 圆角 `calc(6px * val1)` |
| 299 | original | `--primary-main: hsl(calc(239 * var(--control-skin-val2)), 26%, 49%)` ← **val2 是主色色相** |
| 300 | original | `--background-color-alpha: hsla(calc(239*val2), 26%, 49%, 0.1)` |
| 301 | original | `--border-radius: calc(8px * var(--control-skin-val1))` ← **val1 是圆角** |
| 304-312 | original | download 色 `hsl(200*val2,90%,94%)` 等；upload 色 `hsl(38*val2,100%,94%)` 等 |
| 327 | original `.theme-crystal-card` | 圆角 `calc(6px * val1)` |

### 2.2 `assets/styles/layout.scss`

| 行 | 归属 | 消费 |
|---|---|---|
| 20-22 | `.layout::before` 极光 | 三色径向渐变 α×`v` |
| 52-53 | `.layout-content__left` | bg α`1 - 0.2d`；blur `8px*d` |
| 121-128 | `.layout-content__right` | bg α`1 - 0.25d*v`；blur `12px*d*v`；inset 白 α`0.12d`；inset 黑 α`0.3d`；外阴影 `0 4d 16d` α`0.1d`；辉光 `0 0 8v` α`0.12v` |
| 166-167 | `.layout-content__left`（亮） | 同 52-53 |
| 329 | modern-flat | `--depth-factor: 0 !important` ← **例外** |
| 346 | modern-flat `.layout-content__right` | 外阴影 `0 2v 8v` α`base*v` |
| 356-357 | frosted left | bg α`0.02+0.12d`；blur `8px*v` |
| 363-364 | frosted right | bg α`0.01+0.08d`；blur `12px*v` |
| 375-378 | cyberpunk `.layout::before` | 三色 α×`d`；动画 `aurora-move 25s/(v+0.001)` ← **val1 极光强度 / val2 速度** |
| 399 | cyberpunk right | 动画 `cyberpunk-glow-pulse-cyan 2.5s/(v+0.001)` |
| 443 | original | `--depth-factor: 0 !important` ← **例外** |

### 2.3 `utils/button-styles.ts`

| 行 | 函数 | 皮肤 | 消费 |
|---|---|---|---|
| 67-68 | get3DButtonStyle | retro-3d | 辉光色 α`0.15v`/`0.2v` |
| 72-73 | get3DButtonStyle | retro-3d | hover 辉光 α`0.35v`/`0.4v` |
| 81 | get3DButtonStyle | retro-3d | 边框宽 `calc(2px * d)` |
| 91-95 | get3DButtonStyle | retro-3d | bevel `0 1d 0`；`0 2d 0`；`0 3d 2d` α`.35`；辉光 `0 0 8v` |
| 97-101 | get3DButtonStyle | retro-3d（暗） | 同上 α`.6` |
| 114-126 | get3DButtonStyle | retro-3d hover | `0 4d 4d`；辉光 `0 0 12v` |
| 132-136 | get3DButtonStyle | retro-3d active | inset `0 1d 0` |
| 187 | get3DButtonStyle | original | 圆角 `calc(4px * val1)` |
| 257 | get3DButtonStyle | modern-flat | 圆角 `calc(6px * val1)` |
| 264 | get3DButtonStyle | modern-flat | 外阴影 `0 2v 4v` α`base*v` |
| 269 | get3DButtonStyle | modern-flat hover | `0 3v 6v` α`base*2*v` |
| 274 | get3DButtonStyle | modern-flat active | `0 1v 2v` α`base*v` |
| 319 | get3DButtonStyle | frosted | `backdropFilter: blur(8px * v)` |
| 404 | get3DButtonStyle | cyberpunk | 外辉光 `0 0 5d` ← **val1** |
| 407 | get3DButtonStyle | cyberpunk hover | `0 0 10d` |
| 412 | get3DButtonStyle | cyberpunk active | `0 0 2d` |
| 466 | get3DButtonStyle | monochrome | 边框宽 `calc(1px * v)` ← **val2 是边框宽** |
| 257 | get3DCardStyle | modern-flat | 圆角 `calc(6px * val1)` |
| 264 | get3DCardStyle | modern-flat | 外阴影 `0 2v 8v` α`base*v` |
| 319 | get3DCardStyle | frosted | `blur(12px * v)` |
| 404-407 | get3DCardStyle | cyberpunk | 外辉光 `0 0 5d` / hover `0 0 10d` |
| 466 | get3DCardStyle | monochrome | 边框宽 `calc(1px * v)` |
| 545 | get3DCardStyle | retro-3d | 辉光 `0 0 8v` α`0.2v` |
| 557-565 | get3DCardStyle | retro-3d | bevel `0 1d 0`/`0 2d 0`/`0 3d 2d`；辉光 `0 0 8v` |
| 598 | get3DCardStyle | original | 圆角 `calc(8px * val1)` |
| 639 | get3DCardStyle | modern-flat | 圆角 `calc(8px * val1)` |
| 646 | get3DCardStyle | modern-flat | 外阴影 `0 2v 8v` α`base*v` |
| 665 | get3DCardStyle | frosted | `blur(12px * v)` |
| 708-709 | get3DCardStyle | cyberpunk | 外辉光 `0 0 10d` α`.15`/`.2` |
| 721 | get3DCardStyle | monochrome | 边框宽 `calc(1px * v)` |
| 746-752 | get3DInputStyle | retro-3d | inset `0 3d 5d`（亮）/`0 4d 6d`（暗）；边框宽 `calc(2px*d)` |
| 765-773 | get3DInputStyle | retro-3d focus | inset 同上；辉光 `0 0 8v` α`0.35v` |
| 787 | get3DInputStyle | original | 圆角 `calc(4px * val1)` |
| 811 | get3DInputStyle | modern-flat | 圆角 `calc(6px * val1)` |
| 813 | get3DInputStyle | modern-flat | inset `0 1v 3v` α`base*v` |
| 837 | get3DInputStyle | frosted | `blur(6px * v)` |
| 869 | get3DInputStyle | cyberpunk（暗） | 外辉光 `0 0 4d` α`.15` ← **val1** |
| 880 | get3DInputStyle | cyberpunk focus（暗） | `0 0 8d` α`.35` |
| 897 | get3DInputStyle | monochrome | 边框宽 `calc(1px * v)` |
| 906 | get3DInputStyle | monochrome focus | 边框宽 `calc(2px * v)` |
| 935-938 | get3DSegmentedContainerStyle | retro-3d | inset `0 2.5d 4.5d`（亮）/`0 3d 5d`（暗） |
| 954 | get3DSegmentedContainerStyle | original | 圆角 `calc(4px * val1)` |
| 970 | get3DSegmentedContainerStyle | frosted | `blur(8px * v)` |
| 1009-1020 | get3DSegmentedActiveStyle | retro-3d | bevel `0 1d 0`/`0 2d 0`/`0 2d 1px`；inset 白 α`0.7d`/`0.5d`；辉光 `0 0 6v` α`0.2v` |
| 1033 | get3DSegmentedActiveStyle | original | 圆角 `calc(3px * val1)` |
| 1048 | get3DSegmentedActiveStyle | modern-flat | 圆角 `calc(4px * val1)` |
| 1081-1082 | get3DSegmentedActiveStyle | cyberpunk | 外辉光 `0 0 8d` ← **val1** |

### 2.4 `pages/_layout/utils/style-helpers.tsx`（get3DSliderStyle）

| 行 | 皮肤 | 消费 |
|---|---|---|
| 249-297 | retro-3d | rail 固定（**不乘 d**）；track 固定；thumb 阴影 `0 3d 6d`/`0 4d 8d`；inset 白 `0 1d 0`；辉光 `0 0 8v` α`0.25v` |
| 299-306 | retro-3d hover | 阴影 `0 5d 10d`；inset 白 `0 1d 0`；辉光 `0 0 12v` α`0.35v` |
| 307-312 | retro-3d active | inset `1.5d 1.5d 2.5d` α`0.5` |
| 333-360 | original | **无 d/v 消费**（滑块本体不吃因子；主色随 val2 变） |
| 363-392 | modern-flat | rail/track 圆角 `calc(2px * val1)`；thumb 圆角 `calc(6px * val1)` ← **val1 是圆角** |
| 395-437 | frosted | **无 d/v 消费** |
| 452-480 | cyberpunk | **无 d/v 消费**（辉光 5px 固定） |
| 482-508 | monochrome | **无 d/v 消费** |

### 2.5 `components/base/base-switch.tsx`

| 行 | 皮肤 | 消费 |
|---|---|---|
| 66-72 | retro-3d | 槽 inset `0 2d 4d`（亮）/`0 3d 5d`（暗）；亮态加 `inset 0 0 8v` 金 α`.4` + 外辉光 `0 0 8v` α`0.3v`/`0.4v` |
| 94-98 | retro-3d 旋钮 | 阴影 `0 2.5d 5d`/`0 3d 6d`；inset 白 `0 1d 0`；inset 黑 `0 -1d 0`/`-1.5d 0` |
| 111-112 | retro-3d 槽底 | inset `0 2d 4d` α`.25`/`0 3d 5d` α`.7` |
| 118-125 | retro-3d hover | 阴影 `0 4d 8d`/`0 5d 10d`；inset 白；inset 黑；辉光 `0 0 10v` α`0.35v` |
| 188-194 | original | 圆角 `calc(7px * var(--control-skin-val1))` ← **val1 是圆角** |

## 3. 汇总：每个皮肤的 val1 / val2 到底改什么

### retro-3d（Depth / Vibrancy）
- **val1（Depth）**：`.theme-panel` / `.theme-crystal-card` / 按钮 / 卡片 / 输入框 / 分段 / 开关 的 **3D 浮雕深度**
  （inset 位移、外阴影位移与模糊、按钮边框宽、输入框边框宽、旋钮浮雕位移）
- **val2（Vibrancy）**：**金色辉光强度与半径**
  （`.theme-panel` 金辉光、卡片金辉光、按钮金辉光、输入框聚焦金辉光、开关金辉光、旋钮金辉光、极光 α）

### original（Radius / Accent）
- **val1（Radius）**：**所有圆角** —— panel `8*val1`、crystal-card `6*val1`、按钮 `4*val1`、输入框 `4*val1`、分段容器 `4*val1`、分段选中 `3*val1`、开关轨道/旋钮 `7*val1`
- **val2（Accent）**：**主色色相** `hsl(239*val2, 26%, 49%)`；下载卡 `hsl(200*val2,…)`；上传卡 `hsl(38*val2,…)`
- **注意**：`--depth-factor` 被强制 0 → 本皮肤无 3D 阴影深度语义

### modern-flat（Roundness / Shadow）
- **val1（Roundness）**：**所有圆角** —— panel `8*val1`、crystal-card `6*val1`、按钮 `6*val1`、输入框 `6*val1`、滑块 rail/track `2*val1`、thumb `6*val1`
- **val2（Shadow）**：**阴影强度** —— panel `0 4v 16v`、crystal-card `0 2v 8v`、按钮 `0 2v 4v`（hover `0 3v 6v`）、输入框 inset `0 1v 3v`、布局右栏 `0 2v 8v`
- **注意**：`--depth-factor` 被强制 0 → 本皮肤无 3D 阴影深度语义

### frosted-glass（Opacity / Blur）
- **val1（Opacity）**：**玻璃不透明度** —— panel bg α`0.02+0.1d`、crystal-card bg α`0.01+0.06d`、左栏 α`0.02+0.12d`、右栏 α`0.01+0.08d`
- **val2（Blur）**：**背景模糊半径** —— panel `16v`、crystal-card `12v`、按钮 `8v`、输入框 `6v`、分段容器 `8v`、左栏 `8v`、右栏 `12v`
- **注意**：D2D 无 `backdrop-filter` 原语 → 模糊以「提高半透层数」近似，差异须登记

### cyberpunk（Glow / Speed）
- **val1（Glow）**：**霓虹辉光半径与强度** —— 按钮 `0 0 5d`、卡片 `0 0 10d`、输入框 `0 0 4d`、分段选中 `0 0 8d`、panel 脉冲 inset `0 0 4d→14d` / 外辉光 `0 0 6d→20d`、crystal-card 脉冲 inset `0 0 3d→12d` / `0 0 4d→16d`、极光三色 α×d
- **val2（Speed）**：**脉冲/极光动画周期** —— `2.5s/(v+0.001)`、`25s/(v+0.001)`

### monochrome（Radius / Border）
- **val1（Radius）**：**所有圆角** —— panel `8*val1`、crystal-card `6*val1`、chip `6*val1`、分段选中 `4*val1`、按钮 `3*val1`、输入框 `4*val1`
- **val2（Border）**：**边框宽度** —— 按钮 `1px*v`、卡片 `1px*v`、输入框 `1px*v`（focus `2px*v`）
