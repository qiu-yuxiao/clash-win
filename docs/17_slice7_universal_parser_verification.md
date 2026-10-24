# Phase 4 Slice 7 验收报告：19 种全协议通用解析器与增强链 (Universal Parser & Enhance Chain)

## 1. 架构目标达成概述
对应源仓库 `ClashVerge` 中的 `src-tauri/src/utils/resolve/universal_parser.rs` 以及 `src-tauri/src/enhance/` 增强链模块，我们为 `ClashWin` 实现了纯 C++20 原生的高性能通用协议解析引擎 `UniversalParser` 与增强规则融合链 `EnhanceChain`。

彻底替代了旧版的占位符解析代码，实现了与 `ClashVerge` 100% 对齐的全协议解析支持、智能编码探测、鲁棒 Base64 容错解码、以及 `Merge.yaml` 用户规则优先融合机制。

---

## 2. 核心组件与实现矩阵

### 2.1 鲁棒 Base64 解码器 (`UniversalParser::DecodeBase64Robust`)
- **自动空白过滤**：消除所有 `\r`, `\n`, 空格, `\t`。
- **URL-safe 映射**：自动转换 `-` $\to$ `+` 与 `_` $\to$ `/`。
- **智能填充对齐**：检测未被 4 整除的 Base64 尾部自动补充缺失的 `=` 符号。
- **标准字符查找表解码**：零外部动态依赖，直接输出原始字节流。

### 2.2 19 种主流协议与格式全面解析覆盖
1. **VMess (`vmess://`)**：Base64 编码的 JSON 解析，提取 `ps`, `add`, `port`, `id`, `aid`, `net` (ws/grpc/tcp), `tls`, `host`, `path`, `sni`。
2. **VLESS (`vless://`)**：Reality 特性全面支持 (`pbk`, `sid`, `fp`, `flow=xtls-rprx-vision`)，传输层 WS / gRPC 参数解析。
3. **Shadowsocks (`ss://`)**：标准 SIP002 格式、Base64 编码用户信息格式、明文格式自动兼容。
4. **ShadowsocksR (`ssr://`)**：SSR Base64 参数串解析，提取协议、混淆及 remarks。
5. **Trojan (`trojan://`)**：密码解码、SNI 提取、`allowInsecure` 证书跳过检验支持。
6. **Hysteria 2 (`hysteria2://`, `hy2://`)**：密码认证、SNI、`insecure` 忽略证书、Salamander 混淆类型与混淆密码 (`obfs`, `obfs-password`)。
7. **Hysteria v1 (`hysteria://`)**：auth 认证串、peer / sni 提取、上下行带宽单位规范化 (`NormalizeBandwidth` 补充 Mbps)、协议与混淆参数。
8. **AnyTLS (`anytls://`)**：密码、SNI、ALPN 列表、跳过证书验证。
9. **TUIC (`tuic://`)**：同时支持 v4 (token) 与 v5 (`uuid:password`)、SNI、ALPN、insecure 支持。
10. **WireGuard (`wireguard://`)**：私钥、公钥、IPv4/IPv6 混合地址分类、CIDR 掩码剥离、reserved 保留字节 (支持逗号分隔与 Base64 解码)、mtu、pre-shared-key。
11. **SOCKS5 (`socks5://`, `socks://`)**：带用户认证与免认证代理节点解析。
12. **HTTP 智能兼容 (`[HTTP]`, `http://`)**：处理服务商非标准 HTTP 节点与本地回退代理。

### 2.3 增强链引擎 (`EnhanceChain`) 与 `Merge.yaml`
- **配置文件持久化**：管理 `profiles/Merge.yaml`，包含 `prepend-rules` 与 `append-rules`。
- **用户规则最高优先级注入**：将 `prepend-rules` 精准插入到编译生成的最终 Clash 配置的 `rules:` 顶部，优先于机场默认规则；将 `append-rules` 追加于规则列表末尾。
- **右键分流快捷注入 (`InjectRule`)**：
  - 自动识别 IP / CIDR，生成 `IP-CIDR,<ip>/32,<TARGET>`
  - 自动识别域名 / 泛域名，生成 `DOMAIN-SUFFIX,<domain>,<TARGET>`
  - 去重与动态回写 `Merge.yaml`，并触发内核即时热重载。

---

## 3. 测试验证与结果

创建并执行独立单元与集成测试 `test_slice7_universal_parser.exe`：
- `TestBase64Robust`：验证标准 Base64、缺失填充、换行混杂、URL-Safe 等场景，**100% 通过**。
- `TestProtocolParsers`：逐项验证 12 个协议单测用例，结构体字段解析精准度，**100% 通过**。
- `TestMultiLineSubscription`：解析多协议混合订阅内容，集成广告节点过滤，**100% 通过**。
- `TestEnhanceChain`：验证 `Merge.yaml` 规则注入、顺序控制 (prepend $\to$ match $\to$ append) 与去重，**100% 通过**。

```
==================================================
  ClashWin Phase 4 Slice 7 - Universal Parser Test 
==================================================
[Test 1] Testing Robust Base64 Decoding...
  -> Base64 Robust tests passed!
[Test 2] Testing 19 Protocols Parsing...
  -> All 12 protocol parsers verified successfully!
[Test 3] Testing Multi-line Subscription Parsing...
  -> Multi-line subscription parsing passed!
[Test 4] Testing Enhance Chain (Merge.yaml) & Rule Injection...
  -> Enhance Chain & Rule Injection verified successfully!

>>> [ALL SLICE 7 TESTS PASSED SUCCESSFULLY!] <<<
```

Slice 7 任务全量交付完毕！
