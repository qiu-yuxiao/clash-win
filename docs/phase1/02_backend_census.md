# ClashVerge (Clash Mini) Rust 后端全量普查报告

> 侦查员 scout-backend · 2026-10-24 · 参考源只读：C:/Users/sun_y/Documents/AntiGravity_Projects/ClashVerge
> 所有路径相对 src-tauri/（除 crates/）。未确认之处已标注。

## 1. 目录树普查（src-tauri/src/ 共 96 个 .rs，约 22,987 行；crates/ 约 4,224 行）

**src-tauri/src/**（按行数量级）：
- `utils/resolve/universal_parser.rs`（2008 行，最大文件：全部协议 URI 解析器）
- `module/monitor.rs`（1656：后台节点健康监测/自愈线程）
- `enhance/mod.rs`（1273：配置增强流水线）+ `enhance/{merge,script,seq,field,chain,tun}.rs`（71/241/291/71/135/139）
- `config/`（约 2700 行）：`prfitem.rs` 1082、`profiles.rs` 676、`clash.rs` 501、`verge.rs` 483、`config.rs` 414、`migrate.rs` 380、`encrypt.rs` 119、`runtime.rs` 20、`mod.rs` 40
- `core/`（约 3600 行）：`service.rs` 507、`core_updater.rs` 556、`validate.rs` 578、`manager/{mod,lifecycle,state,config}.rs` 134/460/202/173、`sysopt.rs` 265、`timer.rs` 449、`tray/{mod,menu_def}.rs` 298/6、`logger.rs` 264、`handle.rs` 113、`autostart.rs` 124、`notification.rs` 67
- `cmd/`（约 2200 行）：`diagnostics.rs` 502、`profile.rs` 443、`save_profile.rs` 430、`media_unlock_checker/` 11 个文件约 1000 行、`proxy.rs` 116、`clash.rs` 99、其余小文件
- `utils/`：`window_manager.rs` 742、`help.rs` 486、`init.rs` 388、`network.rs` 324、`resolve/{mod,window,window_script,scheme}.rs` 242/290/93/155、`dirs.rs` 198、`sysinfo.rs` 158、`node.rs` 172、`server.rs` 172、`singleton.rs` 63、`tmpl.rs` 48、`speed.rs` 71、`notification.rs` 73
- `feat/`：`config.rs` 276（patch_verge 联动核心）、`icon.rs` 341、`clash.rs` 190、`profile.rs` 182、`window.rs` 171、`proxy.rs` 112
- `lib.rs` 507（应用装配：插件注册、generate_handler 清单、WM_SIZING 子类化、窗口事件）、`main.rs` 32、`constants.rs` 91
- `module/`：`monitor.rs`、`lightweight.rs` 197；`process/async_handler.rs` 67

**crates/**：
- `tauri-plugin-mihomo`（约 3300 行含测试）：`mihomo.rs` 1304（REST/WS 客户端）、`models.rs` 1122、`ipc.rs` 505（命名管道客户端+连接池）、`commands.rs` 371（38 个 Tauri 插件命令）、`error.rs`/`utils.rs`/`lib.rs`
- `clash-verge-draft`（100）：Draft<T> 草稿-提交数据包装器
- `clash-verge-limiter`（165）：速率限制器（托盘点击防抖等）
- `clash-verge-logging`（131）+ `clash-verge-logger`：日志宏
- `clash-verge-signal`（120）：Windows Ctrl+C/Close/Shutdown/Logoff 信号注册
- `clash-verge-i18n`（104）：语言同步
- 另依赖外部 crate `clash-verge-service-ipc`（服务 IPC，源码不在本仓库内——未确认其内部实现，但从调用面可推断见 §5）

## 2. Tauri Command 清单（注册于 `src-tauri/src/lib.rs:126-174`；移植必须复刻的 IPC 契约）

**cmd/clash.rs**：`copy_clash_env`、`get_clash_info`（mixed/socks/http 端口+controller+secret+ipv6+allow_lan）、`patch_clash_config`、`patch_clash_mode`（rule/global/direct）、`start_core`/`stop_core`/`restart_core`、`test_delay`（直连或经 127.0.0.1:mixed-port 的 TCP+TLS 测延迟，失败返回 10000，超时 2000）、`get_clash_logs`、`close_all_connections`。

**cmd/verge.rs**：`get_verge_config`、`patch_verge_config`（经 feat/config.rs bitflags 联动）。

**cmd/profile.rs**：`get_profiles`、`enhance_profiles`（强制重新生成+校验运行时配置）、`import_profile`、`reorder_profile`、`create_profile`、`update_profile`、`delete_profile`、`patch_profiles_config`（切换 current，失败自动回滚 previous profile）、`patch_profile`、`view_profile`、`read_profile_file`、`get_next_update_time`。

**cmd/save_profile.rs**：`save_profile_file`（写文件→独立校验→失败回写原文→影响运行时则 update_config_forced）。

**cmd/proxy.rs**：`trigger_auto_select`（后端群发测速+择优切换）、`get_proxy_addr`（从 clash-mini.yaml/provider 文件查节点 server:port）、`save_proxy_head_state`/`get_proxy_head_state`（proxy_head_state.json）。

**cmd/app.rs**：`open_app_dir`、`open_core_dir`、`open_logs_dir`、`show_in_folder`、`open_web_url`（仅 http/https）、`open_app_log`、`open_core_log`、`exit_app`、`restart_app`、`get_portable_flag`、`get_app_dir`、`get_auto_launch_status`（读真实注册表状态）、`download_icon_cache`、`copy_icon_file`。

**cmd/system.rs**：`get_running_mode`（Service/Sidecar/NotRunning）、`frontend_log`。

**cmd/network.rs**：`get_sys_proxy`、`get_auto_proxy`、`get_system_hostname`、`get_network_interfaces`、`get_network_interfaces_info`、`is_port_in_use`。

**cmd/runtime.rs**：`get_runtime_config`、`get_runtime_yaml`、`get_runtime_exists`、`get_runtime_logs`。

**cmd/service.rs**：`install_service`、`uninstall_service`、`reinstall_service`、`repair_service`、`is_service_available`。

**cmd/lightweight.rs**：`entry_lightweight_mode`、`exit_lightweight_mode`。

**cmd/core_update.rs**：`check_core_update`（GitHub API mihomo latest release）、`start_core_upgrade`（下载+sha256 校验+热替换 cores/mini-mihomo.exe，进度事件）。

**cmd/diagnostics.rs**：`export_diagnostic_bundle`。

**cmd/media_unlock_checker/mod.rs**：`get_unlock_items`、`check_media_unlock`（12 项并发检测：B站陆/港台、ChatGPT、Claude、Gemini、YouTube Premium、Bahamut、Netflix、Disney+、Spotify、TikTok、Prime Video）。

**utils/sysinfo.rs**：`get_app_uptime`、`app_is_admin`。

注意：部分命令在文件中存在但未列入 lib.rs 注册表（死代码嫌疑，以 lib.rs 清单为准复刻）。

**tauri-plugin-mihomo 额外命令**（crates/tauri-plugin-mihomo/src/lib.rs:99-146）：`update_controller`、`update_secret`、`get_version`、`flush_fakeip`、`flush_dns`、`get_connections`、`close_all_connections`、`close_connection`、`get_groups`、`get_group_by_name`、`delay_group`、`get_proxy_providers`、`get_proxy_provider_by_name`、`update_proxy_provider`、`healthcheck_proxy_provider`、`healthcheck_node_in_provider`、`get_proxies`、`get_proxy_by_name`、`select_node_for_group`、`unfixed_proxy`、`delay_proxy_by_name`、`get_rules`、`get_rule_providers`、`update_rule_provider`、`get_base_config`、`reload_config`、`patch_base_config`、`update_geo`、`restart`、`upgrade_core`、`upgrade_ui`、`upgrade_geo`、`ws_traffic`、`ws_memory`、`ws_connections`、`ws_logs`、`ws_disconnect`、`clear_all_ws_connections`。

## 3. 核心进程管理（mihomo 内核）

- **定位**（`core/manager/state.rs:25-80` + `core/service.rs:208-229`）：优先 `app_home_dir()/cores/mini-mihomo.exe`（自定义热升级内核），否则 `current_exe().with_file_name("mini-mihomo.exe")`（sidecar）。`IVerge::VALID_CLASH_CORES = ["mini-mihomo"]`。
- **启动参数**（`state.rs::start_core_by_sidecar`）：`mini-mihomo.exe -d <app_home_dir> -f <clash-mini.yaml> -ext-ctl-pipe \\.\pipe\mini-mihomo`。环境变量：`GOMEMLIMIT`（默认 128MiB）、`GOGC`（默认 100）、`GOMAXPROCS`（默认 2）。
- **双运行模式**（`manager/mod.rs`）：`RunningMode::{Service, Sidecar, NotRunning}`，lifecycle_lock 串行化 start/stop/restart。TUN 开启且非管理员→必须走服务；服务未就绪→明确告警并回退系统代理（`lifecycle.rs::fallback_to_system_proxy`，BUG-298 教训：禁止静默降级）。
- **守护**：无 Job Object（全文无引用）。策略：(a) Sidecar 模式消费 stdout/stderr 写日志；(b) 启动/停止前 `kill_all_mini_cores()` 扫描杀掉名字含 "mini-mihomo" 的残留进程（`state.rs:156-183`）；(c) `module/monitor.rs` 后台监测线程：每 15s（重试 3s/离线 5s）体检，连续 2 次判死→自愈选点；自愈连续失败 3 次→内核 API 不可达则 `restart_core()`；连续 5 次失败弹 MessageBoxW 警报（`fire_self_heal_alert`）。
- **重启与恢复**（`lifecycle.rs::restart_core`）：重启前 `snapshot_proxy_group_now()` 快照 PROXY 组当前节点，重启后 `restore_proxy_group_now()`：等 API 就绪 30s→等组成员填充 20s→跳过内置出站与不在组内节点→90s 重试预算内 `select_node_for_group("PROXY", node)`，失败回退到 filterText 子集内首个非 dummy 节点（`select_fallback_in_subset`）。
- **配置热应用**（`manager/config.rs::apply_config`）：snapshot→`reload_config(force=true)`（PUT /configs?force=true）→restore→`Handle::refresh_clash()`；API 失败则整核重启兜底。
- **服务模式启动**（`service.rs::start_with_existing_service`）：先 `stop_clash()` 清残留，轮询等待 mixed-port 释放（50ms×最多 3s），payload=`ClashConfig{core_config:{config_path, core_path, core_ipc_path, config_dir}, log_config}` 发给服务，再 `start_clash()`。
- **退出**：`feat/clash.rs::restart_app` 与 `feat::quit()` 做 prepare_exit→clean_async（Config::apply_all_and_save_file、reset_sysproxy、停内核、停 Timer/monitor）；`clash-verge-signal` 注册 Ctrl+C/Close/Shutdown/Logoff 触发 quit。

## 4. 配置体系

**文件位置**（`utils/dirs.rs`）：全部在 `app_home_dir` = 便携模式 `<exe>\.config\io.github.clash-mini.clash-mini`，否则 `%APPDATA%\io.github.clash-mini.clash-mini`。核心文件：`config.yaml`（IClashTemp）、`verge.yaml`、`profiles.yaml`、`clash-mini.yaml`（运行时生成）、`clash-mini-check.yaml`（校验用）、`window_state.json`、`proxy_head_state.json`、`profiles/` 子目录（R*.yaml/L*.yaml/m*.yaml/s*.js/r*.yaml/p*.yaml/g*.yaml）、`logs/`。

- **verge.yaml = IVerge**（`config/verge.rs:23-192`）：config_version、app_log_level/max_size/max_count、language、theme_mode、env_type、start_page、startup_script、traffic_graph、enable_memory_usage、enable_group_icon、pause_render_traffic_stats_on_blur、menu_icon、menu_order、notice_position、collapse_navbar、enable_tun_mode、enable_auto_launch、enable_silent_start、enable_system_proxy、enable_proxy_guard、enable_bypass_check、use_default_bypass、system_proxy_bypass、proxy_guard_duration、proxy_auto_config、pac_file_content、proxy_host、theme_setting(IVergeTheme)、web_ui_list、clash_core、enable_always_on_top、home_cards、auto_close_connection、auto_check_update、default_latency_test、enable_auto_delay_detection(+interval)、enable_builtin_enhanced、proxy_layout_column、test_list、auto_log_clean、verge_mixed_port(10801)/verge_socks_port(10802)/verge_socks_enabled/verge_port(10803)/verge_http_enabled、enable_auto_light_weight_mode、auto_light_weight_minutes、enable_hover_jump_navigator(+delay)、enable_external_controller、rule_fallback("proxy"|"direct"|"addurl"，出厂 direct)。版本迁移：`migrate.rs`，verge v3 / clash v2 / profiles v1，迁移前强制备份。
- **config.yaml = IClashTemp**（`config/clash.rs`，Mapping）：模板含 config_version、tun{enable,stack=gvisor,auto-route,strict-route,auto-detect-interface,dns-hijack:[any:53,tcp://any:53]}、mixed-port 10801、socks-port 10802、port 10803、log-level warning、allow-lan false、ipv6 false、mode rule、tcp-concurrent false、dns{enable,cache-size 512}、profile{store-selected,store-fake-ip:false}、external-controller 127.0.0.1:9098、external-controller-pipe `\\.\pipe\mini-mihomo`、secret "adapted-by-qiu-yuxiao"、external-controller-cors、unified-delay。guard：端口 0/越界回退默认，controller 端口冲突自动寻 9098 起空闲口。
- **profiles.yaml = IProfiles**（`config/profiles.rs:18-28`）：{config_version, current, items: Vec<PrfItem>}。PrfItem（`prfitem.rs:20-64`）：{uid, type: remote|local|script|merge|rules|proxies|groups, name, file, desc, url, selected:[{name,now}], extra:{upload,download,total,expire}, updated, option:PrfOption{user_agent, with_proxy, self_proxy, update_interval, timeout_seconds(默认20), danger_accept_invalid_certs, allow_auto_update(默认true), merge, script, rules, proxies, groups}, home}。全局 Merge.yaml / Script.js 必然存在且受保护；每个 remote/local 自动挂 merge/script/rules/proxies 子项。文件名规则：R/L/m/s/r/p/g + uid + 扩展名。
- **Merge.yaml 逻辑**（`enhance/merge.rs::use_merge` + `enhance/seq.rs::use_seq`）：merge 支持 prepend-rules/append-rules/delete、字段覆盖；seq 对 rules/proxies 做 prepend/append/delete 数组操作。
- **订阅下载与解析**（`prfitem.rs::from_url`，427-610）：非 http(s) URL → 直连节点导入（`from_url_direct`，写入固定 L_Direct_Imports.yaml，支持 clear/clean 清空、URI-list 解析、按 name+server+port 去重合并）。http(s)：`fix_dirty_url` 修 & 前缺 ? → SSRF 校验（拒绝 localhost/私网/链路本地/ULA，含 DNS 解析后二次校验防重绑定）→ `NetworkManager::get_with_interrupt`（ProxyType::{None,System,Localhost}）→ 解析 Subscription-Userinfo 头（兼容 x-amz-meta- 等前缀）→ Content-Disposition filename* → profile-update-interval(时→分) → profile-web-page-url → 50MB 上限 → `parse_subscription_content`：三级回退 YAML→Base64 解码后 YAML→URI-list。
- **协议解析器**（`utils/resolve/universal_parser.rs`）：`parse_uri_list` 支持 vmess://（Base64 JSON）、ss://、trojan://、vless://、hysteria2://、hysteria://、anytls://、tuic://、wireguard://、socks5://(socks://)、http 兜底 `parse_http_fallback`；`decode_base64_robust`（standard/urlsafe/补padding）。
- **运行时配置生成**（`enhance/mod.rs::enhance()` 流水线，956-1022）：①收集 clash.yaml + 当前订阅 → ②`get_merged_proxies`：合并**所有** remote/local 订阅的 proxies，重名加 ` #n (HHMMSS)` 后缀 → ③全局 Merge/Script → ④profile 级 Rules/Proxies/Merge/Script → ⑤merge_default_config（clash.yaml 键覆盖；tun 深合并；dns/profile 仅补缺；socks/http 未启用则删键；redir-port/tproxy-port 跳过；config_version 剥离；external-controller 受开关控制）→ ⑥内置脚本 → ⑦`enforce_mini_agreements`：过滤 dummy 节点与 server 无效节点 → 注入唯一代理组 **PROXY(select)** 与测量组 **PROXY__METRICS(url-test, interval 300)**；注入 gfwlist rule-provider（jsdelivr Loyalsoldier clash-rules gfw.txt，interval 86400，走 PROXY）；规则链=prepend-rules + RULE-SET,gfwlist,PROXY + DNS fallback 上游保通规则（IP-CIDR/DOMAIN,PROXY，8.8.8.8↔8.8.4.4、1.1.1.1↔1.0.0.1 对偶补齐）+ IPv4/IPv6 私网 DIRECT 全套 + append-rules + （rule_fallback!=proxy 时）GEOIP,CN,DIRECT + IP-CIDR6,::/0,PROXY + MATCH,DIRECT（否则 MATCH,PROXY）→ ⑧`use_tun` → ⑨use_sort → ⑩`apply_mandatory_dns_settings`（仅补缺：nameserver=[doh.pub,alidns,114]、default-nameserver=[223.6.6.6,223.5.5.5,114]、proxy-server-nameserver，追加明文 223.5.5.5 保底）。
- **校验**（`core/validate.rs`）：生成 clash-mini-check.yaml，用 `mini-mihomo.exe -t -d -f` 子进程验证（超时 10s）；结果分类 Valid/Invalid{kind}/Skipped/Busy。
- **定时更新**（`core/timer.rs`）：DelayQueue 调度器，uid→interval 分钟（默认 1440），过期立刻补跑，失败 2 分钟后重试；与 CURRENT_SWITCHING_PROFILE 互斥。

## 5. 系统集成

- **系统代理**（`core/sysopt.rs`）：经 `sysproxy` crate 写 WinINET（HKCU\...\Internet Settings）。默认 bypass（sysopt.rs:54）：`localhost;127.*;192.168.*;10.*;172.16.*~172.31.*;<local>`，自定义 bypass 可追加或替换。PAC 模式 URL=`http://{proxy_host}:33335/commands/pac`。**顺序规则**（proxy_apply_steps）：纯全局代理先清 PAC 再设 Sysproxy；PAC 模式反之。代理守护：`sysproxy::GuardMonitor` 每 proxy_guard_duration(默认30s) 重写，防第三方篡改。
- **TUN**（`enhance/tun.rs::use_tun`）：不直接操作网卡，全部交给 mihomo 内核（gvisor 栈）；仅做配置注入：TUN 开时 dns 强制 enable=true、enhanced-mode=fake-ip（fake-ip-range 198.18.0.1/16、range6 2001:2::0/64、DEFAULT_FAKE_IP_FILTER 补缺）、顶层 ipv6 强制 true + tun.inet6-address=[fdfe:dcba:9876::1/126]（防 IPv6 泄漏）；关时 dns.ipv6 跟随用户开关。
- **服务模式**（`core/service.rs`）：服务名 `clash_verge_service`，服务二进制在 resources 目录 `clash-verge-service.exe` 及配套 install/uninstall 独立 exe（`dirs.rs::service_path`）。SCM 操作通过 `sc.exe query/start` 子进程 + `runas` 提权（CREATE_NO_WINDOW 0x08000000）；安装/卸载跑独立安装器 exe。就绪判定：IPC 管道存在 + `clash_verge_service_ipc::connect()`。运行时与服务的通信：`start_clash(ClashConfig)/stop_clash()/get_clash_logs()`（kode-bridge IpcConfig：timeout 150ms、retry_delay 250ms、max_retries 20）。就绪等待预算 SERVICE_WAIT_MAX=30s、间隔 200ms。
- **开机自启**（`core/autostart.rs` + lib.rs:118）：注册表 `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run`，键名 "Clash Mini"，命令行固定带 `--silent`（是否真静默由 enable_silent_start && has_silent_arg 双条件决定）。启动对账 `reconcile_on_startup()`：以配置值重写注册表；一次性清理历史 schtasks 任务（标记文件 .autostart_task_migrated）。
- **单实例**（`utils/server.rs`）：TCP 端口 33335（dev 33336），SO_REUSEADDR bind 抢占；bind 失败→向已有实例 GET `/commands/visible`（唤醒）或 `/commands/scheme?param=clash:...`（深链），然后 exit(0)。首个实例的 listener 复用为 warp embed server：`/commands/visible`、`/commands/scheme`、`/commands/pac`（PAC 文本，替换 %mixed-port%）。

## 6. 事件与 IPC

- **后端→前端事件**（`core/notification.rs`，window.emit，轻量模式下全部过滤）：
  - `verge://refresh-clash-config`（payload "yes"）
  - `verge://refresh-verge-config`
  - `verge://refresh-proxy-config`
  - `verge://notice-message`（(status, message)）
  - `profile-changed`（current_profile_id）
  - `verge://timer-updated`（profile_index）
  - `profile-update-started` / `profile-update-completed`（{uid}）
  - `verge://backend-delay-results`（{group:"PROXY", results:[[name,delay]...]}）
  - 内核升级进度事件名未确认（推断 `core-upgrade-progress`）
- **与 mihomo 内核交互**（`crates/tauri-plugin-mihomo`）：主通道为 **Windows 命名管道** `\\.\pipe\mini-mihomo` 上的 HTTP/1.1（hyper over NamedPipeClient），连接池 min 3 / max 16 / idle 30s / 健康检查 60s / 拒绝策略 Timeout 5s；ERROR_PIPE_BUSY 重试 24 次×125ms（BUG-171/172）。REST 端点全集（`mihomo.rs`）：GET /version、GET/PATCH /configs、PUT /configs?force=true、POST /configs/geo、GET /proxies、GET/PUT/DELETE /proxies/{name}、GET /proxies/{name}/delay、GET /group、GET /group/{name}、GET /group/{name}/delay、GET /providers/proxies(+PUT、/healthcheck、/{node}/healthcheck)、GET /rules、GET /providers/rules(+PUT)、GET/DELETE /connections、DELETE /connections/{id}、GET /dns/query?name=&type=A、POST /restart、POST /upgrade(,/ui,/geo)。**WebSocket**（同管道，Upgrade）：/traffic、/memory、/connections、/logs?level=。鉴权 Bearer {secret}。
- **前端与内核的 WS**：前端不直连内核，经插件命令 ws_traffic/ws_memory/ws_connections/ws_logs 由后端代理转发（转发事件名未确认）。

## 7. 托盘 / 窗口 / 单实例

- **托盘**（`core/tray/mod.rs`）：仅 2 项——"轻量模式 / Lite mode"（id `tray_lite_mode`，勾选动态文案）与 "退出 (Exit)"（id `tray_exit`）。图标三态：tray-icon-tun.png / tray-icon-sys.png / tray-icon.png（TUN > 系统代理 > 默认，include_bytes! 内嵌）。左键点击显示窗口（300ms 防抖 Limiter），tooltip "Clash Mini"。
- **窗口**（`utils/resolve/window.rs` + `utils/window_manager.rs`）：无框（decorations=false、shadow(false)、maximizable(false)），默认 271×680，MIN 271×126，MAX 640×860，加载完成才 show；背景色 #2E303D / #F5F5F5；初始化脚本注入主题。窗口几何持久化 `window_state.json`（内尺寸口径，1s 节流+尾随补写+版本号防乱序，0×0 守卫，位置脱屏则居中）。关闭按钮→隐藏窗口+进入轻量模式（非退出）。WM_SIZING 子类化钳制上边缘缩放（lib.rs:354-461）。激活窗口 trick：set_always_on_top(true)→(false)。轻量模式=销毁主窗口+clear_all_ws_connections（省内存），退出=重建窗口。

## 8. 移植到 C++20/Win32 的技术映射建议

- Tauri command IPC → 命名管道 + 自定义 JSON-RPC 分发表（一对一函数指针注册表）
- mihomo REST/WS over 命名管道 → `CreateFile(\\.\pipe\mini-mihomo)` + HTTP/1.1 over pipe；WS 同管道 Upgrade 帧（自研薄层，连接池用信号量+空闲队列复刻）
- 内核进程 → `CreateProcessW` 拼 `-d -f -ext-ctl-pipe`；守护 = Wait + 轮询 `Toolhelp32` 杀残留（原版未用 Job Object，行为对齐则不加）
- 服务模式 → `OpenSCManager/OpenService` 替代 sc.exe；`ShellExecuteW(runas)` 弹 UAC；服务 IPC 复用管道客户端
- 系统代理 → 写 `HKCU\...\Internet Settings`（ProxyEnable/ProxyServer/ProxyOverride/AutoConfigURL）+ `InternetSetOption(REFRESH/SETTINGS_CHANGED)` 广播；守护定时器复刻 GuardMonitor
- TUN → 保持零实现（配置注入 YAML 即可）；fake-ip filter/IPv6 防泄漏逻辑照抄 tun.rs
- 注册表自启 → `RegSetValueEx(HKCU\...\Run)`，键名 "Clash Mini"，带 --silent；启动对账照抄
- YAML → 引入 yaml-cpp；enhance 流水线逐函数移植（核心难点：enforce_mini_agreements 规则顺序必须逐条一致，否则 MATCH/GEOIP 行为漂移）
- JS 合并脚本 → 需嵌 QuickJS/duktape（模板仅 `function main(config, profileName)`）；或先降级"不执行+告警"并标记差异项
- 订阅下载 → WinHTTP + 可选经 127.0.0.1:mixed-port CONNECT；Base64/URI 解析器纯字符串逻辑直接移植
- 事件 emit → 窗口消息或 PostWebMessageAsJson（配合前端事件名契约）
- 托盘 → Shell_NotifyIcon + 2 ITEM 菜单；图标三态内嵌
- 窗口/轻量模式 → 纯 HWND：DestroyWindow/ShowWindow/SetForegroundWindow；WM_SIZING 钳制照抄 lib.rs:411-441；geometry 持久化照抄（节流+尾随补写）
- 定时器/监测 → std::jthread + condition_variable 定时队列替代 DelayQueue；monitor.rs 状态机常量逐个移植

## 附：关键行为常量速查
- 端口：mixed 10801 / socks 10802 / http 10803 / controller 9098 / 单例 33335
- 判死阈值：延迟 <30ms 或 ≥2000ms 判死；探针超时 2000ms；test_delay 错误值 10000
- monitor：正常 15s / 重试 3s / 离线 5s；宽限期 60s；自愈冷却 60s；失败服务器冷却 600s；history 新鲜度 60s；子集复用 30s；API 异常 3 次触发自愈
- 校验超时 10s；脚本执行超时 3s；服务等待 30s；配置更新防抖 300ms；订阅超时默认 20s、YAML 上限 50MB
- secret 默认 "adapted-by-qiu-yuxiao"；托盘点击防抖 300ms；窗口操作防抖 625ms（空闲 3s 豁免）

未确认项汇总：clash-verge-service-ipc 内部实现（外部 crate）、quit() 全文、scheme.rs 深链参数、core_updater 进度事件名、sysproxy crate 落注册表的具体键写法、前端 WS 转发事件名。
