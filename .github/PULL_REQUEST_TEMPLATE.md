<!--
  Clash Win — Pull Request
  形态来源：Mini 仓库无 PULL_REQUEST_TEMPLATE.md（已核实 .github 下不存在），
  本文件按 Clash Win 自身 1:1 移植纪律补建，非照抄 Mini。
-->

## 变更内容 / What changed

<!-- 一句话说明改了什么，以及为什么 -->

## 与 Clash Mini 的对照 / Parity with Clash Mini

Clash Win 是 Clash Mini v3.2.2 的 Windows 原生 1:1 移植。请勾选并填写：

- [ ] 本 PR **是** 1:1 行为对齐：对应 Mini 源码 `路径:行号` = `_______________`
- [ ] 本 PR **是** 实现层差异（Direct2D / Win32 无对应原语）：已在 `docs/` 登记形态差异与理由
- [ ] 本 PR **是** 刻意偏差（用户当场指令）：已在 `docs/README.md` 「刻意偏差」栏登记
- [ ] 本 PR 不涉及 UI / 用户可见行为（构建、CI、文档、测试）

> 若为「Mini 源码里没有的新行为」，请说明依据；1:1 移植不擅自新增 Mini 没有的功能。

## 验证 / Verification

- [ ] `cmake --build build --config Release` 通过（0 error）
- [ ] 全量回归：`python3 scripts/run_tests_py.py` → `EXIT=0 / 0 failed`（不写死用例数）
- [ ] `scripts\ui_src_gate.ps1` PASS（若涉及 UI 源码）
- [ ] `scripts\verify_sidecar.ps1` 5/5（若涉及运行态 / 打包）
- [ ] 新增或修复的缺陷已补回归用例，并做过「临时回退 → 用例失败 → 恢复后通过」的有效性验证
- [ ] 已在与 Clash Mini 共存的机器上验证（端口 10811-10814 / 9108 / 33337 未冲突）

## 交付链 / Delivery

- [ ] 未把 sidecar 的 `*.exe`、`*.pdb`、`build/`、`dist/` 等产物加入提交
- [ ] 未改动 `updater/app-update.json` 之外的更新通道配置（v0.1.0 更新为空通道，见 `docs/KNOWN_LIMITATIONS_0.1.0.md`）
- [ ] 若涉及品牌位，显示名为 `Clash Win`（中间有空格），未引入 `Clash Mini` 的显示名

## 关联 / Related

- Closes #
- 相关 Issue / 讨论：
