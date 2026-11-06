# 2026-11-06 UAC 验收：工具侧代跑尝试与部分执行证据

> 本文件记录**真实发生的事**，不把未完成项写成通过。
> 关联台账：`docs/06-验收状态台账.md` C 节；自动报告：`acceptance-uac-*.md`（本次部分执行**未产出**）。

## 1. 结论先行

- **UAC 五项整体仍为【待验】**，不得宣布通过。
- 13:35 的一次提权成功执行到 **#2 完整打包 → #1 验签 → #3 第一次 `/KEEP`**，随后因工具命令超时、进程树被回收而中断。
- **#3 第二次 `/KEEP`、#4 卸载实测、结构化报告均未完成**；`hash_after2.txt` 不存在，无 `acceptance-uac-*.md`。
- 干净机 P0 脚本已正式（非 DryRun）执行完毕，判定 `NOT-CLEAN-MACHINE`，报告见 `acceptance-p0-20261106-132726.md`。

## 2. 环境与前置事实

| 项 | 实测值 |
| --- | --- |
| 工具进程管理员上下文 | `IS_ADMIN=False` |
| `makensis.exe` | `C:\Users\sun_y\AppData\Local\tauri\NSIS\makensis.exe`（存在） |
| `signtool.exe` | `C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe`（存在） |
| 第一次 UAC 提权 | `Start-Process -Verb RunAs` 报 `The operation was canceled by the user` |
| `sc.exe` | 被执行环境程序黑名单禁用（`Access is denied`），P0 脚本如实记 `UNKNOWN`，未重试、未绕行 |

**发现并修复的真 bug**：原 `acceptance_uac.ps1` 的提权分支用 `$ErrorActionPreference='Continue'`，`Start-Process` 抛错后继续执行 `exit $p.ExitCode`，而 `$p` 为 null → **退出码 0**，即"一项都没跑却报告成功"。已在 `acceptance_uac.ps1`、`installer/build_installer.ps1`、`scripts/sign.ps1` 三处统一加 `try/catch` + 空进程对象检查，失败固定 `exit 2`，且不写报告。

## 3. 13:35 部分执行已产生的证据（磁盘实测，非推断）

| 验收项 | 状态 | 实测证据 |
| --- | --- | --- |
| #2 完整打包 | **本轮实测 PASS** | `dist\ClashWin_0.1.0_x64-setup.exe` 由 11-05 的 18,215,213 B / `75F66429…` 重打为 **18,354,496 B / `456445A9…`**（13:35:25）；`installer\stage\` 已重建 |
| #1 签名验证 | **本轮实测 PASS** | 上述新包 `Get-AuthenticodeSignature` = `Valid`，`CN=ClashWin`，时间戳证书存在；`build\bin\Release\clashwin.exe`、`installer\stage\clashwin.exe`、`C:\Program Files\ClashWin\clashwin.exe` 亦均为 `Valid / CN=ClashWin` |
| 卸载器签名钩子 | **本轮实测 PASS** | `C:\Program Files\ClashWin\uninstall.exe` = `Valid / CN=ClashWin`（证明 `!uninstfinalize` 生效） |
| #3 第一次 `/KEEP` | **本轮实测 PASS** | `hash_before.txt`（13:35:27）与 `hash_after.txt`（13:35:49）各 18 行，`diff` 为空，配置目录 `%APPDATA%\io.github.clash-win.clash-win` 全部文件 SHA256 零差异 |
| #3 第二次 `/KEEP` | **未执行** | `hash_after2.txt` 不存在 |
| #4 卸载实测 | **未执行** | `C:\Program Files\ClashWin` 仍存在 |
| 结构化报告 | **未产出** | 无 `acceptance-uac-*.md` |

> 注：上述 `hash_before.txt` / `hash_after.txt` 会被后续完整运行覆盖；本文件保留"两者完全一致"这一事实结论。

## 4. 交付链收敛情况（对比 11-06 早间台账记录的"四处互不相同"）

| 位置 | SHA256 | 大小 / 时间 |
| --- | --- | --- |
| `build\bin\Release\clashwin.exe` | `5690CF3DA5B5BA399067E7500EEE6DD50B79784062EBB20242285CADF9808D55` | 4,598,240 B，13:33:46 |
| `installer\stage\clashwin.exe` | 同上 | 同上 |
| `C:\Program Files\ClashWin\clashwin.exe` | 同上 | 同上 |
| `dist\Clash Win 0.1.0\clashwin.exe` | `AB94B28A…` | 3,530,752 B，11-02 06:17（历史解压目录） |
| `dist\ClashWin\clashwin.exe` | `510D553B…` | 3,520,512 B，10-30 03:37（历史解压目录） |

**结论**：以 `build/bin/Release` 为单一事实源的**活动交付链（前三处）已收敛为同一字节**；`dist` 下两个子目录是 10-30 / 11-02 的历史解压副本，不属于本次打包产物，仍为旧值，不作覆盖。

**仍未收口**：`build\bin\Release\.delivery\stamp.json` 仍记录 11-05 15:56 的 `CA35E3023F47BE459DA1776F3861450DF4D0B973CFB1BB70A84DD11C7D736D6A`，与当前 `5690CF3D…` 不符，交付基线登记过期。

## 5. 安全与保全措施

- 执行前已把 11-05 的两个安装包备份到项目外：`C:\Users\sun_y\AppData\Local\Temp\clashwin_dist_backup_20261106\`（35 MB），SHA256 与源端逐项一致：
  - `ClashWin_0.1.0_x64-setup-signed.exe` = `CDED79E579ACE9D64FB83902621FF7AC663F13FA9DEC474A11D5756CCFEF22D8`
  - `ClashWin_0.1.0_x64-setup.exe` = `75F66429DDC637D66FCB7B56C4BADD4D8CD56F9DC1CF6C625A054FEB37482447`
- `sidecar/` 5 个上游 exe 本次复核 SHA256 与 `docs/05-sidecar-内核资产.md` 基线一致，**未被改动、未升级、未重新编译**。
- `ClashVerge` 参考目录本轮未访问。
- 未 push、未建 Release、未推 tag、未上传资产。

## 6. 后续：13:44 完整重跑已闭合

`scripts\acceptance_uac.ps1` 于 2026-11-06 13:43–13:44 在后台完整执行（无 `-SkipRebuild`、无 `-SkipDestructive`），产出 `acceptance-uac-20261106-134426.md`，**`RESULT=PASS`，EXIT=0**：

| # | 结果 |
| --- | --- |
| #2 完整打包 | PASS — 18,354,760 B / `3B536480…` |
| #1 签名验证 | PASS — `Valid / CN=ClashWin` + 时间戳 |
| #3 `/KEEP` ×2 | PASS — `hash_before` / `hash_after` / `hash_after2` 各 18 行、两两零差异 |
| #4 卸载实测 | PASS — 程序文件移除、配置目录保留 |
| #0 / #5 | MANUAL / SKIP-MANUAL，仍需人工 |

**以 `acceptance-uac-20261106-134426.md` 为最终验收依据**；本文件第 3 节记录的是 13:35 那次被中断的部分执行，`hash_before.txt` / `hash_after.txt` 已被完整运行覆盖。交付链随后由 `scripts\delivery_check.ps1 -Record` 重登记（`60ECEDAF…`，7 个接线标记全 PASS），详见 `docs/06-验收状态台账.md` §4。
