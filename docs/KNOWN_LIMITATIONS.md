# Clash Win 已知限制（Known Limitations）

本文档为常青清单，如实记录当前发布版本在工程交付与环境边界上的已知限制。

## (a) 安装包未附带公网商业 Authenticode 代码签名
发布产物采用无商业证书构建口径（CI 传入 `-SkipSign`，签名状态记录于随包 `SIGNATURE_STATUS.txt`，不阻断发布；与上游 Clash Mini v3.2.2 一致）。
首次运行安装器或便携版时，Windows SmartScreen 可能提示「未知发布者（Unknown publisher）」。请在运行前比对随 Release 附带的 `SHA256SUMS.txt` 校验文件完整性。

## (b) sidecar 5 个上游二进制未纳入 Git 源码树
`win-mihomo.exe`、`clash-verge-service*.exe`、`enableLoopback.exe` 因 `.gitignore` 的 `*.exe` 规则未提交进版本库：
- 本地从源码全新克隆后需按 [`docs/05-sidecar-内核资产.md`](05-sidecar-内核资产.md) 放置 5 个上游二进制并通过 `scripts/verify_sidecar.ps1` 5/5 哈希校验；
- CI 构建自动从受控归档还原（若归档缺失 `win-mihomo.exe` 则从 `MetaCubeX/mihomo` v1.19.33 补齐并强校验 SHA256 基线），确保产出的 `setup.exe` 与 `portable.zip` 始终内置完整内核。

## (c) 应用内自动更新依赖 minisign 私钥配置
客户端内置 minisign 公钥验签硬门禁（`src/config/update_keys.h`）。GitHub Actions 在配置了仓库密钥 `MINISIGN_PRIVATE_KEY` 时会自动为 `setup.exe` 生成签名并写入 `app-update.json`；若复刻仓库未配置对应密钥，应用内下载安装会因缺签名而拒绝执行（手动下载安装包覆盖升级不受影响）。

## (d) 全新干净 Windows 机器验收覆盖范围
日常开发与回归主要在装有开发工具链及曾安装过 Clash Mini 的 Windows x64 环境完成；对于从未安装过任何同类客户端的全新 Windows 10/11 环境，UAC 提权与 Windows 服务首次注册路径欢迎通过 Issue 反馈实机表现。

## (e) 少量次要界面缓存未持久化
窗口几何尺寸/位置（`window_state.json`）及代理页过滤词/排序方式（`proxy_head_state.json`）均已实现跨重启持久化；代理列表滚动条像素偏移（`proxy-scroll-positions`）等低优先级瞬态尚未单独落盘。
