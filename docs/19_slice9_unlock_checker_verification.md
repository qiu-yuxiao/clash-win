# Phase 4 Slice 9 验收报告：主流 AI 与流媒体权威解锁检测体系 (Media Unlock Checker)

## 1. 架构目标达成概述
对应源仓库 `ClashVerge` 中的 `src-tauri/src/cmd/media_unlock_checker/`（共 14 个独立模块），我们在 `ClashWin` 中完成了纯 C++20 原生、高并发、基于 WinHTTP 的主流 AI 与流媒体权威解锁检测体系 `UnlockChecker`。

实现了完全对齐原版的 11 项平台解锁探测、Cloudflare Trace 出口归属地解析、国旗 Emoji 渲染、智能容错与全异步执行，杜绝 UI 线程阻塞。

---

## 2. 核心组件与实现矩阵

### 2.1 11 项核心平台解锁判定矩阵
1. **ChatGPT Web**：请求 `https://api.openai.com/compliance/cookie_requirements`，分析合规策略；结合 `https://chat.openai.com/cdn-cgi/trace` 提取出口国家/地区。
2. **ChatGPT iOS**：请求 `https://ios.chat.openai.com/`，检测是否出现 `Disallowed ISP`，`Request is not allowed` 判定放行。
3. **Claude**：请求 `https://claude.ai/cdn-cgi/trace`，提取 `loc=`，严格对照黑名单（CN, HK, MO, RU, IR, KP 等），放行判定 "Yes"。
4. **Gemini**：请求 `https://gemini.google.com`，匹配 `,2,1,200,"XXX` 3位国家代码（例如 USA, JPN, SGP），对照受限黑名单判定 "Yes"/"No"。
5. **YouTube Premium**：请求 `https://www.youtube.com/premium?hl=en`，分析响应体是否包含 `premium is not available` 或 `ad-free`/`browseid`，提取 `"GL":"XX"` 地区。
6. **Netflix**：并发请求 `https://www.netflix.com/title/81280792` 与 `title/70143836`，区分全量自制与非自制解锁 ("Yes")，抑或仅自制剧 ("Originals Only")。
7. **Disney+**：请求 `https://www.disneyplus.com`，分析重定向与放行状态。
8. **Spotify**：请求 `https://www.spotify.com/api/content/v1/country-selector?platform=web&format=json`，检测 403/451 封禁与服务可用性。
9. **TikTok**：请求 `https://www.tiktok.com/cdn-cgi/trace` 与落地页，提取 `loc=` 判定地域。
10. **哔哩哔哩港澳台**：请求 `https://api.bilibili.com/pgc/player/web/playurl?avid=18281381&ep_id=183799`，通过 `code: 0` 判定港澳台地区版权放行。
11. **巴哈姆特动画疯**：请求 `https://ani.gamer.com.tw/`，通过版权地域特征判定台湾原生放行。

### 2.2 WinHTTP 代理透传探针 (`WinHttpProbe`)
- 原生调用 `WinHttpOpen` 并配置 `WINHTTP_ACCESS_TYPE_NAMED_PROXY`，经由本地 `127.0.0.1:10801` 混合端口出站。
- 动态安全标志注入：支持绕过中间人证书误报，精准探测真实节点的国际出口连通性。
- 异步线程池隔离：通过 `StartCheckAsync` 触发，检测过程中状态机安全互斥，完成后通知前端更新。

---

## 3. 测试验证与结果

创建并执行独立单元与集成测试 `test_slice9_unlock_checker.exe`：
- `TestPlatformListAndRegionFormat`：验证 11 项平台名、国旗格式化映射，**100% 通过**。
- `TestUnlockCheckerLifecycle`：验证全量探针调度生命周期、离线与超时容错防御机制，**100% 通过**。

```
==================================================
  ClashWin Phase 4 Slice 9 - Media Unlock Checker 
==================================================
[Test 1] Testing Default Platforms & Region Formatting...
  -> Platforms and Region formatting passed!
[Test 2] Testing Unlock Checker Execution...
  -> Unlock Checker Lifecycle & Error Tolerance passed!

>>> [ALL SLICE 9 TESTS PASSED SUCCESSFULLY!] <<<
```

Slice 9 任务全量交付完毕！
