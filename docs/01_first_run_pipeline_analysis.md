# 📑 第一步成果物：Clash Mini 首次冷启动（First-Run）与订阅导入全链路精读逆向报告

> **所属阶段**：阶段一：源系统全景解构与功能拓扑清单（Source Deconstruction & Functional Inventory）  
> **对应清单项**：`1.1.1 首次冷启动（First-Run）链路`  
> **逆向参考目标（只读）**：[`c:\Users\sun_y\Documents\AntiGravity_Projects\ClashVerge`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge)  
> **文档定位**：记录 Clash Mini 冷启动、初次使用判定、订阅输入 UI、网络拉取、多级解析及内核首次激活的 100% 真实源码逻辑，为后续 C++20 原生复刻提供绝对准确的技术依据。

---

## 目录
1. [一、冷启动与初次使用判定逻辑（Detection）](#一一冷启动与初次使用判定逻辑detection)
2. [二、首次运行引导对话框 UI 规范（FirstRunGuideDialog）](#二二首次运行引导对话框-ui-规范firstrunguidedialog)
3. [三、订阅网络拉取与安全防护（Network & SSRF）](#三三订阅网络拉取与安全防护network--ssrf)
4. [四、通用订阅内容三级回退解析器（Universal Parser）](#四四通用订阅内容三级回退解析器universal-parser)
5. [五、配置落盘与首选自动激活（Save & Auto-Activation）](#五五配置落盘与首选自动激活save--auto-activation)
6. [六、内核初次拉起与连通性验证（Core Launch & Readiness）](#六六内核初次拉起与连通性验证core-launch--readiness)
7. [七、C++20 原生移植映射方案（Native Mapping Architecture）](#七七c20-原生移植映射方案native-mapping-architecture)

---

## 一、冷启动与初次使用判定逻辑（Detection）

### 1.1 源码位置
- 前端检测点：[`src/pages/_layout.tsx`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src/pages/_layout.tsx) L340–L355
- 判定辅助函数：[`src/utils/profile-items.ts`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src/utils/profile-items.ts) L1–L27
- 本地存储标记：`localStorage.getItem('clash-mini-first-run-completed')`

### 1.2 关键业务铁律（严防判定失效）
> [!IMPORTANT]
> **绝对不能使用 `profiles.items.length === 0` 作为首次启动的判定条件！**

**根因机制**：
后端在初始化配置 `Config::init_config()` 时，每次启动都会**无条件**向 `profiles.items` 注入 `Merge`（`type: merge`）与 `Script`（`type: script`）两个内置处理项。因此，`profiles.items.length` 的长度**恒定 $\ge 2$**。

**正确判定准则**：
1. 检查本地是否已有完成标记：`clash-mini-first-run-completed === 'true'`。
2. 检查 `profiles.items` 中是否存在**真实订阅项（Real Profile Item）**：
   $$\text{isRealProfile}(item) \iff item.type \in \{\text{"remote"}, \text{"local"}\}$$
3. 当且仅当 **`!guided && !hasRealProfileItem(profiles.items)`** 时，判定为真实冷启动，延时 500ms 弹出首次运行引导弹窗 `FirstRunGuideDialog`。

---

## 二、首次运行引导对话框 UI 规范（FirstRunGuideDialog）

### 2.1 源码位置
- [`src/pages/_layout/components/first-run-guide-dialog.tsx`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src/pages/_layout/components/first-run-guide-dialog.tsx)

### 2.2 两阶段状态机（Two-Step State Machine）
引导弹窗内部维护严格的两阶段流转状态机：`step: 'input' | 'ready'`。

```
[用户打开程序]
      │
      ▼
 [无真实订阅?] ──No──> [直接进入常规主视口]
      │ Yes
      ▼
 ┌──────────────────────────────────────────────┐
 │ 状态 1: step = 'input' (订阅输入与导入阶段)   │
 │ - 欢迎标题与简要说明                         │
 │ - 订阅 URL 输入框 (多行自适应, 1~4 行)       │
 │ - 右键上下文菜单: 剪切/复制/粘贴/全选/清空   │
 │ - 按钮组: 📋 粘贴 / 稍后再说 / 导入          │
 │ - 全局回车快捷键监听 (排除中文输入法合成)     │
 └──────────────────────┬───────────────────────┘
                        │ 点击导入 / 回车
                        ▼
                 [WinHTTP 下载并解析]
                        │ 成功
                        ▼
 ┌──────────────────────────────────────────────┐
 │ 状态 2: step = 'ready' (内核与节点就绪检测)  │
 │ - 每 1.5 秒轮询一次代理状态与活跃节点延迟     │
 │ - 判定条件: activeDelay > 0 && < 10000ms     │
 │ - 20 秒超时容错回退机制                      │
 │ - 确认完成: 写入标记 'clash-mini-first-run'  │
 └──────────────────────┬───────────────────────┘
                        │ 完成
                        ▼
                [进入主界面, 节点已就绪]
```

### 2.3 视觉与交互规范
1. **输入框（TextField）**：
   - 占位符提示：`填入订阅链接/节点配置...`
   - 输入框右键呼出原生拟物上下文菜单：`剪切`、`复制`、`粘贴`、`全选`、`清空`。
   - 自动聚焦：弹窗弹出 100ms 后自动执行输入框 `focus()`。
2. **快捷粘贴（📋 粘贴）**：
   - 读取系统剪贴板文本，自动执行 `trim()` 并填充至输入框，自动清除此前可能存在的错误提示。
3. **回车提交全局化（BUG-299 防御）**：
   - 键盘监听器挂在 Dialog 顶层容器，而非单一挂在 input 上（防止用户点击提示文本导致焦点移出输入框后回车无效）。
   - 明确过滤中文输入法组词事件：`e.nativeEvent.isComposing === false`。

---

## 三、订阅网络拉取与安全防护（Network & SSRF）

### 3.1 源码位置
- [`src-tauri/src/config/prfitem.rs`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src-tauri/src/config/prfitem.rs) L427–L530

### 3.2 请求参数与协议规范
- **User-Agent**：默认携带规范的客户端 UA（如 `clash-verge/v...` 或自定义 UA）。
- **超时控制**：默认设置 20 秒请求超时。
- **重试机制**：首次直连拉取失败后，支持自动降级通过已有本地代理重试。

### 3.3 SSRF 严格安全防御（必做）
在发出网络请求之前，必须执行 `validate_url_no_ssrf(&url)` 严格预检：
1. **协议限制**：仅限 `http://` 与 `https://`。
2. **回环与私有地址阻断**：
   - 阻断域名：`localhost`、`127.0.0.1`、`::1`、`0.0.0.0`、`169.254.*`。
   - 阻断 IPv4 私有段：`10.0.0.0/8`、`172.16.0.0/12`、`192.168.0.0/16` 及链路本地地址。
   - 阻断 IPv6 本地段：`fe80::/10`、`fc00::/7`。

### 3.4 响应头解析提取（Subscription Metadata）
拉取成功后，不仅读取正文，更要从 HTTP Response Headers 中提取关键元数据：
1. **流量与配额信息**：
   - 检索 `subscription-userinfo` 头（或兼容 `x-amz-meta-subscription-userinfo` 等带前缀的变种头）。
   - 提取并解析键值对：`upload=...; download=...; total=...; expire=...`。
2. **订阅名称识别**：
   - 从 `Content-Disposition` 解析 `filename*`（RFC 5987 百分号编码 UTF-8 解码）或 `filename`。
   - 若无，回退提取 URL 路径的最后一段文件名；最终兜底为 `"Remote File"`。
3. **自动更新间隔**：
   - 解析 `profile-update-interval` 头，单位为小时，转换存储为分钟数。

---

## 四、通用订阅内容三级回退解析器（Universal Parser）

### 4.1 源码位置
- [`src-tauri/src/config/prfitem.rs`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src-tauri/src/config/prfitem.rs) L752–L785
- [`src-tauri/src/utils/resolve/universal_parser.rs`](file:///c:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge/src-tauri/src/utils/resolve/universal_parser.rs)

### 4.2 三级解析时序
```
                  [原始响应字符串 data]
                            │
                            ▼
           [尝试 1: 直接作为 YAML 文本反序列化]
                            │
              包含 proxies 或 proxy-providers?
              ├── Yes ──> [解析成功，直接采纳]
              └── No
                    │
                    ▼
          [尝试 2: Robust Base64 解码并转 UTF-8]
                    │
               解码后文本包含 proxies 键?
               ├── Yes ──> [解析成功，采纳解码后 YAML]
               └── No
                     │
                     ▼
           [尝试 3: 按行 URI 协议解析 (Universal Parser)]
                     │
           逐行匹配: ss://, vmess://, vless://,
           trojan://, hysteria2://, tuic://, wireguard://
                     │
             至少成功解析出 1 个有效节点?
             ├── Yes ──> [封装为标准 proxies 列表并序列化为 YAML]
             └── No  ──> [抛出格式错误: 既非合规 YAML 也非节点列表]
```

### 4.3 协议拆解核心铁律（先 Query 后 Userinfo）
> [!CAUTION]
> **在解析诸如 `trojan://password@host:port?sni=domain@sni` 等协议 URL 时，必须严格遵循「先以 `?` 剥离 Query 参数，再以 `@` 拆分认证密码与主机」的顺序！**  
> 若先按 `@` 切割，会导致含有 `@` 字符的混淆参数或密码把主机地址截断破坏。

---

## 五、配置落盘与首选自动激活（Save & Auto-Activation）

### 5.1 目录与文件布局
- 应用数据主目录：Windows 下位于 `%APPDATA%\io.github.clash-mini.clash-mini\`（便携模式位于可执行程序同级 `.config\io.github.clash-mini.clash-mini\`）。
- 订阅文件存储路径：`<app_home>\profiles\<uid>.yaml`（远程订阅生成唯一随机 UID，如 `R1a2b3c.yaml`）。
- 订阅元数据清单：`<app_home>\profiles.yaml`。

### 5.2 自动激活机制
1. 若 `profiles.yaml` 中原先的 `current` 字段为空（即系统无已激活配置）：
   - 将新导入配置的 UID 设为 `current`。
2. 触发 **`update_config_forced()`**：
   - 读取 `<app_home>\profiles\<current_uid>.yaml`。
   - 执行单组压平增强：提取全部有效节点，聚合到单一 `PROXY` 选择组和隐藏的 `PROXY__METRICS` 组中。
   - 注入本地运行参数（Mixed Port: `10801`，Controller API: `127.0.0.1:9098`，Secret 秘钥）。
   - 将最终运行配置写入 `<app_home>\config.yaml`。

---

## 六、内核初次拉起与连通性验证（Core Launch & Readiness）

### 6.1 内核拉起
- 可执行文件：`mini-mihomo.exe`。
- 命令行参数：`mini-mihomo.exe -f <app_home>\config.yaml -d <app_home>`。
- **Win32 Job Object 绑定**：创建进程时分配独立作业对象，设置 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`，保证主程序退出或崩溃时内核 100% 自动殉爆销毁，零孤儿进程残留。

### 6.2 连通性探针轮询
- 弹窗进入 `step: 'ready'` 状态后，以 1500ms 间隔向 `http://127.0.0.1:9098/proxies` 发送原生 HTTP GET 请求。
- 检索 `PROXY` 组下的当前激活节点 `now`，读取其历史测速延迟数据。
- 一旦检测到有效延迟（`delay > 0 && delay < 10000`），界面指示灯变绿，提示“节点已连接”，用户点击“开始使用”完成首次向导并持久化标记。

---

## 七、C++20 原生移植映射方案（Native Mapping Architecture）

在 C++20 / Win32 / Direct2D 下，本链路的原生模块划分如下：

| 功能职责 | 源系统实现 (ClashVerge) | ClashWin C++20 原生映射实现 |
| :--- | :--- | :--- |
| **首次启动判定** | React `useEffect` + `hasRealProfileItem` | `ConfigManager::HasRealProfiles()` + 注册表/本地轻量 JSON 存储 |
| **引导弹窗 UI** | MUI Dialog + `FirstRunGuideDialog.tsx` | Direct2D 实体层级模态对话框 `DrawFirstRunGuideDialog()` |
| **剪贴板交互** | `@tauri-apps/plugin-clipboard-manager` | Win32 原生 `OpenClipboard` / `GetClipboardData(CF_UNICODETEXT)` |
| **SSRF 防御** | `validate_url_no_ssrf` (Rust url crate) | 原生 C++ URL 规范化与 Win32 `InetPton` / 私有 IP 段位运算过滤 |
| **网络下载引擎** | Rust `reqwest` 异步客户端 | Win32 原生 `WinHTTP` (支持 Keep-Alive、自定义 Headers 与超时) |
| **三级解析器** | `parse_subscription_content` | 原生 C++20 YAML 流解析器 + Base64 快速解码 + 节点 URI 解析器 |
| **配置增强与落盘** | `enhance::merge_default_config` | 原生 C++20 配置编译引擎（输出单 `PROXY` 组 `config.yaml`） |
| **进程守护与拉起** | Rust `std::process::Command` | Win32 `CreateProcessW` + Windows Job Object（安全绑定防孤儿） |
| **就绪轮询探针** | React `setInterval` + REST `/proxies` | WinHTTP 异步工作线程轮询 + Direct2D 响应式刷新通知 |
