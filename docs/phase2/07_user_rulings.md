# 阶段二遗留 6 问 · 用户裁决（2026-10-25 00:32）

对 docs/phase2/05_cpp20_architecture.md §10 六项待明确事项的最终裁定：

1. **客户端自更新**：Clash Win 已有 GitHub 项目空间，自动更新逻辑**完全模仿 Clash Mini**（GitHub Releases + 检查/下载/安装同构）。
2. **clash-verge-service**：**直接复用** sidecar/ 内原版 clash-verge-service.exe（服务名/管道/X-IPC-Magic 不变），不重写服务。
3. **依赖方式**：授权架构师自定——**按设计全 vendored**（yaml-cpp / QuickJS / nlohmann-json 源码进仓库，不用 vcpkg）。
4. **高 DPI**：Per-Monitor-V2 + 96dpi 逻辑像素；非 96dpi 缩放出现像素偏差时，以**实机对照裁决**优先。
5. **解锁检测**：默认**走当前选中节点代理**（与 test_delay 同策略）。
6. **上游遗留字段**（enable_memory_usage 等）：**配置里保留、UI 不挂载**。

§10 待明确事项清零，工程阶段启动。
