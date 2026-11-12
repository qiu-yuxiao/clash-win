# Clash Win 已知限制（Known Limitations）

本文档为常青清单，如实记录当前发布版本在工程交付、原生形态与环境边界上的已知限制。

## (a) 安装包未附带公网商业 Authenticode 代码签名
发布产物采用无商业证书构建口径（CI 传入 `-SkipSign`，签名状态记录于随包 `SIGNATURE_STATUS.txt`，不阻断发布；与上游 Clash Mini 一致）。
首次运行安装器或便携版时，Windows SmartScreen 可能提示「未知发布者（Unknown publisher）」。请在运行前比对随 Release 附带的 `SHA256SUMS.txt` 校验文件完整性（应用内自动更新则额外通过内置 Minisign Ed25519 公钥强验签）。

## (b) sidecar 5 个上游二进制未纳入 Git 源码树
`win-mihomo.exe`、`clash-verge-service*.exe`、`enableLoopback.exe` 因 `.gitignore` 的 `*.exe` 规则未提交进版本库：
- 本地从源码全新克隆后需按 [`docs/05-sidecar-内核资产.md`](05-sidecar-内核资产.md) 放置 5 个上游二进制并通过 `scripts/verify_sidecar.ps1` 5/5 哈希校验；
- CI 构建自动从受控归档还原（若归档缺失 `win-mihomo.exe` 则从 `MetaCubeX/mihomo` v1.19.33 补齐并强校验 SHA256 基线），确保产出的 `setup.exe` 与 `portable.zip` 始终内置完整内核；运行期若发现内核缺失亦支持自动恢复。

## (c) 应用内自动更新依赖 minisign 私钥配置
客户端内置 minisign 公钥验签硬门禁（`src/config/update_keys.h`）。GitHub Actions 在配置了仓库密钥 `MINISIGN_PRIVATE_KEY` 时会自动为 `setup.exe` 生成签名并写入 `app-update.json`；若第三方复刻（Fork）仓库未配置对应密钥，应用内下载安装会因缺签名而拒绝执行（手动下载安装包覆盖升级不受影响）。

## (d) Direct2D 毛玻璃背景模糊近似（`frosted-glass` 皮肤）
Direct2D 1.0 无 Web 引擎 CSS `backdrop-filter: blur()` 原语，`frosted-glass` 皮肤采用多层半透明玻璃底色、高光双线边框与随滑块动态插值的柔光阴影进行原生近似呈现。

## (e) 全新干净 Windows 机器边缘环境反馈
日常开发与回归主要在装有开发工具链及与 Clash Mini 交替共存的 Windows x64 环境完成；对于从未安装过任何同类客户端的全新 Windows 10/11 环境，UAC 提权与 Windows 服务首次注册路径如遇异常欢迎通过 Issue 反馈实机表现。

## (f) 少量低优先级瞬态界面缓存未单独落盘
窗口几何尺寸/位置（`window_state.json`）及代理页过滤词/排序方式（`proxy_head_state.json`）均已实现跨重启持久化；代理列表滚动条像素偏移（`proxy-scroll-positions`）等低优先级瞬态尚未单独落盘。

## (g) 纯自绘 Direct2D 界面暂未接入 Windows UI Automation 读屏无障碍桥接
Clash Mini 基于 Edge WebView2（Chromium）渲染，其 HTML `aria-label` / `aria-expanded` 语义属性可由浏览器内核自动映射为 Windows UI Automation (UIA) / MSAA 节点供「Windows 讲述人（Narrator）」、NVDA 等外部读屏软件朗读。Clash Win 为彻底消除浏览器引擎开销采用纯 Direct2D 单画布自绘控件树：虽然完整保留了 13 语种 `a11y.*` 词条（用于图标按钮悬停提示 Tooltip）、WCAG 高对比度色板与键盘快捷键，但当前尚未在 C++ `Widget` 层实现 `WM_GETOBJECT` / `IRawElementProviderSimple` 无障碍接口桥接，读屏软件暂无法逐项朗读主窗口画布内的自绘按钮与节点列表（视障读屏用户现阶段建议优先选用同机兼容的 Clash Mini）。
