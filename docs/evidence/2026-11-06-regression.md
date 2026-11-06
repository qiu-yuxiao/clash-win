# 回归证据 · 2026-11-06

> 本文件只记录 **2026-11-06 本轮真实执行** 的命令与输出。未执行的项不写结论，见 `../06-验收状态台账.md` C 节。
> 全程未 push、未建 Release、未推 tag、未改动 `sidecar/` 任何二进制、未访问改动 `ClashVerge` 参考目录（仅一次文件名检索）。

## §1 sidecar 内核资产完整性

命令：

```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify_sidecar.ps1
```

输出（原文）：

```
sidecar: C:\Users\sun_y\Documents\AntiGravity_Projects\ClashWin\sidecar
[OK]      clash-verge-service-install.exe  175616 B
[OK]      clash-verge-service-uninstall.exe  155648 B
[OK]      clash-verge-service.exe  1778176 B
[OK]      enableLoopback.exe  84040 B
[OK]      mini-mihomo.exe  47187456 B
PASS: 5/5 项与基线一致，内核资产未被改动。
```

`SIDECAR_EXIT=0`。基线与口径见 `../05-sidecar-内核资产.md`。

## §2 UI 源码出处门禁

命令：

```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ui_src_gate.ps1
```

输出（原文）：

```
UI_SRC_GATE: PASS（src/ui 全部数值/颜色字面量均有 src:/←/豁免 标注）
```

`GATE_EXIT=0`。门禁规则见 `../02-方法与流程.md:12-20`。

## §3 全量测试（隔离模式）

命令：

```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run_tests_v4.ps1
```

输出（原文）：

```
EXIT=0
```

`TESTS_EXIT=0`。原始日志由脚本写到**项目外**：

```
C:\Users\sun_y\Documents\Trae-CN\temp_20261102_02\tests_full.log
50,644 B  •  mtime 2026-11-06 12:50  •  编码 UTF-16LE（故 grep ASCII 模式不命中）
```

日志尾行（按 UTF-16LE 解码）：

```
547 cases, 0 failed, 0 assertion failures
```

日志尾部可见新增用例族 `t26_*`（delay_threshold / tun_toggle_restart_flags / dns_foreign_plain_upstream_to_doh /
enhance_pipeline_applies_delta_parity / unreferenced_rule_providers_pruned），故用例数由文档旧值 531 增至 **547**。

> 隔离原因：`README.md:67-81` 记载 360 主动防御对 `build\bin\Release` 路径下进程写盘返回 `err=5`，
> 脚本默认把 exe 与 sidecar/locales/assets 复制到 `%TEMP%\cwrun` 后重定向 `TMP/TEMP` 运行。

## §4 安装包产物只读核实

命令（`Get-ChildItem` + `Get-FileHash` + `Get-AuthenticodeSignature`）：

```
sha256sum dist/ClashWin_0.1.0_x64-setup*.exe
Get-AuthenticodeSignature -LiteralPath <each>
```

| 文件 | 字节 | SHA256 | 签名 | 时间戳 |
| --- | --- | --- | --- | --- |
| `dist\ClashWin_0.1.0_x64-setup.exe` | 18,215,213 | `75F66429DDC637D66FCB7B56C4BADD4D8CD56F9DC1CF6C625A054FEB37482447` | `NotSigned` | 无 |
| `dist\ClashWin_0.1.0_x64-setup-signed.exe` | 18,270,488 | `CDED79E579ACE9D64FB83902621FF7AC663F13FA9DEC474A11D5756CCFEF22D8` | `Valid / CN=ClashWin` | 有 |

结论：与 `README.md:29-31`、`docs/README.md:147-149` 一致；`docs/README.md:49-50` 把 18,270,488 / `Valid` 记到不带 `-signed` 的路径上是错的，本轮已修正。

## §5 只读发现的交付链不一致

| 位置 | SHA256 | 字节 | mtime |
| --- | --- | --- | --- |
| `build\bin\Release\clashwin.exe` | `ACA646958150D7DFD451460BB0B079A726C5F2BB582C72DF31EDAACFE809D644` | 4,590,080 | 2026-11-06 10:50 |
| `installer\stage\clashwin.exe` | `88BF7F8A894A35D8A72316D9C1C4D3E342A7586B379EAFEB17C633C6D91AF0D3` | 4,356,096 | 2026-11-05 10:19 |
| `dist\Clash Win 0.1.0\clashwin.exe` | `AB94B28A89B5DADAC02F3AE31D80568C249B71DF73A3F21BD93C4BE44F4B8D9D` | — | — |
| `dist\ClashWin\clashwin.exe` | `510D553B59D97735A2318234F742C79DDC19E27ACAC31801169568BDEBE8E02` | — | — |

`build\bin\Release\.delivery\stamp.json` 记录：

```json
{ "sha256": "CA35E3023F47BE459DA1776F3861450DF4D0B973CFB1BB70A84DD11C7D736D6A",
  "mtime": "2026-11-05T15:56:22.1546584+08:00" }
```

与当前 Release exe 不符 → `delivery_check.ps1` 此刻会判 FAIL。**本轮未覆盖、未重新 `-Record`**，差异如实保留在 `../06-验收状态台账.md` §4。

## §6 本轮未执行项（不计入通过）

| 项 | 原因 |
| --- | --- |
| `cmake --build build --config Release` | 本轮未重新构建，使用的是 2026-11-06 10:50 已有产物 |
| `scripts\run_app_test.ps1`（启动自检） | 脚本 `:32,:43` 使用 `Add-Type`，被当前执行环境安全策略拦截；未绕过、未换 shell 重试 |
| 5 项 UAC 验收 / 干净机 P0 / 4 页并排图 / 目视 / 重启 | 需 UAC、干净机或人工，见 `../06-验收状态台账.md` C 节 |
| `delivery_check.ps1` | 依赖运行态（唯一实例 + 服务 RUNNING + 系统代理开启），且基线已过期；本轮不制造运行态改动 |

## §7 一键验收脚本 DryRun 验证（2026-11-06）

`scripts\acceptance_uac.ps1 -DryRun`：**EXIT=0**，`RESULT=DRYRUN`；#0 / #5 标 `MANUAL` / `SKIP-MANUAL`，#1～#4 仅打印将执行的命令；未签名、未安装、未卸载、未写报告文件。

`scripts\acceptance_clean_machine.ps1 -DryRun`：**EXIT=1（预期行为）**，输出 `EVIDENCE=NOT-CLEAN-MACHINE`、`P0_DECLARATION=DEFERRED`。只读探测到：`C:\Program Files\Clash Mini` 存在、`clash-mini` 进程 1 个、`mini-mihomo` 进程 1 个（PID 11504，SessionId=0）、`ProxyEnable=0` / `ProxyServer=127.0.0.1:10811`、默认路由含 `0.0.0.0/0 -> 198.18.0.2`（TUN 痕迹）、命名管道 301 个且控制面管道未命中。脚本**拒绝宣布 P0 通过**，符合 `01-目的与方针.md:35` 口径。

**环境限制（如实登记）**：`sc.exe` 被当前执行环境的程序黑名单禁用（`Program 'sc.exe' failed to run: Access is denied`），服务状态未采集到。按安全策略**未重试、未换 shell、未做等价绕过**；已将脚本改为在探测失败时诚实报 `UNKNOWN` + `SERVICE-QUERY-UNAVAILABLE`，不再误判成「服务未安装 / 干净机起点正确」。三个脚本随后经 PowerShell 语法解析验证 `parse_errors=0`（只解析、不执行脚本内容）。脚本设计为在用户本机（无该限制）用 `sc query` 完整采集。

**DryRun 不写报告文件**，故 `docs/evidence/` 下没有产生看似验收通过的空证据。
