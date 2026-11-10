# 05 sidecar 内核资产与完整性校验

## 定位（用户口径，2026-11-06）

- **Clash Verge**：上游原始项目。
- **Clash Mini**：在上游 Clash Verge 基础上，按个人需要做的进化改进版（Tauri / React / Rust，可跨平台）。
- **Clash Win**：把可跨平台的 Clash Mini **原封不动**移植为 Windows 原生客户端
  （C++20 / Win32 / Direct2D），移植的是上层客户端逻辑，不是重做产品。

`sidecar/` 下 5 个 exe 是上游程序的内核与服务组件，**照原样使用**：

> 绝对需要，绝对不能改动、替换、升级或重新编译。

因此本文与配套脚本只做**完整性校验**，不下载、不复制、不替换、不修改这些二进制。

> **★ 特例（2026-11-10 用户裁决，覆盖上条铁律）**：代理内核**对齐到 mihomo v1.19.33** 并改名为 `win-mihomo.exe`。
> 理由：① TUN 模式下直连出口超时的根因是旧内核停留在 v1.19.27（落后 6 个稳定版，缺 auto-route 修复），
> 升级到 v1.19.33 即与 Clash Mini 当前版本一致；② 与 Clash Mini 共存时避免内核同名冲突，按用户记忆的
> 改名链路 `verge-mihomo → mini-mihomo → win-mihomo` 处理——**内核内容均来自同一上游 mihomo v1.19.33，差异只在文件名**。
> **溯源（2026-11-10 二次裁决，关键）**：Win 的内核**直接取自上游源头 `MetaCubeX/mihomo` 的 v1.19.33 发布物**，
> 重命名为 `win-mihomo.exe`，**不是**从 Clash Mini 安装目录的文件复制而来。这样未来若 Clash Mini 被废弃、只维护
> Win 一条线，内核来源仍清晰可追溯；上游升级时也有干净的「Win 现版 vs 上游新版」比较基准。
> Mini 的 `mini-mihomo.exe` 与 Win 的 `win-mihomo.exe` 均为 mihomo v1.19.33 / `with_gvisor` / go1.26.10 的
> **同一源码独立构建**（SHA 不同：Mini=`0B4C7353…`、Win=`1CDE41CF…`，相差约 2 秒构建、23 KB，运行时行为等价），
> 并非字节一致。此改动为唯一授权例外，其余 4 个 exe 仍绝对不可改动。

## 清单与基线

SHA256 于 2026-11-06 在 `C:\Users\sun_y\Documents\AntiGravity_Projects\ClashWin\sidecar\`
实测（`sha256sum`），下表为大写形式，与 `Get-FileHash` 输出一致。

| 文件 | 字节 | SHA256 | 作用 |
| --- | ---: | --- | --- |
| `clash-verge-service-install.exe` | 175,616 | `EA73082094D12A0782389667B9A608CEED3814DF442EED25BA9B56820D0A1A1D` | 安装 `clash_verge_service` 服务 |
| `clash-verge-service-uninstall.exe` | 155,648 | `1CDFCFC3CAB2F26ED4E62FBCE128BC58B6D5C2A3D435BFC8042A5051E288350F` | 卸载该服务 |
| `clash-verge-service.exe` | 1,778,176 | `92AD792ABB91BD84B89894E63A8251434A631A5803D9D043C18F7A5A7961E9D2` | 服务本体，用户裁决「直接复用、不重写」（`docs/phase2/07_user_rulings.md:6`） |
| `enableLoopback.exe` | 84,040 | `F0C328376BD5AE0EF3B0EB19AC1F343C24A9ABFE7BC1008D3B2F6EF3573D04D1` | Loopback 豁免工具 |
| `win-mihomo.exe` | 61,247,488 | `1CDE41CFFFD3DE0AEDAD63C0A8515E1CE5EEA507E8133AED0F637D316C93800A` | **代理内核（特例：mihomo v1.19.33 `with_gvisor` 构建，直接取自上游 `MetaCubeX/mihomo` v1.19.33 发布物并重命名为 `win-mihomo.exe`；与 Clash Mini 的 `mini-mihomo.exe` 同源 v1.19.33 gvisor 但为独立构建、非复制；2026-11-10 用户裁决）**；`src/core/core_manager.*` 以 `CreateProcessW` 拉起 |

## 为什么克隆下来不能运行

- `.gitignore:17` 的 `*.exe` 全局忽略，导致这 5 个文件**从未进入版本库**（`git ls-tree -r HEAD | grep sidecar` 为空）。
- `cmake/copy_resources.cmake:4-5` 的 POST_BUILD `copy_directory` 会把整个 `sidecar/`
  拷到输出目录；`src/core/core_manager.*` 再从输出目录拉起 `win-mihomo.exe`。
- 结论：**从远端克隆后源码可编译，但缺少内核与服务组件，无法运行**。

按 2026-11-06 用户裁决，处理方式为「补文档 + 校验脚本」，**不把二进制纳入版本库**（避免仓库膨胀约 49 MB）。

## 新环境就位方式

**已确认（2026-11-10 用户裁决，含二次溯源裁决）**：代理内核版本＝**mihomo v1.19.33**；文件名＝**`win-mihomo.exe`**（特例，见顶部铁律例外块）；**内容取用＝上游 `MetaCubeX/mihomo` 的 v1.19.33 发布物**（`mihomo-windows-amd64-v1.19.33.zip` 解包后的 `mihomo-windows-amd64.exe`，`with_gvisor` 构建），重命名为 `win-mihomo.exe` 放入 `sidecar\`。**直接取上游，不从 Clash Mini 安装目录复制**——Mini 的 `mini-mihomo.exe` 与 Win 的 `win-mihomo.exe` 同源（均为 mihomo v1.19.33 gvisor）但为独立构建，并非同一文件。

原则：

1. 从上游原版安装中取得**同一份**二进制，原样放入仓库根目录 `sidecar\`；
2. 不改动文件内容，不使用任何经过修改或重新编译的版本；
3. 就位后运行校验，5/5 通过才允许构建、测试或发版；
4. 参考源目录 `C:\Users\sun_y\Documents\AntiGravity_Projects\ClashVerge` **只可参考，
   不得改动、移动或覆写**——本流程不读取、不复制该目录中的任何文件。

校验命令（只读，退出码 0 = 全部一致）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify_sidecar.ps1
```

## 变更纪律

- 任何对这 5 个文件的改动、替换或升级，都必须由用户明确授权。
- 若确需更换，必须同步更新本文的字节/SHA256 与 `scripts\verify_sidecar.ps1`
  的 `$expected` 表，并说明来源与授权依据。
- 校验不通过时的处置：**停止构建与发版**，先核对是否被外部程序（安全软件、
  内核热更新、安装器）替换，不要自行覆盖或「修复」内核。
