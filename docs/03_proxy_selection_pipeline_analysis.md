# 📑 第三步成果物：Clash Mini 代理管控与节点选择全链路精读逆向报告

> **所属阶段**：阶段一：源系统全景解构与功能拓扑清单（Source Deconstruction & Functional Inventory）  
> **对应清单项**：`1.1.3 代理管控与节点选择链路（Proxy & Selection）`  
> **逆向参考目标（只读）**：[`c:\Users\sun_y\Documents\AntiGravity_Projects\ClashVerge`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge)  
> **文档定位**：解构单一 PROXY 组模型架构、假节点/广告节点全链路过滤防线、节点切换与持久化时序、单点/批量延迟测试及主题色映射标准，为后续 C++20 原生复刻提供绝对准确的技术依据。

---

## 目录
1. [一、单一 PROXY 组与测量组双核模型架构](#一一单一-proxy-组与测量组双核模型架构)
2. [二、假节点与占位死节点全链路过滤防线](#二二假节点与占位死节点全链路过滤防线)
3. [三、活跃节点选择、持久化与切换生效时序](#三三活跃节点选择持久化与切换生效时序)
4. [四、节点延迟测速规范与阈值单一真源](#四四节点延迟测速规范与阈值单一真源)
5. [五、节点排序、搜索过滤与状态存储（proxy_head_state.json）](#五五节点排序搜索过滤与状态存储proxy_head_statejson)
6. [六、Direct2D 拟物节点卡片排版规范（ProxyItem）](#六六direct2d-拟物节点卡片排版规范proxyitem)
7. [七、C++20 原生移植映射方案](#七七c20-原生移植映射方案)

---

## 一、单一 PROXY 组与测量组双核模型架构

### 1.1 源码位置
- [`src-tauri/src/enhance/mod.rs`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src-tauri/src/enhance/mod.rs) L649–L751

### 1.2 核心架构设计意图
原版 Clash/Verge 往往维持复杂的策略组嵌套（如「节点选择 $\rightarrow$ 自动选择 $\rightarrow$ 香港节点 $\rightarrow$ 具体节点」），导致数据层级极深、状态同步竞态严重、内存占用高。

Clash Mini 在数据架构上彻底执行**单一 `PROXY` 组平铺折叠**，并在生成运行时配置（`enhance`）时建立双核组模型：

```
上游复杂配置 (含嵌套组 / Proxy-Providers)
                 │
                 ▼
     [enhance::enforce_mini_agreements]
                 │
                 ├───────────────────────────────┐
                 ▼                               ▼
    ┌────────────────────────┐      ┌────────────────────────┐
    │  控制组: "PROXY"        │      │  测量组: "PROXY__METRICS"
    ├────────────────────────┤      ├────────────────────────┤
    │ type: select           │      │ type: url-test         │
    │ proxies: [全部真实节点]  │      │ proxies: [同源全部真实节点]
    │ 职责: 用户/App 手动选点 │      │ url: cloudflare 204    │
    │ 特性: 稳定持久, 绝不回滚│      │ 职责: 内核按 300s 自动拨测
    └────────────────────────┘      └────────────────────────┘
                 │                               │
                 └───────────────┬───────────────┘
                                 │
                                 ▼
                     彻底替换原有全部 proxy-groups
```

### 1.3 核心设计铁律
1. **控制组 `PROXY`（`type: select`）**：
   - 供前端 UI 呈现与用户点击切换。
   - 若节点列表为空，兜底压入 `["DIRECT"]`，防止配置校验失败。
2. **测量组 `PROXY__METRICS`（`type: url-test`）**：
   - 包含与 `PROXY` 组完全一致的成员节点，设置 `interval: 300`。
   - **为什么独立设测量组**：在 TUN 模式下，如果 App 直接向活跃节点调用 `/proxies/{name}/delay` 自测，会导致流量回环并产生 `timeout` 假象（历史断流断测根因）。通过内核测量组拨测，数据直接维护在节点的 `history` 中，成为测速的唯一权威来源。

---

## 二、假节点与占位死节点全链路过滤防线

### 2.1 源码位置
- 前端过滤规范：[`src/utils/node.ts`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src/utils/node.ts) L1–L103
- 后端过滤规范：[`src-tauri/src/utils/node.rs`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src-tauri/src/utils/node.rs) L1–L70

### 2.2 广告/假节点判定准则（`is_dummy_node`）
机场订阅中常夹带宣传、通知或剩余流量节点。如果它们混入 `PROXY` 组且排在第一位，内核重启时会将当前节点重置为广告节点，导致断网。

过滤规则分为两层：
1. **高置信度子串过滤（英文/URL）**：
   - 节点名小写后若包含 `http://` 或 `https://`，或包含英文广告词：
     `"expire"`, `"traffic"`, `"website"`, `"subscribe"`
     $\implies$ **立即判定为假节点（Dummy）**。
2. **中文广告词严格前缀匹配（必须先归一化）**：
   - **多层剥壳归一化（Normalize）**：循环剥除外层包裹符号：
     `【 】`, `( )`, `（ ）`, `[ ]`, `「 」`
     以及前缀：`"节点-"`, `"节点:"`, `"节点："`, `"节点 "`。
   - **中文广告关键词列表**：
     `"流量"`, `"过期时间"`, `"网址"`, `"官网"`, `"剩余"`, `"套餐到期"`, `"续费"`, `"公告"`, `"购买"`, `"群"`。
   - **匹配准则**：
     > [!IMPORTANT]
     > 归一化后的名称必须满足 **`normalized == phrase || normalized.starts_with(phrase)`**。  
     > **绝对禁止使用 `contains / includes`**！否则诸如 `"HK-流量优化"`, `"日本-群节点-01"`, `"CN2-购买入口"` 等真实可用专线会被误杀！

### 2.3 占位死节点服务器拦截（`is_valid_node_server`）
部分机场会下发名称伪装成真实节点、但 `server: '1'` 的占位节点。该节点一旦排在首位，TUN 模式下全系统流量会打入死地址产生连接风暴，打满 CPU 导致 API 全部超时断网 80 秒（BUG-301）。
* **合法 Server 校验**：
  `server` 字段必须是非空字符串，且必须**成功解析为合法 IP 地址**，或**作为域名至少包含一个 `.`**。
* 不满足者，强制剔除出 `PROXY` 组成员名单！

---

## 三、活跃节点选择、持久化与切换生效时序

### 3.1 源码位置
- [`src/hooks/use-proxy-selection.ts`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src/hooks/use-proxy-selection.ts) L50–L120

### 3.2 节点切换时序与联动
当用户在界面上点击某个节点卡片时，系统严格执行以下时序：

```
[用户点击节点卡片: proxyName]
             │
             ▼
1. 立即调用 REST API: PUT /proxies/PROXY {"name": proxyName}
             │
             ▼
2. 写入本地 profiles.yaml 持久化:
   selected: [{ "name": "PROXY", "now": proxyName }]
             │
             ▼
3. 检查 verge.yaml 的 auto_close_connection 开关:
   ├── true  ──> 调用 DELETE /connections 清理前一节点的遗留连接 (防僵尸连接泄漏)
   └── false ──> 保持既有连接自然过渡
             │
             ▼
4. 触发 UI 刷新: 当前节点左侧呈现 3px 高亮竖条、字体加粗，原节点恢复普通样式
```

---

## 四、节点延迟测速规范与阈值单一真源

### 4.1 源码位置
- [`src/services/delay.ts`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src/services/delay.ts) L15–L370
- [`src-tauri/src/module/monitor.rs`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src-tauri/src/module/monitor.rs) L43–L100

### 4.2 阈值单一真源（严格保持前后端统一）
* **`NODE_DELAY_MIN_MS = 30`**：下限 30ms。延迟低于此值的节点视为机场伪造的虚假延迟节点。
* **`NODE_DELAY_MAX_MS = 2000`**：上限 2000ms。探针超时与判死阈值完全统一，超过 2000ms 一律判定为死节点（Timeout）。
* **`FAILED_SERVER_COOLDOWN_SECS = 600`**：失败服务器冷却时长 10 分钟。若某节点断流，其所在物理服务器上的所有节点在 10 分钟内避让选择，避免在同一台宕机机器的多个端口间死循环。

### 4.3 延迟数值状态与 UI 呈现规范
| 内部延迟数值 `delay` | UI 文本呈现 | 含义说明 |
| :---: | :---: | :--- |
| `-1` | `-` | 尚未进行延迟测试 |
| `-2` | `testing` (或加载圈) | 正在测速中 |
| `0` 或 `delay >= 2000 && delay <= 100000` | `Timeout` | 测速超时（视为不可用） |
| `delay > 100000` | `Error` | 握手或网络协议异常 |
| 正常数值 (如 `128`) | `128` | 正常延迟（单位 ms） |

### 4.4 主题色彩映射标准（`formatDelayColor`）
```cpp
if (delay < 0) return Color::DefaultText();
if (delay == 0 || delay >= 2000) return Color::ErrorRed();      // 红色: #f44336
if (delay >= 400)                return Color::WarningOrange();  // 橙色: #ff9800
if (delay >= 250)                return Color::PrimaryBlue();    // 蓝色: #2196f3
return                                  Color::SuccessGreen();   // 绿色: #4caf50 (< 250ms 优质节点)
```

---

## 五、节点排序、搜索过滤与状态存储（proxy_head_state.json）

### 5.1 存储文件规范
- 文件路径：`<app_home>/proxy_head_state.json`。
- 数据结构：
  ```json
  {
    "<profile_uid>": {
      "PROXY": {
        "open": false,
        "showType": true,
        "sortType": 1,
        "filterText": "香港"
      }
    }
  }
  ```

### 5.2 排序类型定义（`ProxySortType`）
* **`0` (Default)**：按订阅文件中的原始先后顺序排列。
* **`1` (Delay)**：按延迟从小到大升序排序；未测速（`-1`）、超时（`Timeout`）与错误（`Error`）自动沉底。
* **`2` (Alphabetical)**：按节点名称拼音/英文字母 A–Z 升序排序。

### 5.3 实时搜索过滤（`filterText`）
* 文本框输入关键词时，执行忽略大小写的包含检索：
  $$\text{match}(node) \iff \text{node.name.toLowerCase().contains(filterText.trim().toLowerCase())}$$

---

## 六、Direct2D 拟物节点卡片排版规范（ProxyItem）

### 6.1 窗口几何适配规则
根据原生窗口设计契约，节点列表在两种窗口宽度下自适应切换排版：

#### 1. 极窄隐匿模式（窗口逻辑宽 $\le 285\text{px}$）
```
┌─────────────────────────────────────────────────────────────┐
│ [协议 55px] │ [节点名称 (单行省略号截断)  flex: 1] │ [延迟 65px] │
└─────────────────────────────────────────────────────────────┘
```
- **左侧协议徽章**：宽度固定 55px，显示大写协议类型（如 `SS`, `VMESS`, `VLESS`, `TROJAN`, `HYSTERIA2`），右侧 2px 浅色分割线。
- **中间节点名称**：占据剩余宽度（flex: 1），单行超出显示 `...`，右侧 2px 分割线。
- **右侧延迟徽章**：宽度固定 65px，居中对齐，根据延迟大小显示绿/蓝/橙/红颜色文本。

#### 2. 标准与宽窗模式（窗口逻辑宽 $> 285\text{px}$）
```
┌─────────────────────────────────────────────────────────────┐
│ [节点名称 + 协议类型标签                 flex: 1] │ [延迟 65px] │
└─────────────────────────────────────────────────────────────┘
```

### 6.2 拟物视觉光影细节
- **行高**：每行高度 `24px`。
- **隔行斑马纹**：奇数行带 $4\%\sim 8\%$ 极浅底色，偶数行透明。
- **激活态（Active / Selected）**：
  - 左侧绘制 **3px 主题色高亮坚实指示条**。
  - 背景叠加 **$15\%\sim 35\%$ 主题色半透明拟物光影底色**。
  - 节点文字**加粗（Bold）**高亮。
- **悬停态（Hover）**：轻微提升亮度，手势指针切换为标准点击手柄。

---

## 七、C++20 原生移植映射方案

| 功能模块 | 源系统实现 (ClashVerge) | ClashWin C++20 原生映射实现 |
| :--- | :--- | :--- |
| **单一 PROXY 组构建** | `enhance/mod.rs::enforce_mini_agreements` | `ConfigCompiler::EnforceMiniAgreements()`（YAML 内存模型操作） |
| **假节点过滤** | `utils/node.rs::is_dummy_node` | 原生 C++20 `NodeFilter::IsDummyNode()`（大小写剥壳与前缀匹配） |
| **Server 校验** | `utils/node.rs::is_valid_node_server` | 原生 C++20 `NodeFilter::IsValidServer()`（Win32 `InetPton` 与域校验） |
| **选点与持久化** | `use-proxy-selection.ts` | `ProfileManager::SelectNode()` + 原生轻量 JSON 存储 |
| **测速调度中枢** | `services/delay.ts` (DelayManager) | 原生 C++20 `DelayManager` 单例（延迟缓存、30m 轮询清洗） |
| **并发批量测速** | `POST /group/PROXY/delay` + 24次退避 | 原生 `WinHTTP` 工作线程池并发探测 + 延迟数据统一回填 |
| **ProxyItem 渲染** | React `ProxyItem.tsx` | Direct2D 实体层级 `DrawProxyItem()`（双宽度模式自适应、3D 高亮条） |
| **ProxyHead 控制栏** | React `ProxyHead.tsx` | Direct2D 实体层级 `DrawProxyHead()`（⚡测速按钮脉冲、排序切换、输入框） |
