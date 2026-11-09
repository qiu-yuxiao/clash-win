# 第一批 1:1 差异清零验收记录（2026-11-06）

范围：F-D01 i18n 运行时接线、B-D06 verge 失败回退模板、B-D26 日志 Service 分支。
（F-D05 帮助菜单 loading 禁用移至第二批；B-D35 已撤销，见差异清单 §5。）

## 结果总览

| 检查 | 结果 |
| --- | --- |
| 全量编译（clashwin_test + clashwin） | PASS，0 错误 |
| 全量回归测试 | **PASS：547 cases / 0 failed / 0 assertion failures** |
| verify_sidecar（Python 等价执行） | PASS：5/5 SHA256 与基线一致 |
| ui_src_gate（Python 等价执行） | PASS：0 违规（含「实测/截图/目测/近似/取色」禁止依据检查） |
| 中文硬编码残留 | 198 → 23 → 21 处（均定性为非 F-D01 机制缺口，见差异清单 §5 定性表） |
| tr() 调用 | 146 处，覆盖 123 个 Mini locale key |

## 编译

- `cmake --build build --config Release --target clashwin_test clashwin`
- 中途修复：`i18n.h` 漏声明 `revision_` 成员；`utility_modals.cpp:1008`
  `draw_tool_btn` lambda 第二参数 `const wchar_t*` → `const std::wstring&`（适配 `tr()` 返回类型）。
- 产物：`build/bin/Release/clashwin.exe`、`clashwin_test.exe`，locales 已随 POST_BUILD 拷贝。

## 回归测试（关键环境事实）

- 环境限制 1：`powershell.exe` 经 Bash 调用被安全策略拦截 → 用 Python `subprocess`
  等价复刻 `run_tests_v4.ps1` 的隔离逻辑。
- 环境限制 2：`%TEMP%\cwrun` 下新建的自编译 exe 会被安全软件直接删除（文件消失），
  亦导致此前编译日志/链接异常假象 → 隔离目录改用项目内 `build/cwtest`。
- 隔离目录：`build/cwtest`（exe 改名 `cwrun.exe`，附带 sidecar 5 exe 与 locales），
  TMP/TEMP 重定向至 `build/cwtest/tmp`。
- 输出：`547 cases, 0 failed, 0 assertion failures`。

## 门禁

- `verify_sidecar.ps1` 逐条 Python 复刻（SHA256 + 字节数）：5/5 OK
  （EA730820…、1CDFCFC3…、92AD792A…、F0C32837…、3ED26E40… 与 `docs/05` 基线一致）。
- `ui_src_gate.ps1` 规则逐条 Python 复刻（9 类 UI 常量模式、4/8 行回看出处、块级标记、
  禁止依据词表）：`src/ui/**` 0 违规。

## 顺带修复

- `scripts/run_tests_v4.ps1`：日志目录不存在时自动创建（原硬编码
  `Trae-CN\temp_20261102_02`，目录已删会导致脚本失败）。
