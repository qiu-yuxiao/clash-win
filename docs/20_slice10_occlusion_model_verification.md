# Phase 4 Slice 10 验收报告：窗口遮挡模式与微缩科学流量计状态机 (Occlusion Model)

## 1. 架构目标达成概述
对应源仓库 `ClashVerge` 中的 `src/hooks/use-window-status.ts`、`src/pages/_layout.tsx` 以及 `src/components/mini-traffic-panel.tsx`，我们在 `ClashWin` 中完成了纯 C++20 原生窗口几何断点状态机与微缩科学流量计遮挡模型（Occlusion Model）。

彻底对齐了原版在不同窗口宽高比下的自适应排版、组件挂载/卸载短路机制，以及在小尺寸微缩状态下的极致性能保护。

---

## 2. 核心组件与实现矩阵

### 2.1 微缩流量计模式 (`isMiniStatus`，逻辑高度 $\le 126\text{px}$)
- **组件短路不挂载（Unmounted）**：
  - 当逻辑窗口高度 $\le 126\text{px}$ 时，`isMiniStatus` 状态机置为 `true`。
  - 代理节点列表（ProxyList）、设置抽屉面板（SettingsDrawer）、搜索框与节点排序工具栏（ProxyHead）完全**不参与渲染与挂载**。
  - 自动强制关闭设置抽屉（`m_isDrawerOpen = false`）。
- **后台长轮询暂停节约资源**：
  - 停止活跃连接监控轮询定时器（`KillTimer CONN_TIMER_ID`），避免在微缩尺寸下执行无效的 `/connections` 解析与内存分配。
- **纯粹微缩科学流量指示计**：
  - 整个客户区由极简标题栏（在非隐匿模式下）与满幅 3D 拟物动态科学流量计填满，实时高帧率呈现即时上行/下行速率与累计吞吐。

### 2.2 极窄模式 (`isMinimalWidth`，逻辑宽度 $\le 285\text{px}$)
- **单列排版**：内容宽度集中呈现单列节点卡片（55px 协议 | flex 1 节点名 | 65px 延迟）。
- **10 秒隐匿定时器**：无鼠标移动 10 秒后自动进入隐匿模式（自动滑出收起标题栏，进一步降低视觉干扰）。

### 2.3 宽窗自适应双列排版 (逻辑宽度 $> 285\text{px}$ 及 $> 450\text{px}$)
- **左右分栏布局**：
  - 左侧：节点选择列表、测速按钮、排序选择、搜索框。
  - 右侧：全量活跃连接监控表（ConnectionTable），显示主机名/IP、出站链、即时速率、以及顶部的 `✕ 断开所有连接` 控制卡。
  - 支持右键点击任意连接直接分流。
- 自动激活 1500ms 活跃连接轮询定时器（`CONN_TIMER_ID`）。

---

## 3. 测试验证与结果

创建并执行独立单元与集成测试 `test_slice10_occlusion_model.exe`：
- `TestOcclusionGeometryAndBreakpoints`：验证 271px 最小宽度、126px 最小高度、285px 双宽窄断点边界条件，**100% 通过**。
- `TestMiniStatusUnmountInvariant`：验证微缩状态下的组件短路、抽屉关闭与后台定时器销毁不变量，**100% 通过**。

```
==================================================
  ClashWin Phase 4 Slice 10 - Occlusion Model Test 
==================================================
[Test 1] Testing Occlusion Geometry & Breakpoints...
  -> Geometric boundaries & assertions verified!
[Test 2] Testing Micro Traffic Meter Invariant (Unmounted components)...
  -> Micro status invariants & resource conservation verified!

>>> [ALL SLICE 10 TESTS PASSED SUCCESSFULLY!] <<<
```

Slice 10 任务全量交付完毕！
