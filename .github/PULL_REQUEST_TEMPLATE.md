<!--
  Clash Win — Pull Request
  本文件按 Clash Win 1:1 移植纪律制定。
-->

## 变更内容 / What changed

<!-- 一句话说明改了什么，以及为什么 -->

## 与 Clash Mini 的对照 / Parity with Clash Mini

Clash Win 是 Clash Mini（当前同步基线 v3.2.5，详见 `移植基本准则.md`）的 Windows 原生 1:1 移植。请勾选并填写：

- [ ] 本 PR **是** 1:1 行为对齐：对应 Mini 源码 `路径:行号` = `_______________`
- [ ] 本 PR **是** 实现层差异（Direct2D / Win32 无对应原语）：已在 `docs/` 登记形态差异与理由
- [ ] 本 PR **是** 刻意偏差（维护者明确裁决）：已在 `移植基本准则.md` §5.4 登记
- [ ] 本 PR 不涉及 UI / 用户可见行为（构建、CI、文档、测试）

> 若为「Mini 源码里没有的新行为」，请说明依据；1:1 移植不擅自新增 Mini 没有的功能。

## 验证 / Verification

- [ ] 构建通过：`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build.ps1`（0 error）
- [ ] 全量隔离回归：`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run_tests.ps1` → `EXIT=0 / 0 failed / 0 assertion failures`
- [ ] `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ui_src_gate.ps1` PASS（若涉及 UI 源码）
- [ ] `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release_preflight.ps1` PASS（7 道门禁全绿）
- [ ] 新增或修复的缺陷已补回归用例，并做过「临时回退 → 用例失败 → 恢复后通过」的有效性验证
- [ ] 已验证与 Clash Mini 的同机共处与运行期互斥行为正常（端口 10801 / 10802 / 10803 / 7899 / 9098 / 33335 对齐）

## 交付链 / Delivery

- [ ] 未把 `sidecar/*.exe`、`*.pdb`、`build/`、`dist/` 等二进制或构建产物加入提交
- [ ] 未手工篡改更新通道配置（更新通道与签名机制见 `docs/RELEASE_PROCESS.md` 与 `docs/KNOWN_LIMITATIONS.md`）
- [ ] 若涉及品牌位，显示名为 `Clash Win`（中间有空格），未残留 `Clash Mini` 显示名

## 关联 / Related

- Closes #
- 相关 Issue / 讨论：
