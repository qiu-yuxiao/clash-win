# Clash Win 发版流程（已验证成功流程，v0.2.3 起生效）

> 目的：把 v0.2.2 / v0.2.3 两次发版反复踩的坑固化成**先本地预检、再推送**的确定性流程，
> 不要再等 CI 跑 18 分钟才发现脚本写死的假设过期。
> 本流程经 v0.2.3（run 42794820672，三 job 全绿、4 产物已发布）验证。

---

## 0. 一句话流程（法律：唯一切口是 `scripts/release.ps1`）

```
本地改完产品代码 → 跑 scripts/release.ps1 -Version X -Build -Push（-Build 含本地全量回归）
→ 脚本自动：同步版本号4处 + 脚手架发布材料 + 跑预检(GATE1~6) + 本地回归
→ 提交 → 推 dev → 快进 main → 推 main → 打 tag → 推 tag（pre-push 钩子再拦一道）
→ CI（validate→build→publish，build 还原 sidecar 后重跑全部预检 gate）→ 回拉 dist/ 核验
```

> **⚠️ 铁律：发版唯一受支持且被强制的路径就是 `scripts/release.ps1`。**
> 不得手敲 `git tag` / `git push` 走发版——pre-push 钩子会在你推 `v*` tag 时强制跑预检，
> 不过即拒绝推送；CI build job 还原 sidecar 后也会重跑全部预检 gate，任一失败整条 run 失败、绝不发布。
> 三层兜底（脚本 → 钩子 → CI）让"漏步"在动作发生那一刻就被拦下，不必等 CI 18 分钟。
> **切勿为了过 CI 改产品代码**：CI 红 = 仓库脚本/断言过期或本地漏跑回归，先本地修。

### 新人三件事（克隆后一次即可，之后全自动）
1. `powershell -File scripts/install_hooks.ps1` —— 装好 pre-push 钩子（幂等）。
2. 发版只用 `scripts/release.ps1 -Version X -Build -Push`；`-Build` 跑本地回归是第一道闸。
3. 绝不手敲 `git tag`/`git push` 发版；若误敲，钩子会拦。

---

## 1. 发版前本地必做（拦住 90% 的坑）

### 1.1 跑预检脚本（现由 `release.ps1` 自动跑，不必手敲）
> 预检现已**强制化**：`scripts/release.ps1` 在提交前自动跑 `release_preflight.ps1`；
> pre-push 钩子在推 `v*` tag 时再跑一次；CI build job 还原 sidecar 后又跑一次。
> 三处任一不过都走不下去。下列清单是预检覆盖的内容，供你理解"为什么拦"，不是让你手敲。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts/release_preflight.ps1 -Version <新版本号>
```
脚本会检查（详见脚本注释）：
- **版本单一事实源**：`include/clashwin/config/constants.h` 的 `kAppVersion` ==
  `installer/clashwin.nsi` 的 `VERSION` == `updater/app-update.json` 的 `version` == 你要打的 tag。
- **便携包预校验条目 == 实际 sidecar 内核名**：`release.yml` / `portable.ps1` /
  `build_installer.ps1` 里写的 `sidecar/<名>.exe` 必须真实存在于 `sidecar/`。
  （v0.2.3 第二次 run 失败根因：内核改名 `mini-mihomo→win-mihomo` 后，
  `release.yml` 还写死 `sidecar/mini-mihomo.exe`，校验直接抛 `missing entry`。）
- **t35 发布就绪用例版本断言 == 版本号**：`tests/test_t35_release_readiness.cpp`
  里写死的版本必须同步 bump。（v0.2.3 第三次 run 失败根因：CI 回归卡死在版本写死用例。）
- **测试写死内核名 == `kCoreProcessToken`**：`test_config_template.cpp` 的 `clash_core:`、
  `test_core_kill.cpp` 的进程名不能有残留旧名 `mini-mihomo`。
- **sidecar 5/5 基线**：调 `verify_sidecar.ps1`（大小 + SHA256）。
- **setup 必须含内核（GATE 6 + CI 双保险）**：`win-mihomo.exe` 必须进最终 `setup.exe`。
  v0.2.5 正是漏了这条——归档漏内核、且当时便携包/源 sidecar 都查了、**setup 内容却没人查**，
  打出不带内核的安装包。现已补三道锁：① `build_installer.ps1` 硬性要求
  `build\bin\Release\sidecar\win-mihomo.exe`，缺失即 Fail；② `release.yml` 在归档缺失时从 mihomo
  上游 v1.19.33 补齐并按 `verify_sidecar.ps1` 基线校验哈希；③ `release.yml` "Verify release artifacts"
  校验**打包源** `installer/stage/sidecar/win-mihomo.exe` 的字节数（`== 61247488`）**且** SHA256 匹配
  `verify_sidecar.ps1` 基线（`1CDE41CF…`）——NSIS 直接从该源打包，源是上游原版完整内核 ⇒ setup 必含内核。
  预检 **GATE 6** 校验本地 `sidecar/win-mihomo.exe` 存在。
  ⚠️ **严禁用 setup.exe 磁盘体积判内核有无**：lzma 把 78MB 内容压成 ~21MB，含/漏内核磁盘体积几乎一样
  （v0.2.5 漏内核 21.9MB，v0.2.6 含内核 20.87MB），旧的 `< 35MB` 断言已因误杀正确包而废止（见 §9 反例）。

> 任何一项 FAIL 都先本地改完再推。**不要**指望 CI 帮你发现——CI 一轮 18 分钟。

### 1.2 本地全量回归
```bash
python3 scripts/run_tests_py.py   # 期望：678 cases, 0 failed, EXIT=0
```
（若构建触发全量重编，约 7–10 分钟。）

### 1.3 构建环境坑（沙箱/本地都需注意）
- **重复代理变量会让 MSBuild 直接罢工**：确保环境里不同时存在 `HTTPS_PROXY` 和 `https_proxy`
  （大小写两套），以及 `HTTP_PROXY`/`http_proxy`、`ALL_PROXY`/`all_proxy`。构建前清掉多余的那套：
  ```powershell
  Remove-Item Env:HTTPS_PROXY,Env:https_proxy,Env:HTTP_PROXY,Env:http_proxy,Env:ALL_PROXY,Env:all_proxy -ErrorAction SilentlyContinue
  ```
- 找 cmake 用 VS2022 BuildTools 路径：
  `C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe`

---

## 2. 版本号与发布材料（每次发版必改）

版本号同步改 **3 处**：
1. `include/clashwin/config/constants.h` → `kAppVersion`
2. `installer/clashwin.nsi` → `!define VERSION`
3. `updater/app-update.json` → `"version"`（占位，CI 会生成真实清单回写 main）

发布材料（validate job 强制要求存在，否则 CI 在第一步就红）：
- `docs/RELEASE_NOTES_<版>.md` —— 本次改了什么（用户能懂的语言，别堆代码行号）
- `docs/KNOWN_LIMITATIONS_<版>.md` —— 已知限制（未签名、干净机未实测等诚实披露）
- `CHANGELOG.md` —— 顶部加 `## v<版>` 条目
- `.github/ISSUE_TEMPLATE/*.yml` —— 里面的已知限制文档链接版本号同步

---

## 3. 推送与打 tag

```bash
git add -A && git commit -m "chore(release): v<版> 版本准备 + 发布材料"
git push origin dev
git checkout main && git merge --ff-only dev && git checkout dev
git push origin main
git tag -a v<版> -m "Clash Win v<版> —— <一句话亮点>"
git push origin v<版>
```
> 分支约定：从 `dev` 打 tag 发版；`main` 仅作快进镜像（发布说明/Issue 模板链接指向 `blob/main`）。
> 若需要重发（见 §5），用 `git tag -f -a v<版> -m "..."` + `git push origin v<版> --force`。

---

## 4. 等 CI 并回拉产物

```bash
gh run watch <run_id> --repo qiu-yuxiao/clash-win --exit-status --interval 30
```
- 三 job：`Validate` → `Build Windows x64` → `Publish Release`。全程约 18 分钟。
- 成功后产物回拉到 **`dist/`**（用户长期约定，勿用临时目录）：
  ```bash
  gh release download v<版> --repo qiu-yuxiao/clash-win --dir dist --clobber
  ```
- **本地核验**（不信 CI 绿灯，独立重算）：
  - `SHA256SUMS.txt` 两条哈希与下载产物 `Get-FileHash -Algorithm SHA256` 一致。
  - `SIGNATURE_STATUS.txt` 应为 `NotSigned|`（与发布说明口径一致；本会话无法 UAC 自签）。
  - sidecar 5 个 exe 字节未被改写（对比 `verify_sidecar.ps1` 基线）。
  - 若走应用内更新，可用官方 minisign + 编译进 `src/config/update_keys.h` 的公钥对真实 setup.exe 验签。

---

## 5. CI 失败怎么办（v0.2.2 / v0.2.3 的共同范式）

**不要慌，也不要为了过 CI 改产品代码。** 失败几乎总是「仓库脚本/断言写死的假设过期」：

| 现象 | 根因 | 修法 |
|---|---|---|
| `portable.ps1 failed` 但 ZIP 已产出 | pwsh 下 `$LASTEXITCODE` 为 `$null`，`$null -ne 0` 为 True，被误判失败 | 脚本成功路径末尾显式 `exit 0`（失败路径仍由 `exit 1` 终止） |
| NSIS 自签失败 / 改写 sidecar 字节 | `sign.ps1` 签遍所有 exe 含 5 个上游内核 | CI "Build NSIS" 传 `-SkipSign` |
| publish 阶段 `git checkout -B main` 报 would be overwritten | 清单把工作树改脏后跨树切换 | 清单只写 `RUNNER_TEMP`；`git fetch` 显式 refspec；`git checkout -f -B main` |
| `portable.zip missing entry: sidecar/mini-mihomo.exe` | 内核改名后 release.yml 校验条目没跟上 | 把条目改成实际内核名（如 `win-mihomo`） |
| CI 回归卡死在版本断言 | `test_t35_release_readiness.cpp` 版本写死 | 同步 bump 该用例的版本字面量 |
| 杀残留内核的测试失败 | 内核改名后测试断言还按旧名 | 同步 bump 测试里的内核名（对齐 `kCoreProcessToken`） |
| sidecar 还原后 5/5 失败 | `sidecar` 内部归档还含旧内核 | 本地按现行 sidecar 重新打包上传替换该内部 Release 资产 |
| **v0.2.6** Full regression `2 failed` | 发版改动违背既有契约：`sidecar_core_path()` 被误改成 `<exe>\sidecar\`；仓库 `app-update.json` 被写入真实 url/signature（应为占位符） | 改回既定契约；**发版必须带 `-Build` 本地全量回归**，别等 CI 18 分钟后才暴露 |
| **v0.2.6** 预检闸 `ParserError`（Build 之前整条 run 红） | 无 BOM 的 UTF-8 中文 `.ps1` 被 CI 的 `pwsh` 7 按系统默认编码（CP936/GBK）误读；本地 Windows PowerShell 5.1 容错，导致「本地 PASS=11」是假象 | 全部 `scripts/*.ps1` 加 UTF-8 BOM；预检新增 **GATE 7**（BOM + 无孤立 CR），让本地 5.1 也能提前拦下编码类 bug |
| **v0.2.6** `Verify release artifacts` 报 `setup.exe 体积异常偏小 (20.9 MB)` | **断言逻辑与打包事实不符**：NSIS 用 lzma 把 78MB 未压缩内容（含 61MB 内核）压成 20.87MB 磁盘文件；`< 35MB` 体积断言既误杀正确包、又分不出真漏内核（v0.2.5 漏内核 21.9MB） | 删除体积断言，改为校验打包源 `installer/stage/sidecar/win-mihomo.exe` 的**字节数 + SHA256 基线**；新守卫落地前先看打包器原始输出（`Install data` / `Total size`），不凭「体积越大越完整」的假设 |

**标准修复循环**（v0.2.2 验证过）：
1. `gh run view <id> --log-failed` 看真实报错。
2. 在**仓库**里修脚本/断言（不碰产品逻辑、不削弱断言）。
3. 本地跑预检 + 回归确认绿。
4. 提交 → 推 dev/main → `git tag -f -a v<版>` → `git push origin v<版> --force` → 触发新 run。
5. 重复直到三 job 全绿。

---

## 6. 铁律提醒（发版不得违反）

- **sidecar 5 个 exe 字节不可改动**（含内核）：发版用 `-SkipSign`、不重新编译、不替换，
  只用官方/已裁决基线；核验时逐字节比对 `verify_sidecar.ps1` 基线。
- **ClashVerge 工作区只读**，绝不动。
- **Mini 源码是唯一真相源**：凡与 Mini 行为对齐的改动，改完要有 Mini `file:line` 出处。
- **未签名资产**是既定口径（避免 CI 自签改写内核 + 撞 UAC），发布说明如实披露「未知发布者」。

---

## 6.5 v0.2.5 复盘：setup 漏内核（本流程的历史漏洞）

v0.2.5 的 `setup.exe`（21.9MB）**不含 `win-mihomo.exe`**，所有干净安装都拿不到内核、节点表空白。
根因不是本地文件被删，而是**发版流程没有一道锁验证「最终 setup 含内核」**：

- `verify_sidecar.ps1` 5/5 基线、CI restore 步骤、预检 GATE 2 都只查「源/便携包」里有没有内核；
- `release.yml` 的 "Verify release artifacts" 只断言 **portable.zip** 含 `sidecar/win-mihomo.exe`，
  **setup.exe 内容从未被校验**；
- 当时 `build_installer.ps1` 也还没有「缺失即 Fail」的硬卡。

结果：一旦 `SIDECAR_ARCHIVE_URL` 那个外部归档漏了内核，CI 就静默打出内核-less 的 setup。

**已补的三道锁（见 §1.1 与脚本）：**
1. `build_installer.ps1` 硬性要求 `build\bin\Release\sidecar\win-mihomo.exe`，缺失即 `Fail`。
2. `release.yml` restore 步骤在归档缺内核时，从 `MetaCubeX/mihomo` v1.19.33 补齐并按
   `verify_sidecar.ps1` 基线（`1CDE41CF…`）校验 SHA256，不符即失败。
3. `release.yml` "Verify release artifacts" 校验**打包源** `installer/stage/sidecar/win-mihomo.exe`：
   字节数 `== 61247488` 且 SHA256 匹配基线（`1CDE41CF…`）。NSIS 直接从此源打包，源正确即 setup 必含
   完整内核；预检 **GATE 6** 校验本地 `sidecar/win-mihomo.exe` 存在。
   （原第 3 条「setup 体积 < 35MB 断言」已于 v0.2.6 证伪并删除：lzma 压缩使含/漏内核磁盘体积几乎一样，
   该断言误杀了含内核的正确包，详见 §9 反例。）

**铁律**：任何发版改动若动到上述三处守卫，必须先跑一遍预检 + 本地构建 installer 确认 setup 含内核，
绝不再依赖「CI 绿灯」——CI 绿灯曾经放过内核-less 的包。新增守卫本身也必须先核对**打包器原始输出**
（如 NSIS 的 `Install data` 字节数）再落地，不能凭「体积越大越完整」这类想当然的假设。

## 7. 检查清单（发版前打勾）

> 以下绝大多数由 `scripts/release.ps1` 自动完成；本清单用于复核，不是手敲步骤。

- [ ] 产品代码本地改完
- [ ] `scripts/release.ps1 -Version <版> -Build -Push`：**`-Build` 本地全量回归几秒~十几分钟拦下不一致**（v0.2.6 正是漏这步被 CI 18 分钟拦下 2 个测试失败）
- [ ] 预检 GATE 1~7 全 PASS（脚本自动跑；推 tag 时 pre-push 钩子再跑；CI 还原 sidecar 后又跑；GATE 7 校验全部 ps1 的 UTF-8 BOM / 孤立 CR）
- [ ] 版本号 4 处一致（脚本自动同步 constants.h / clashwin.nsi / app-update.json / t35 测试）
- [ ] 发布材料齐（RELEASE_NOTES / KNOWN_LIMITATIONS / CHANGELOG / Issue 模板链接，脚本脚手架；内容需人工填）
- [ ] 内核若改名过：sidecar 内部归档已重传、`release.yml` 校验条目已同步
- [ ] **setup 含 win-mihomo**：预检 GATE 6 通过；CI 三道锁（build_installer 硬卡 + 归档缺失上游补齐 + 打包源字节数/SHA256 双校验）未被改动或退化；**不得以 setup 磁盘体积作判据**
- [ ] CI 三 job 全绿；产物回拉 `dist/` 并独立核验哈希/签名/sidecar 字节
- [ ] 更新 `.workbuddy/memory/` 当日日志与（如需）MEMORY.md

---

## 8. 强制遵守机制（防漏步——本节是 v0.2.6 反复踩坑后的固化）

每次 release 都重复踩前面的坑，根因是"流程写在文档里、靠人记忆守纪律"。
现把流程变成**不按流程就走不下去的代码**，三层兜底，绕不过去：

| 层 | 机制 | 拦在哪个动作 | 效果 |
|---|---|---|---|
| ① 唯一切口 | `scripts/release.ps1` | 唯一发版命令 | 版本号只输入一次自动同步 4 处；自动脚手架发布材料；自动跑预检；`-Build` 跑本地全量回归；`-Push` 才碰远程 |
| ② 本地钩子 | `scripts/git-hooks/pre-push`（受跟踪，`install_hooks.ps1` 启用） | `git push origin vX` 那一刻 | 推 tag 前强制重跑预检，不过即拒绝推送——正好拦在"等 CI 才发现"之前 |
| ③ CI 重闸 | `release.yml` build job 还原 sidecar 后重跑 `release_preflight.ps1` | CI 内 | 即便有人绕过①②手推 tag，CI 仍 fail-fast，绝不发布 |
| ④ 文档即法律 | 本文 §0「唯一切口」+ 新人三件事 | 阅读/克隆时 | 新人克隆后第一件事装钩子、只用脚本；手敲 git tag/push 不被允许的路径 |

**额外铁律**：发版前务必带 `-Build`，让全量回归在本地先跑。CI 的回归是兜底不是首选——
v0.2.6 的 2 个测试失败若带 `-Build` 在本地几秒就能拦下，而不是耗 18 分钟等 CI。

---

## 9. 遇到新失败的处理纪律（v0.2.6 反复踩坑后立的铁律）

发版实行过程中若遇到**流程/脚本没拦住的新失败**（CI 红、本地预检漏过的坑等），
执行以下闭环，**严禁把问题甩回用户、严禁反问用户"你看怎么办"**：

1. **主动找原因**：从运行日志 / 编译器的**实际输出**定位真实根因，务必把根因坐实——
   看打包器、编译器、测试框架的原始打印，不靠猜；必要时对照成功案例或本地复现。
2. **先报告用户**：把「根因 + 证据 + 影响 + 修复方向」写清楚给用户，等用户拍板，不擅自动手。
3. **得许可后再改**：任何对 release 脚本 / CI 配置 / 本流程文档的修正，都必须先获得用户
   **明确许可**；不得擅自修改，更不得为过 CI 而削弱断言或改产品代码。
4. **修正即升级文档**：修复后必须把这次的新失败模式补进本文（§5 失败根因表、§1.1 预检覆盖、
   §7 检查清单，或新增条款），让 CI 和后来者都能拦下同一类问题——**流程必须随每次失败一起
   演进**，不能原地重复踩同一个坑。

> **反例（已犯、已修）**：v0.2.6 一度把「setup.exe 磁盘体积 < 35MB」当作漏内核判据。
> 但 NSIS 用 lzma 把 78MB 未压缩内容压成 ~21MB 磁盘文件——**含内核的包和漏内核的包
> 磁盘体积几乎一样**（v0.2.5 漏内核 21.9MB，v0.2.6 含内核 20.87MB）。该判据既误杀正确包、
> 又分不出真漏内核，纯属「断言逻辑与打包事实不符」。证据取自 NSIS 编译器原始输出
> （`Install data: 81645473 bytes`、`Total size: 21883195 / 81980702 bytes`），不是推测。
> **实际落地修正**：删除体积断言，改为校验打包源 `installer/stage/sidecar/win-mihomo.exe` 的
> **字节数（== 61247488）+ SHA256（== 基线 `1CDE41CF…`）** 双校验；`build_installer.ps1`
> 编译后不清 stage，NSIS 直接从该源打包，源为上游原版完整内核 ⇒ setup 必含内核。
> §1.1 / §6.5 / §7 已同步为同一口径，不再保留「体积断言」旧说法。
>
> 注：本节第 2、3 条是长期纪律；若用户当场明确指令「不要问、直接做成功再报告」，以当场指令为准，
> 但仍须完成第 1 条（坐实根因）、第 4 条（修正即升级文档）——本次即按此执行。
