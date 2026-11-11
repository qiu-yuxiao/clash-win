# Clash Win v0.2.6 已知限制（Known Limitations）

诚实披露本版本仍未闭环的事项，避免「假发布」。

## (a) 安装包未做 Authenticode 代码签名
本版本安装包**未做 Authenticode 签名**（发布环境无法 UAC 提升自签，且 CI 采用 `-SkipSign` 口径）。
Windows 会提示「未知发布者」/ SmartScreen 警告。请运行前用随包提供的 `SHA256SUMS.txt` 校验哈希。
签名状态只记录、不阻断发布（与 v0.2.2 起口径一致）。

## (b) sidecar 5 个上游二进制不在 git 内
`clash-verge-service*.exe` / `enableLoopback.exe` / `win-mihomo.exe` 因 `.gitignore` 的 `*.exe`
未入库，CI 全新 checkout 后依赖仓库变量 `SIDECAR_ARCHIVE_URL` 还原。
v0.2.6 已对此加固：归档若漏 `win-mihomo.exe`，CI 会从 `MetaCubeX/mihomo` v1.19.33 补齐并按
`verify_sidecar.ps1` 基线校验 SHA256，不符即失败，不再静默产出内核-less 包。

## (c) 应用内更新（minisign）依赖密钥配置
客户端验签链为 minisign（公钥编译于 `src/config/update_keys.h`），是硬门禁。
若仓库 secret `MINISIGN_PRIVATE_KEY` 未配置，生成的 `app-update.json` 不带 `signature` 字段，
应用内「下载-安装」通道会拒绝安装——属诚实降级，不影响手动下载安装。

## (d) 干净机（未装过 Clash Mini）未做完整实测
当前实测机已装有 Clash Mini；`NOT-CLEAN-MACHINE`。干净机下的 UAC 自提升、服务接管等路径
未经完整实测，欢迎反馈。

## (e) 内核自动恢复需联网
v0.2.6 的「内核缺失自动恢复」需能访问 GitHub（MetaCubeX/mihomo releases）。
完全离线环境首次启动仍会节点表空白；但常态下安装包已含内核，此路径仅作兜底。

## (f) ClashVerge 工作区只读
对比基线 `ClashVerge` 工作区保持只读，不改动其任何文件。
