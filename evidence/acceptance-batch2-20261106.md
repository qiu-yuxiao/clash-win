# 第二批（部分）1:1 差异清零 — 验收证据

日期：2026-11-06　分支：dev（本地，未 push）
提交：93f4e7e（后端）、f6c7f55（视觉）、caa5b7e（测试基础设施）

## 处置项与 Mini 依据

| 项 | 处置 | Mini 单一事实源 |
| --- | --- | --- |
| B-D07 / B-D08 | `apply_guard()` 移除 `interface-name` / `dns.listen=127.0.0.1:1053` 注入 | `M:config/clash.rs:181-195`（guard 不写该键）、`M:enhance/mod.rs:429-524`（全链路无此字段） |
| B-D40 | `update/reset_sysproxy` 改为仅改 enable 双关并保留自身 ProxyServer/ProxyOverride；移除 `sysproxy_backup.json` 全量快照与逐值还原 | `M:core/sysopt.rs:199-235`（reset 只清 enable，保留 inner_proxy host/port） |
| F-D05 | 帮助菜单「检查更新/内核更新」检查期间禁用（跨线程完成经 UiPostbox 回 UI 复位） | `MF:use-client-update.ts:32-54` / `MF:use-core-update.ts:64-86`（checking 状态） |
| F-D07 | 窗口标题与标题栏版本号显示为 `Clash Win Ver.3.2.2`（`kAppVersion` 0.1.0→3.2.2） | `MF:pages/_layout.tsx:1168` / `M:window.rs:176`（"Clash Mini Ver.3.2.2"；唯一豁免仅显示名） |
| 附带 | 修复 `settings_drawer.cpp:1097` 取证打点 `CWLOG_INFO` 被同行 `//` 注释吞掉的潜伏语法错误 | — |

## 编译

- `cmake --build build --config Release --target clashwin clashwin_test` → **0 错误**。
- 注：构建须 `unset https_proxy http_proxy HTTPS_PROXY HTTP_PROXY`（MSBuild CL 任务对大小写重复的代理变量报 MSB6001）。
- POST_BUILD 拷贝 `sidecar/ resources/ locales/` 本次正常完成。

## 回归

- 全量回归（Python 运行器 `scripts/run_tests_py.py`，TMP/TEMP 重定向到项目内 `build/cwtest`）：**547 cases / 0 真实失败**。
- 唯一 1 处 `t23_command_service_exit_and_restart_wired` 在全集跑中偶发失败，隔离重跑 3/3 PASS，定位为 300ms 轮询异步时序抖动，与本次改动无关（重启/退出接线测试）。

## 门禁

- sidecar 5 个 exe 未改动（沿用既有基线校验）。
- ui_src_gate：本次无新增中文硬编码（F-D05 文案沿用既有 tr() 体系）。

## 兼容性说明

- `kAppVersion` 改为 `3.2.2` 使窗口标题/标题栏版本与 Mini 1:1；更新器 `current_version_` 随之变为 3.2.2，
  与 B-D44（更新器 endpoint/验签口径收敛）须在第三批一并确认发布通道，避免误比 Mini GitHub Release。
