# ClashWin 阶段四：切片一（配置持久化与多订阅管理）验收报告

> **归档位置**：`docs/09_slice1_config_persistence_verification.md`  
> **关联清单项**：Phase 4.1 切片一：配置持久化与多订阅管理  
> **测试程序**：`build/bin/test_slice1_config.exe`  
> **模块代码**：`src/config/app_config.cpp`, `src/config/node_filter.cpp`, `src/config/profile_manager.cpp`

---

## 1. 验收测试执行概况

2026-10-24 成功执行 C++20 原生切片一全量验收测试，测试通过率 100%：

```
====================================================
[ClashWin 切片一] 配置持久化与多订阅管理验收测试
====================================================
[PASS] 1.1 广告与废弃通知节点正确过滤
[PASS] 1.2 包含敏感字样的正常出口节点绝不误杀 (杜绝 contains 陷阱)
[PASS] 2.1 verge.yaml 配置项序列化与反序列化完全一致
[PASS] 2.2 window_state.json 几何状态持久化与 0x0 守卫防御生效
[PASS] 3.1 首个 Profile 导入成功，生成独立文件 p_126776.yaml
[PASS] 3.2 第二个 Profile 导入成功，profiles.yaml 列表中共计 2 项
[PASS] 3.3 成功切换回 Profile 1，并重新加载其对应节点列表
[PASS] 3.4 节点活跃状态快照保存并在 profiles.yaml 中对账一致
[PASS] 3.5 成功删除 Profile 2 并清理本地存储文件

>>> 切片一: 配置持久化与多订阅管理全项 100% 验收通过！<<<
```

---

## 2. 核心技术落地核对

### 2.1 NodeFilter 杜绝误杀防线
- **英文词汇子串匹配**：`traffic`, `expire`, `renew`, `reset`, `left`, `http://`, `https://`, `sub.`, `t.me/`。
- **中文广告词严格前缀比对**：去除空格后通过 `starts_with` 判定 `剩余流量`, `过期时间`, `重置时间`, `购买订阅`, `套餐到期`, `官网`, `发布页`, `通知`, `公告`, `更新`。
- **抗误杀验证**：诸如 `"HK-流量优化专线"`、`"SG-官方中继 01"` 等真实节点绝不被误判。

### 2.2 AppConfig (verge.yaml & window_state.json)
- **verge.yaml 序列化**：支持主题、双滑块参数、`rule_fallback` 三态路由（`direct`/`proxy`/`addurl`）、系统代理开关、TUN 模式开关、开机自启与混合端口 `10801`。
- **window_state.json 节流落盘与 0x0 守卫**：
  - 1000ms 周期节流 + 尾随补写（Trailing Write Armed），确保拖拽中途与终值绝对不丢失。
  - 严格拦截 `< 271px` 或 `< 126px` 或 `0x0` 的异常销毁事件，防止有效窗口坐标被脏数据污染。

### 2.3 ProfileManager 多订阅系统
- **独立文件存储**：每个订阅保存至 `profiles/{uid}.yaml`，元数据记入 `profiles.yaml`。
- **平滑切换与快照恢复**：`SwitchProfile` 切换配置时重新生成 `config.yaml`，并恢复先前在该订阅下记录的活跃选中节点 `selected_proxy`。
- **级联删除与自动回退**：删除当前活跃配置时自动退回首个可用配置，避免空状态悬挂。
