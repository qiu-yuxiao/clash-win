# Clash Win 标准发版流程（Release Runbook）

> **目标**：把发版收敛成一条**零手工拼接、零分支分叉、零编码陷阱、本地完整预演后再推远程**的确定性流水线。每次发新版本只需遵循本文 **第 2 节的两条命令**即可一次成功。

---

## 1. 架构总览与单一事实源（Single Sources of Truth）

旧版发版流程之所以屡次失败，根源在于「版本号散落多处手工改、每个版本复制一份临时文档、发版前清空线上更新清单、CI 回写 `main` 导致 `dev`/`main` 每次发版后必然分叉、本地不演练打包直接盲推 CI」。重构后的流水线彻底消除上述隐患：

| 维度 | 唯一事实源 / 机制 | 自动派生与同步规则 |
| :--- | :--- | :--- |
| **版本号** | 命令行参数 `-Version X.Y.Z` → 写入 `include/clashwin/config/constants.h` (`kAppVersion`) | `scripts/release.ps1` 自动以严格 UTF-8 无 BOM 正则同步至 `src/clashwin.rc`（PE 文件属性版本号）与 `installer/clashwin.nsi`（NSIS `!define VERSION`）；单元测试 `t35` 动态交叉校验三处一致，不再写死字面量。 |
| **发布说明（Release Notes）** | `CHANGELOG.md` 顶部的 `## vX.Y.Z (YYYY-MM-DD)` 章节 | 若缺失，`release.ps1` 自动从上一个 tag 以来的 `git log` 生成草稿供润色；CI `Publish Release` 阶段直接从 `CHANGELOG.md` 提取该章节生成 GitHub Release 正文与 `updater/app-update.json` 的 `notes` 字段，**不再维护按版本堆叠的 `docs/RELEASE_NOTES_*.md`**。 |
| **已知限制披露** | 常青文档 `docs/KNOWN_LIMITATIONS.md` | Issue 模板永久指向 `docs/KNOWN_LIMITATIONS.md`，**不再维护按版本复制且易写坏正则链接的 `docs/KNOWN_LIMITATIONS_*.md`**。 |
| **应用内自动更新清单** | `updater/app-update.json` | 仓库平时保留**上一个已发布版本**的真实签名清单（发版编译窗口期内旧版客户端检查更新零中断）；CI 发布新资产并完成 Minisign 签名后，自动回写 `origin/main` **并快进同步 `origin/dev`**；本地 `release.ps1` 收尾阶段自动拉回同步本地 `dev`/`main`，**彻底消除分支分叉**。 |
| **全量隔离回归** | `scripts/run_tests.ps1` | 统一本地（Windows PowerShell 5.1）与 CI（PowerShell 7 `pwsh`）的唯一测试运行器，零硬编码路径，自动隔离临时目录并清理陈旧资源副本。 |
| **三道防漏锁** | `release.ps1` → `scripts/git-hooks/pre-push` → `.github/workflows/release.yml` | 本地预演、推 `v*` 标签瞬间、CI 构建前均强制执行 `scripts/release_preflight.ps1`（GATE 1~7），任何一处未过立即阻断。 |

---

## 2. 标准发版操作步骤（每次发版照此执行）

### 前置条件检查（首次配置机器跑一次即可）
1. 本地已安装：Visual Studio 2022（含 C++ 桌面开发工作负载与 CMake）、NSIS 3.x（`makensis.exe`）、GitHub CLI（`gh auth status` 已登录）。
2. `sidecar/` 目录下 5 个上游二进制文件齐全且通过基线校验：
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify_sidecar.ps1
   ```
3. 安装仓库受管 Git 钩子（`release.ps1` 每次运行也会自动幂等确认）：
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\install_hooks.ps1
   ```

---

### 第一步：准备 `CHANGELOG.md` 并执行本地全链路预演（零远程副作用）

1. 在 `CHANGELOG.md` 顶部补充或检查 `## v<X.Y.Z> (YYYY-MM-DD)` 章节（用面向用户的清晰语言描述新增/修复项，不得留 `TODO` / `TBD` 占位符；若尚未建章节，直接跑下面命令也会根据 `git log` 自动生成草稿）。
2. 在 `dev` 分支运行**本地完整预演命令**（不带 `-Push`，绝不触碰远程仓库）：
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release.ps1 -Version <X.Y.Z>
   ```
   **该命令在本地自动串行完成 6 件事**：
   - 确认 `core.hooksPath` 指向 `scripts/git-hooks`；
   - 将 `<X.Y.Z>` 同步到 `constants.h`、`src/clashwin.rc`、`installer/clashwin.nsi`；
   - 校验 `CHANGELOG.md` 含非空的 `## v<X.Y.Z>` 章节；
   - 执行 `scripts/release_preflight.ps1 -Version <X.Y.Z>`（**GATE 1~7 全量门禁**）；
   - 顺序编译 Release 版 `clashwin.exe` 与 `clashwin_test.exe`，并调用 `scripts/run_tests.ps1` 跑完隔离全量单元测试（要求 `0 failed, 0 assertion failures`）；
   - 本地实际打包 `dist\ClashWin_<X.Y.Z>_x64_portable.zip` 与 `dist\ClashWin_<X.Y.Z>_x64-setup.exe`（`-SkipSign`），并交叉核验编译产物、安装包 stage 目录、便携包 ZIP 三处 `clashwin.exe` 与 `win-mihomo.exe` 的 SHA256 完全一致。

> **判定标准**：终端末尾打印绿色的 `=== Clash Win v<X.Y.Z> 本地预演全部通过 ===`。若任何一步报错，直接在本地修复后重跑第一步，无需耗费 18 分钟等待远程 CI。

---

### 第二步：提交并发起正式发布（自动推送 + CI 盯梢 + 产物回拉核验 + 分支收尾）

确认第一步全绿、`CHANGELOG.md` 文案定稿、产品改动已提交（若仅剩第一步自动改动的版本号与 `CHANGELOG.md` 未提交，脚本会自动提交它们）后，执行：

```powershell
# 若刚刚跑完第一步且未再改任何源码，加 -SkipBuild 免重复编译，直接预检 + 推送 + 盯梢：
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release.ps1 -Version <X.Y.Z> -SkipBuild -Push -Yes

# 或者不加 -SkipBuild，一条命令串行跑完编译回归 + 推送发布：
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release.ps1 -Version <X.Y.Z> -Push -Yes
```

**该命令自动完成全部远端闭环**：
1. 确认当前位于 `dev` 分支且工作树干净（若有版本准备改动则自动生成 `chore(release): prepare v<X.Y.Z>` 提交）；
2. `git fetch origin --prune`，若 `origin/main` 含有上一轮 CI 自动提交的 `app-update.json` 而未合入 `dev`，自动合并进 `dev`；
3. 重跑一次 `release_preflight.ps1` 确保合并后 GATE 1~7 依然全绿；
4. 推送 `dev` → 无切换工作树直接快进推送 `origin/main`（`git push origin dev:main`）→ 创建带注释标签 `v<X.Y.Z>` 并推送到 `origin`（触发本地 `pre-push` 钩子二次门禁）；
5. 通过 `gh run list --json` 精确锁定 `headBranch == "v<X.Y.Z>"` 的 GitHub Actions `run_id`，实时流式盯梢 `Validate` → `Build Windows x64` → `Publish Release` 三个 Job；
6. CI 全绿后，将 GitHub Release 上的全部产物（`setup.exe`、`portable.zip`、`SHA256SUMS.txt`、`SIGNATURE_STATUS.txt`、`app-update.json`）下载回本地 `dist/`；
7. 在本地独立重算下载产物的 SHA256 与 `SHA256SUMS.txt`、`app-update.json` 逐一比对；
8. 自动 `git fetch origin main dev` 并将 CI 回写的 `updater/app-update.json` 提交快进同步回本地 `dev` 与 `main` 分支。

> **完成标志**：终端打印 `=== Clash Win v<X.Y.Z> 正式发布与核验圆满完成！ ===`，且 `git status` 干净、本地 `dev` / `main` / `origin/dev` / `origin/main` 指向同一最新提交。

---

## 3. 预检七道门禁详解（`scripts/release_preflight.ps1`）

无论何时可单独运行预检快速排障（耗时 < 3 秒）：
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release_preflight.ps1 -Version <X.Y.Z>
```

| 门禁 | 校验项 | 防御的历史故障 |
| :--- | :--- | :--- |
| **GATE 1** | `constants.h` (`kAppVersion`) == `src/clashwin.rc`（4 处版本字段）== `installer/clashwin.nsi` (`!define VERSION`) == `-Version` | 防止漏改 `.rc` 导致发布的 `clashwin.exe` 右键属性停留在旧版本，或正则写坏版本号。 |
| **GATE 2** | `updater/app-update.json` 合法 JSON、无 `clash-mini` 残留、版本号 `<=` 目标版本、URL 指向 `qiu-yuxiao/clash-win` | 防止误合入 Mini 更新源或损坏 JSON；允许保留上一已发布版本的有效清单。 |
| **GATE 3** | `CHANGELOG.md` 含非空 `## v<X.Y.Z>` 且无 `TODO`/`TBD`；`docs/KNOWN_LIMITATIONS.md` 存在且被 Issue 模板正确引用 | 防止空发版说明、未完成的占位符上线、或 Issue 模板出现 404 死链。 |
| **GATE 4** | `tests/test_*.cpp` 100% 在 `CMakeLists.txt` 注册；`test_t35_release_readiness.cpp` 无硬编码死版本；内核进程名常量一致 | 防止新增测试文件漏注册静默跳过，或 `t35` 写死旧版本号卡死 CI。 |
| **GATE 5** | 本地或 CI 还原后调用 `verify_sidecar.ps1` 校验 5 个上游二进制字节数与 SHA256；校验打包脚本引用的内核文件名真实存在 | 防止 `win-mihomo.exe` 缺失（如历史 v0.2.5 安装包漏内核事故）或上游二进制被签名脚本篡改。 |
| **GATE 6** | 调用 `scripts/ui_src_gate.ps1` 校验 UI 源码溯源注释门禁 | 防止缺少 Mini 源码溯源标注的改动混入发布分支。 |
| **GATE 7** | 扫描 `scripts/*.ps1` **及** `installer/*.ps1`：凡含非 ASCII 字符必须带 UTF-8 BOM (`EF BB BF`) 且无孤立 `CR`；`scripts/git-hooks/pre-push` 必须为纯 `LF` 且自动兼容 `powershell.exe` / `pwsh` | 防止中文 PowerShell 脚本在本地 5.1 与 CI `pwsh` 7 编码解析不一致（`ParserError`），以及防止本机无 `pwsh` 时 `pre-push` 钩子直接报错阻断推送。 |

---

## 4. 异常恢复与辅助开关速查

| 场景 | 处理命令 |
| :--- | :--- |
| **网络波动导致盯梢终端断开，但 CI 仍在跑或已跑完** | 无需重推标签，直接恢复盯梢、下载核验与分支同步：<br>`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release.ps1 -Version <X.Y.Z> -WatchOnly` |
| **CI 因外部网络抖动（如 GitHub API 瞬时 502）偶发失败** | 直接重跑失败的 GitHub Actions Job：<br>`gh run rerun <run_id> --failed --repo qiu-yuxiao/clash-win`<br>随后用 `-WatchOnly` 完成回拉核验与本地分支同步。 |
| **发布前 CI 因仓库脚本问题失败（尚未产生正式 Release），本地修复提交后需重挂同一标签** | 修好并跑通本地预演后，使用 `-Retag` 覆盖标签重新触发 CI：<br>`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release.ps1 -Version <X.Y.Z> -SkipBuild -Push -Retag -Yes` |
| **仅单独跑全量或子集单元测试** | 全量：`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run_tests.ps1`<br>按名称过滤：`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run_tests.ps1 -Filter t35` |

---

## 5. 不可触碰的发版红线

1. **禁止修改 `sidecar/*.exe` 的任何一字节**：本地打包与 CI 构建安装包均须传 `-SkipSign`（或确保签名逻辑绝不触碰 `sidecar/*.exe`），严禁重签或替换 5 个上游二进制。
2. **禁止用手敲 `git tag` + `git push --tags` 绕过 `scripts/release.ps1`**：所有版本发布一律通过 `scripts/release.ps1` 编排。
3. **禁止用 `setup.exe` 的压缩后磁盘体积判断内核是否存在**：NSIS LZMA 固实压缩后约 20~22 MB 属正常现象；内核完整性以打包源 `installer/stage/sidecar/win-mihomo.exe` 的精确字节数与 SHA256 基线校验为准（已内置于 `release.ps1` 与 `release.yml`）。
4. **GitHub Actions 仓库配置依赖**：仓库需保持配置 Variable `SIDECAR_ARCHIVE_URL`（指向内部 sidecar 归档）及 Secret `MINISIGN_PRIVATE_KEY`（用于对安装包生成应用内更新签名，对应公钥位于 `src/config/update_keys.h`）。
