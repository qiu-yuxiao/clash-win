# Changelog

All notable changes to **Clash Win** are documented here.

Version format follows the project's **own** line starting at `0.1.0` (constitution §5.3): Clash Win versions increment independently and are **not** bound to Clash Mini's `3.x` numbering (`v3.2.2` was the initial port reference anchor, now synced 1:1 through **v3.5.10**).

Format reference: `Changelog.md` in the Clash Mini repository (newest first, `## vX.Y.Z` headings with grouped change bullets). Content here is Clash Win's own history, not a copy of Mini's entries.

---

## v1.1.3 (2026-11-20)

### Upstream v3.5.10 Sync (`BUG-411` ~ `BUG-412`)
- **BUG-411 (Fast Restart on Missing Core Pipe, Skip Redundant API Ready Wait & Steady-State Counter Reset)**: Added zero-handle named-pipe missing detection (`is_pipe_missing_os_error` / `is_named_pipe_server_missing` / `MihomoClient::is_local_socket_missing`, strictly separating `ERROR_FILE_NOT_FOUND` / `ERROR_PATH_NOT_FOUND` / `ERROR_BAD_PATHNAME` from busy states) paired with `CoreManager::should_expect_core_pipe_alive()` (`ScopedConfigUpdate` & `lifecycle_` lock guard) and a `500ms` double-confirm debounce inside `CoreMonitor` to immediately trigger `restart_core()` instead of waiting through `3` API polling rounds and `30s` readiness waits; added `ApiChannelPrecheck` (`KeepAlive` / `ProceedAutoSelect` / `ConfirmedDown`) so dead API channels skip the `30s` `wait_for_clash_ready()` wait, and reset `auto_select_fail_count` / `api_dead_heal_fail_count` on steady `NoAction` recovery.
- **BUG-412 (600s Brownout Sliding-Window Escape, All-Stage Single-Target Fallback Skip & Dead-Node Connection Cleanup)**: Enabled `allow_single_target_skip = true` across all `evaluate_failover` probe phases when `>= 3` healthy real peers respond on the primary test URL so intermittent blackhole nodes cannot survive via fallback URL retries; added a `600s` brownout sliding window (`kBrownoutWindowSecs` / `kBrownoutConfirmFails = 2`) in `CoreMonitor::decide_self_heal` to escape flapping nodes after `2` independent `ShouldFailover` episodes; added `should_close_dead_node_proxy_connection` and `close_dead_node_proxy_connections` to cleanly close stale `PROXY` connections pinned to the confirmed dead/brownout node upon failover switch while leaving `DIRECT` / `REJECT` and healthy connections untouched.

## v1.1.2 (2026-11-20)

### UI Bugfixes & Pixel-Level Polish
- **3-State Tray Icon Asset Parity & Instant Mode Sync**: Updated `resources/icons/tray-icon*.ico` (`16×16` / `20×20` / `24×24` / `32×32`) from upstream PNG assets (Default dark grey-blue gear, System Proxy vivid green gear, TUN pink-blue square) and routed post-commit proxy mode changes and TUN fallback through thread-safe `Tray::sync_from_verge` so the system tray stays synchronized immediately regardless of lightweight `EventBus` filtering.
- **Help & Tools `?` Button Rendering**: Unblocked `Button::on_paint` delegation to `draw_button_text` for icon-only subclasses (`HelpIconButton`) and added the crisp vector `HelpOutlineRounded` SVG path to `IconW`, restoring the visible `?` icon in both bottom-bar (`>830px`) and top-right (`<=830px`) settings drawer layouts.
- **Maximize Button Breakpoints & Border Alignment**: Fixed `Breakpoints` notification ordering and synchronized `MainScene`, `TrafficPane`, `ContentStack`, and `SettingsDrawer` visibility directly inside `MainWindow::on_client_resized` (plus hiding `NodeList` when the settings drawer is open and omitting orphan column dividers on incomplete 3-column rows), ensuring clicking Maximize (`toggle_large`) updates layout cleanly with zero protruding double-border artifacts.
- **3-Column Active Node Label Natural Flow**: Updated `NodeRowWidget::on_paint` so the selected node name always occupies the left full column width while localized `- 当前节点` renders inline only when sufficient horizontal room remains, preventing narrow 3-column cards from squeezing out the node name.

### Upstream v3.5.8 ~ v3.5.9 Sync (`BUG-406` ~ `BUG-410`)
- **BUG-410 (Unified Width-Adaptive Real-Time Traffic Graph & Legacy Graph Removal)**: Unified narrow and wide windows onto a single persistent, logarithmic `CanvasGraph` (`63`-point rolling history buffer preserved seamlessly across both manual drag and titlebar maximize/restore, expanding smoothly from `30s` minimalist reference lines at `<=285px` to `30s -> 60s` with vertical linear-gradient area fills, `1M`/`10K`/`100B` log scale lines, `10s` time grid, and corner time badge in wide windows, plus `[2, height - 2]` stroke clamping and shrinkable lower-pane flex alignment in short wide windows); completely deleted legacy `EnhancedTrafficGraph`, `TrafficDataSampler`, and `draw_dashed_line` dead code.
- **BUG-406 (Named Pipe Pool Hygiene, 0ms WS Disconnect & Pre-Stop Cleanup)**: Added `PipeHttpClient::clear_idle()` around `PUT /configs` config reloads, enabled `0ms` immediate local named-pipe WebSocket handle release on `PipeWebSocket::close(0)` across `WsManager`, and wired `CoreManager::stop()` pre-stop cleanup to flush WS sockets and idle HTTP connections before stopping the core.
- **BUG-407 (Async Epoch-Cancellable Proxy Group Ensure & WS Reconnect Stagger)**: Added non-blocking `CoreManager::spawn_ensure_proxy_group_selection` with `selection_ensure_epoch_` cancellation and compliant snapshot persistence, removed premature `CoreMonitor::kick()` on core restart, deduplicated `refresh_clash` / `refresh_proxies` dispatches and `MainWindow` listeners, and added `400ms` WS refresh debounce plus `45ms` per-channel reconnect stagger.
- **BUG-408 (Session 0 Service Core Cleanup, Dead `restart_core` IPC Removal & Graceful NSIS Stop)**: Immediately stopped any autostarted Session 0 service core when `clash_verge_service` readies while running in Sidecar mode, removed the dead `restart_core` IPC command, and updated NSIS install/uninstall routines to issue graceful SCM `sc stop` prior to residual process termination.
- **BUG-409 (Global Merge Deduplication & `profile_affects_runtime` Fix)**: Prevented `enhance` from double-applying global `Merge.yaml` when `option.merge` is unset or equals `"Merge"`, fixed `profile_affects_runtime` to always treat global `"Merge"` as affecting runtime when a profile is active, and deduplicated custom routing rule backup imports under `LongOperationGuard`.

## v1.1.1 (2026-11-19)

### Upstream Clash Mini v3.5.7 Sync (`BUG-404` ~ `BUG-405`)
- **BUG-405 (Zero-Millisecond Compliant Exit Binding & Filtered-Out Migration)**:
  - Set `profile.store-selected: false` across built-in templates and runtime enhancement output so Mihomo no longer revives stale excluded cache entries on cold start, restart, or reload.
  - Added `effective_filter_text`, `pick_subset_fallback_node`, `promote_preferred_proxy_first`, and `apply_proxy_group_head_and_store_selected` / `sync_runtime_proxy_group_head` to promote the persisted compliant node (or compliant subset fallback) to `PROXY.proxies[0]` before writing `clash-win.yaml`.
  - Unified `resolve_desired_proxy_selection` and `ensure_proxy_group_selection` across `start_core`, `restart_with_restore`, `apply_config`, and `CoreUpdater` (with 50ms fast initial polling during the first 2s), followed by `close_excluded_proxy_connections` cleanup so no live proxy connection stays pinned to an excluded node.
  - Added `CoreMonitor::should_migrate_filtered_out_active` (`NodeMigration`) with incomplete-filter protection (`effective_filter_text` in `persisted_exit_is_usable`) and post-heal excluded connection cleanup.
- **BUG-404 (Dead IPC Removal & Visibility Refresh Convergence)**:
  - Removed dead IPC commands `get_runtime_config` and `uninstall_service` from `CommandService`.
  - Retired active visibility wakeup latency probing (`silent_test_on_wakeup`) on normal window show in favor of a `3000ms`-throttled passive `reload_nodes()` state sync, keeping active probing and self-healing centralized in `CoreMonitor` (while preserving `skipProbeIfFresh` on lightweight-mode wake).

## v1.1.0 (2026-11-19)

### 1:1 Naming, Packaging & Installer Migration Alignment
- **Aligned executable, install path & artifact naming**: Renamed the Windows binary to `clash-win.exe` (matching upstream naming), changed the default NSIS installation folder to `Clash Win` (matching `Clash Mini`), and standardized distribution archives to `Clash.Win_<version>_x64-setup.exe` and `Clash.Win_<version>_x64_portable.zip` while keeping runtime coexistence mutual exclusion intact.
- **Upgrade migration cleanup**: NSIS setup automatically cleans up legacy `v1.0.x` `ClashWin` / `clashwin.exe` remnants when upgrading in place without affecting side-by-side Clash Mini installations.

### Pixel-Level UI, Traffic Graph, Drawer & Modal Parity
- **Dual-mode responsive traffic graph**: Added dual-layout rendering to `EnhancedTrafficGraph`, switching between compact single-line and wide two-column canvases at the `450px` width threshold.
- **Settings drawer, notices & tray polish**: Added `adjusting` wait notices for basic toggles (`enable_auto_launch`, `enable_silent_start`, `enable_tun_sniffer`), post-patch registry verification warning (`settings.mini.autoLaunchVerifyFailed`) and UI rollback on `enable_auto_launch` mismatch, `auto_close_connection` cleanup when switching to Manual takeover mode, full mixed-port fallback resolution (`verge_mixed_port -> clash mixed-port -> 10801`), and cold-start silent tray notification support (`BUG-395`).
- **Unified panels & modal suite alignment**: Removed redundant `connections_view.*` (unified into `ConnectionsPanel`), aligned profile delete confirmation geometry (`BUG-390`), rebuilt `AddUrlDialog` (`TextField` + `32px` context menu), aligned `FirstRunGuideDialog` (`errImport`, `dismiss_via_close()`, deduplicated star glyphs), `EditProfileModal`, `ClientUpdateModal` / `CoreUpdateModal` (`versions_equal` up-to-date disabling, wheel scroll, progress events), `LogViewerModal` (`search_field_`, match highlights, WS pause/resume toggling), and `MainWindow::arrange_modal_overlays` on client resize.

### Upstream Clash Mini v3.5.4 ~ v3.5.6 Sync (`BUG-397` ~ `BUG-403`)
- **BUG-397 / BUG-398**: Aligned visible batch testing vs auto-select stale-connection cleanup and gated unverified HTTPS migration during active flowing traffic.
- **BUG-399 (Passive Freshness & Provider Lazy Check)**: Forced `proxy-providers.*.health-check.lazy: true`, set `PROXY__METRICS` `interval: 1800` and `lazy: true`, relaxed monitor freshness skip thresholds (`kPassiveTrafficMaxSkipSecs = 1800s`, `kIdleHealthFreshSecs = 900s`), and extended frontend `kHistoryFallbackMaxAgeMs` to `45 min`.
- **BUG-400 (Sufficient Verified Standbys Gate)**: Extended `kHttpsVerifyIntervalSecs` to `600s` and gated periodic HTTPS verification with `should_run_periodic_https_verify_with` (respecting active filter text, skipping dead-only unverified nodes, and stopping once active + `>= 3` standbys are `Alive` and verified).
- **BUG-401 (Single-Target Fallback Skip)**: Added `should_recheck_primary_with_fallback[_with]` across `CoreMonitor`, `DelayManager`, and `cmd_profile` (`kSingleTargetSkipFallbackMinHealthyPeers = 3`) to bypass redundant fallback-URL probes on isolated single-node timeouts when the primary URL is healthy across peers, while retaining fallback verification on second-strike death confirmation and multi-node batches.
- **BUG-402 (Zero-Kick Lightweight & Fresh Exit Skip)**: Removed `CoreMonitor::kick()` from `entry_lightweight_mode()` and removed the legacy 10s fallback coordination timer; added `is_proxy_history_fresh_and_valid` / `has_fresh_active_exit_latency` / `skip_probe_if_fresh` and passed filtered visible subsets to `AutoSelect::silent_test_on_wakeup`.
- **BUG-403 (Two-Phase Net Probe & Flowing Online Skip)**: Switched `probe_physical_network_with` to probe `223.5.5.5:53` (`250ms`) first before racing backup targets `119.29.29.29` / `1.1.1.1` (`600ms`) plus DNS fallback, and skipped redundant periodic TCP:53 probes while recent (`< 60s`) downstream flowing traffic is active.

## v1.0.3 (2026-11-19)

### Button Three-State Visuals (V8 closed)
- **Buttons now carry the whole `get3DButtonStyle` state model**: `ButtonPaint` gained three-state shadows (`ShadowLayer shadow[2]` / `shadow_hover` / `shadow_active`, with layer counts that differ per state), three-state glow (`glow_hover` / `glow_active`, distinguishable from "no glow in this state") and `border_disabled`. Hover now deepens the shadow and widens the glow the way the source does — retro-3d adds a *second* layer `0 4d 4d` and takes its glow from `8v/0.2` to `12v/0.4` (`0.15 -> 0.35` for error), original `1/3 .15 -> 2/6 .2`, modern-flat `2v/4v -> 3v/6v`, frosted `4/12 -> 6/16`, cyberpunk `5d -> 10d` — and press pulls it back (`0 d 2px`, `2v`, `2d`, glow gone in retro-3d). Disabled buttons keep each skin's own border colour instead of being drawn borderless.
- **One draw path instead of six**: new `src/ui/button_draw.h` (`draw_button_skin`) is now the only place a themed button is painted. `button.cpp`, `quick_bar`, `add_url_dialog`, `first_run_guide_dialog`, `connection_detail_dialog` and the three lambdas in `utility_modals` all delegate to it, which removed a set of invented coefficients: `shadow_blur × 1.3` / `glow_blur × 1.4` on hover, `alpha × 0.38` on the disabled border, `max(0.25, vibrancy)` on the frosted shadow, and quick-bar's white `12%/4%` + black `10%` + white `50%` press overlays.
- **Bevel edge direction corrected**: `0 1px 0 0 bevelShadowDark` / `0 1px 0 rgba(255,255,255,.8)` are not `inset`, so they sit on the bottom edge. The old code drew the white highlight at the top edge (`r.y + 1`); it is now the dark bevel line at the bottom, as the CSS stacks it.
- New test `t12_button_state_table_v8` pins the per-skin three-state layers, offsets, glow blurs and disabled borders.

### Popup Family & Six-Skin Button Audit (pixel-parity round 3)
- **Unified three-band popup geometry**: every narrow popup now derives its title / content / actions bands from `popup-styles.ts` — title `px 14 / py 10.8 / 20.925 line + 1px rule = 43.525`, content `py 12`, actions `py 10.8 / gap 8` — and buttons are sized by `fontSize × 1.75 + py × 2 + skinBorder × 2` instead of hard-coded heights.
  - *Edit profile*: input fields `36 -> 40` (`size="small"` = `8.5×2 + 23`), title `15 -> 15.5`, buttons `28 -> 37.55 / 33.28` (retro, border `2 × depth`), gap `6 -> 8`, card height `372 -> 376.45` (was a hard-coded number).
  - *Client update*: version rows `13px/18 -> 12.5px/17.5`, changelog box `12px × lineHeight 1.5 + p 10 + radius 6` (was 12.5px / radius 4 / p 8), progress bar now uses `subtleSurfaceBg + 1px subtleSurfaceBorder` instead of an invented 10% black fill, card height follows content with the changelog capped at 160.
  - *Core update*: regained the missing version card (`p 1.25 / radius 6 / subtleSurface bg + border`, 61.8 tall) and the first row's `secondaryText` colour.
  - *Antivirus whitelist*: title/actions `py 12 -> 10.8`, content `py 14 -> 12` (the `py 1.5` override belongs to `DialogContent` only), button height by formula.
- **Six-skin `get3DButtonStyle` branch gaps closed**: monochrome `error` regained its `#333333 / #cccccc` scale (had fallen into the default black/white branch); modern-flat `error` regained hover (`error.dark`) and border (`error.main`) — hover used to jump to grey; `error.main` is now read from the live palette instead of a hard-coded light-mode `#FF3B30` (dark mode is `#FF453A`); `error.dark` recomputed as MUI's `darken(main, 0.3)` instead of Material Red 900; modern-flat primary hover deepened from `0.15` to `0.3`; original primary hover gained its `brightness(0.95)`; retro-3d error glow corrected to `0.15 × vibrancy`.
- **`outlined` variant wired into button drawing**: secondary buttons in Mini are `get3DButtonStyle(theme,'outlined','default')`. In retro-3d that is a 2-stop translucent gold gradient with a deep-gold border, turning into a 4-stop opaque gold on hover — previously every button was drawn as `contained`, so cancel/save looked identical apart from the label. `Button` gains `theme_outlined_`; `ButtonPaint` gains `grad_hover_count` so the 2-stop normal / 4-stop hover pair interpolates without a visible jump.
- **Connection detail rebuilt to the Popover structure**: `p 16` (was 14), row box `12 × 1.4 = 16.8` with `gap 6` (was 24), removed the self-authored "view detail" heading, divider and the redundant Close button (click-away closes, matching MUI Popover; a11y children `3 -> 2`), the close-connection button now goes through `button_paint("error")` so all six skins colour it, at 13px with `padding 4×10` right-aligned, and card height is content-driven with the 480 cap.
- Help entry noted for follow-up dialog alignment.

## v1.0.2 (2026-11-19)

### Multi-line Input Line Box (V6 closed)
- **Render layer gains an explicit line box**: `TextStyle` now carries `line_h` (logical px). When set, `D2dRenderTarget::draw_text` / `measure_text` apply `SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, line_h, baseline)` with the baseline derived from the first line's own metrics (`baseline + (line_h - height) / 2`), i.e. Chromium's half-leading model — the glyph box is centred inside the line box instead of being pinned to the top. `max_lines` truncation now also measures with `line_h` instead of the old `size × 1.33` approximation.
- **Multi-line fields now lay out on the real 23px MUI line box**: the profile-import textarea (`multiline`) and the Add-URL dialog textarea render at 23px per row instead of DWrite's default ≈17.3px, so text finally fills the geometry that was already derived from `4 × 23` (P14 `48`, P16 `125`) instead of sitting 5.7px tighter per row. Caret stepping moves by 23, caret height keeps the `1.4375 × size` value (13 -> 19) vertically centred in the row, and selection height plus the placeholder share the same row box.
- Single-line fields are untouched (`line_h` defaults to 0 → DirectWrite's font metrics).

## v1.0.1 (2026-11-18)

### Upstream v3.5.3 Sync & Code-Level Pixel Parity
- **BUG-392 (v3.5.1) Filter Race**: Added `has_effective_filter_term` / `is_incomplete_filter_input` (mirrors `node.ts`), stopped arming the 700ms debounce on half-typed input, and introduced an in-flight re-test queue so a newly typed filter term is never silently dropped (`AUTO_SELECT_BUSY` retried up to twice).
- **BUG-393 (v3.5.1) Whitelist Copy**: Antivirus whitelist copy aligned to the 16 authoritative keys across 13 locales.
- **BUG-394 (v3.5.2) Single-Instance Probe**: Probe timeout raised `500ms` -> `1500ms` (upstream `server.rs:54`).
- **BUG-395 (v3.5.2) Silent Tray & Guide Trimming**: `should_silent` now follows `has_real_profile_items()`; the first-run guide's antivirus card was removed (-143 lines); the whitelist modal was rebuilt as a two-option card layout with silent-run / copy buttons.
- **BUG-396 (v3.5.3) Foreground Switch Cleanup**: New `core/connection_stale.h` closes only the previous node's connections under `PROXY` on every foreground switch point, plus a `proxyNode` row (with stale-node highlight) in the connection detail dialog.
- **Removed Self-Authored Extras**: Deleted 11 fabricated operation-result notices and 30 Win-only `layout.notices.*` locale keys across 13 languages.
- **Code-Level Pixel Parity (round 1)**: Established the MUI `content-box` conversion rule (`height + p:1px + border:1px` -> outer `+4`), corrected all four segmented controls (`122x24` / `26` tall, border stroked inside, segments split over the padding box, thumb inset 2), replaced the switch ink-baseline hack with the flex-centre model, re-derived card heights (`basic 141.9`, `theme 165.3`, `combo 128.8`), inset the theme-card dashed footer, switched the title-bar underline to `--divider-color`, and re-based the connections panel rhythm (search row `28/40`, table `76`, header `81`, rows `106`) with the header now inside the 5px double frame.
- **Code-Level Pixel Parity (round 2, MUI input box model)**: Second conversion rule — MUI's `InputBase` root carries `lineHeight: '1.4375em'`, which resolves to an **absolute 23px** at the root's 16px font size and is inherited verbatim by the `input`/`textarea` (`font: inherit`), so an inline `fontSize: 13px` does **not** shrink the line box back to 18.7. All input geometry re-derived from that 23px line box: Mixed-Port field `80x22.7` -> `88x27` (inline `width:80` is content-box, `+ pl4 pr4`), profile-import textarea `46` -> `48` (multiline+small root `p 8.5x2` + textarea `31`), card neck `124.4` -> `126.4`; basic card `141.9` -> `146.2`. Theme-card footer now accounts for its `borderTop: 1px dashed` layout height (copyright top = button bottom + 9, dashed rule drawn at the footer's top edge, card `165.3` -> `166.3`). Add-URL dialog: input `88` -> `125` (`4x23 + root p16.5x2`), buttons now derived as `1.75xfs + pyx2 + skinBorderx2` (border-box) instead of hard-coded `34/30`, title `py 10.8` / line `20.25`. Ledger P12~P16; V1/V2/V3 closed, V6 registered (Multi-line input *inner* line spacing still uses DWrite's default metrics — fixing it needs `SetLineSpacing` in the render layer and an on-screen check).

## v1.0.0 (2026-11-17)

### Upstream v3.5.0 1:1 Full Parity Milestone
- **Retired Non-Parity Subsystems**: Removed the `--guardian` watchdog process and invisible `1×1` sentinel windows, the `/unlock` feature & locale keys, and embedded `QuickJS` JavaScript profile script execution (`script` profile creation is explicitly rejected while preserving `create_profile`, `reorder_profile`, and `get_next_update_time`).
- **Node Clone-Topology Deduplication & 24h HTTPS Verification Quarantine**: Synchronized `util/node`, `enhance`, `cmd_proxy`, `monitor`, and `auto_select` to upstream `v3.5.0`, filtering confirmed-fake nodes in `NodeData`, preserving pre-test delay timestamps across `-2` pending states, evicting stale Timeout cache entries when fresher alive history exists (`BUG-387`), preempting auto-select on manual node switch (`AUTO_SELECT_MANUAL_PREEMPTED`), and gating `Offline` self-heal retests (`BUG-388`).
- **Unified Live + Closed Path Control & Custom Route Backup**: Merged active, synthetic, and recently closed connections into a unified 5-bucket view (`syn_disc` -> `syn_reject` -> `live` -> `syn_idle` -> `closed`), removed legacy Active/Closed tab switching, added YAML custom route **Save** / **Import** (`merge_profile` backup export/import), updated UIA virtual children, and cleaned hidden connection state after 30 seconds.
- **271px Left-Anchored Narrow Popup & UI Parity**: Standardized all narrow modals (`FirstRunGuideDialog`, `AddUrlDialog`, `EditProfileModal`, `ClientUpdateModal`, `CoreUpdateModal`, `AntivirusWhitelistModal`, `LogViewerModal`, `DeleteProfileConfirmModal`) to `271px` left-anchored layout with `popup_card_paint()` / `popup_tokens()`, added profile deletion confirmation (`BUG-390`), `Monochrome` `28×14` square switch with hover borders (`BUG-384`), Mixed-Port `Esc`/Close blur-save guard (`BUG-385`), `F1`/`F4` global shortcuts active during search focus (`BUG-389`), `320ms` minimum wait-notice dwell, atomic `WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE` geometry persistence, and `clash-win://` deep link scheme support.
- **13-Language Locales & Full Upstream Citation Closure**: Synchronized all 52 locale files across 13 languages and reached `0` unresolved `src:` citations (`ui_src_gate.ps1 -VerifySources`).

## v0.3.3 (2026-11-14)

### Guardian & Diagnostics Convergence
- **Minimal Guardian Isolation Architecture**: Converged cross-tree isolation to `CREATE_BREAKAWAY_FROM_JOB` without same-job fallback, preventing silent failure masking and enforcing fail-closed behavior across guarded launches.
- **Two-Phase Exit Proof & Boot/Epoch Respawn Budget**: Added prepared/confirmed `intentional_exit.json` verification, `respawn_state.json` fail-closed persistence under boot fingerprint and session epoch checks, and main-process guardian supervision with exponential rebuild backoff (1.5s–30s).
- **Safe File Replacement**: Fixed Windows handle retention in `evaluate_and_record_respawn_in` so `MoveFileExW` atomic updates succeed reliably on active state files.
- **Antivirus & Crash Observability**: Added 360/security-software process detection, whitelist heuristic signals, and Windows Error Reporting (WER) crash record collection to diagnostic bundles.

### Antivirus Whitelist User Guide
- **Integrated Whitelist Guide (`docs/antivirus-whitelist.md`)**: Added an entry in the Help menu, a dedicated first-run guide card, and a five-section transparent dialog explaining why antivirus cleaners may misclassify process-guardian respawns, how to whitelist `clashwin.exe`, and how recovery works, synced across all 13 locales.

### DNS, TUN Sniffer & Routing Hardening
- **3-State TUN Domain Sniffer (`Auto` / `On` / `Off`)**: Added `enable_tun_sniffer` setting and segmented control in Settings, routing config generation through the single `use_tun_with_sniffer` pipeline with 24 skip-dst exclusions.
- **Unified `sanitize_dns_upstreams` & `respect-rules` Safety**: Unified DNS upstream sanitization across `nameserver`, `fallback`, and `nameserver-policy` (stripping `#DIRECT`/`#PROXY` pseudo-fragments while preserving native `#h3=true`), rewriting unreachable foreign upstreams in-place rather than pruning keys, and removing fallback `IP-CIDR6,::/0,PROXY` leakage in GFWList mode.
- **Exact Quick-Routing & 4-State Path Control**: Switched quick-routing actions and URL rule creation to exact `DOMAIN` / `IP-CIDR` / `IP-CIDR6` / `PROCESS-NAME` rules, added managed rule tracking, in-table `Disconnected` (`ChainsDisconnected`) state retention, and in-place `RestoreDefault` across all 13 locales.
- **Monitor Freshness Tuning**: Adjusted `CoreMonitor::kHealthDataFreshSecs` back to 60s to prevent excessive background latency probes.

## v0.3.2 (2026-11-13)

与 Clash Mini v3.2.8（`BUG-364`）1:1 语义同步：

### 修复与加固

- **TUN 模式默认注入并补全 `sniffer` 域名嗅探**：
  - `enhance::use_tun(cfg, true)` 强制开启 `sniffer.enable: true` 并按缺补全 `force-dns-mapping: true`、`parse-pure-ip: true`、`override-destination: true`、`sniff`（`HTTP` `80, 8080-8880`；`TLS`/`QUIC` `443, 8443`）以及 `skip-domain`（`Mijia Cloud`、`+.push.apple.com`）与私网/回环/CGNAT/ULA/链路本地 `skip-dst-address` 免误伤列表；
  - 根治常驻后台浏览器（如 Edge）在模式切换或路由器 IPv6 RA DNS 下持有真实/GFW 投毒裸 IP 绕过 Fake-IP、因无 `Host` 落入 `MATCH,DIRECT` 导致 TUN 无法代理出境的问题。
- **新增 `dns.nameserver-policy` 卫生清理通道**：
  - `enhance_core` 流水线尾部新增 `sanitize_nameserver_policy`：未开启 `respect-rules`（或条目显式标注 `#DIRECT`）时剔除海外被墙公共 DNS 上游（如 `https://dns.cloudflare.com/dns-query`、`8.8.8.8` 等）并清除空键/空映射，防止 `SERVFAIL`；保留国内/内网/`system` 策略；
  - 开启 `respect-rules: true` 时复用 `BUG-308` 原则将 4 家境外公共 DNS 明文 UDP 改写为 DoH 并自动在 `GEOIP,CN` / `MATCH` 前补齐 `PROXY` 规则。
- **单测与 Golden 基线同步**：新增 6 项 TUN 嗅探与 `nameserver-policy` 卫生回归单测，并同步更新 `expected_clash_win.yaml` golden 夹具基线。

## v0.3.1 (2026-11-13)

### 轻量模式与后台托盘防第三方清理软件误杀四重加固（BUG-363）
- **伴生看门狗二进制异名异目录物理隔离（`clashwin-guard.exe`）**：针对 360 安全卫士加速球等清理工具在匹配主进程清理规则时会按映像名或安装目录将同名看门狗一并加入同批次强杀列表（导致看门狗与主进程在 `0ms` 内被同时 `TerminateProcess(99)` 连坐秒杀）的问题，启动看门狗前将程序自动同步至独立的 `%LOCALAPPDATA%\io.github.clash-win.clash-win\guard\clashwin-guard.exe`（通过 `.staging` 原子替换并在主槽位锁定时自动回退至 `clashwin-guard-alt.exe`），并通过 `--target-exe` 显式指向主程序路径——使伴生看门狗在进程名与磁盘目录两个维度与主进程彻底解耦。
- **主进程与伴生看门狗双向互锁监督与退避重建**：改变看门狗单次发射后不复检的模型，主进程新增后台监督线程（`WaitForMultipleObjects` 同时等待伴生进程句柄与主动退出事件，日常 `0% CPU`）；正常退出时毫秒级唤醒收尾，若看门狗在运行期被外部误杀则按 `1.5s -> 2s -> 5s -> 15s -> 30s` 阶梯退避自动重新同步并拉起伴生进程，形成「看门狗守主进程 + 主进程守看门狗」的双向自愈闭环。
- **主屏工作区 `1×1` 极微透明（`alpha=1`）鼠标穿透哨兵窗与消息泵保活**：升级防清理哨兵窗口对 `create_sentinel_window_pair`——将 `1×1` `WS_POPUP | WS_VISIBLE` 哨兵窗置于主显示器工作区左上角（`SPI_GETWORKAREA`，确保 `MonitorFromWindow != NULL`）、赋予非空窗口标题、将分层透明度由 `alpha=0` 提升至肉眼完全不可见的 `alpha=1`（`1/255`，规避针对屏幕外坐标与全透明 `alpha=0` 假窗口的启发式剔除），同时在窗口过程中对 `WM_NCHITTEST` 恒定返回 `HTTRANSPARENT` 确保绝对不拦截任何鼠标点击；伴生看门狗同样挂载独立哨兵窗并改用 `MsgWaitForMultipleObjects(..., QS_ALLINPUT)` 泵送窗口消息，确保 `IsHungAppWindow` 恒为 `FALSE`。
- **稳态运行计数复位延长与安装/卸载/双端互斥全链路清理**：将看门狗判定主进程稳定运行以重置自愈代数（`generation`）的阈值从 `60s` 上调至 `120s`；主窗口无论是否开启自动轻量模式，在隐藏至托盘时均同步置位轻量模式守护信令、唤醒显示时复位；并在 NSIS 安装、升级、卸载脚本及双端互斥接管流程中补齐隔离看门狗进程与缓存目录的自动清理。

## v0.3.0 (2026-11-13)

### 订阅拉取兼容标识解耦与单订阅自定义 User-Agent（BUG-362）
- **默认订阅 `User-Agent` 恒定锚定 `clash-verge/v2.5.10`，与客户端独立版本号彻底解耦**：将远程订阅默认请求标识独立为单一事实源常量 `kSubscriptionCompatUserAgent = "clash-verge/v2.5.10"`（对齐当前内置 `win-mihomo.exe` 即 `Mihomo v1.19.33` 的协议支持代际），不再随客户端 `0.x` 版本号拼接变化——彻底解决部分机场面板将 `0.x` 误判为远古客户端而拦截下发「请升级客户端」假节点的问题，同时消除未来版本号高频递增触发字典序比较缺陷、超前下发未知协议或暴露唯一版本指纹的隐患。
- **单条订阅自定义 `User-Agent` 完整闭环**：支持在单条订阅配置项中独立设置自定义 `user_agent`（空白字符自动裁剪并回退全局兼容标识），修复订阅导入与合并更新时自定义 `user_agent` 持久化丢失的问题。

### 同步跟进 Clash Mini v3.2.6（BUG-317 ~ BUG-361）：探针语义重构、服务内核加固与原生交互升级
- **测速数据来源语义重建、并行竞速探针与断流自愈闭环（BUG-317 / BUG-318 / BUG-320 ~ BUG-322 / BUG-336 / BUG-344 / BUG-345 / BUG-348 / BUG-349 / BUG-359）**：彻底废除 120 秒陈旧历史延迟兜底，区分实时探测（`LiveProbe`）与新鲜缓存（`FreshMetricsCache`，前端缓存续期上限钳制 30 秒），测速未执行或异常中断时精确回滚 `-2` 占位符杜绝永久转圈；物理网络连通性升级为三路公网 DNS `TCP:53` 600ms 非阻塞并行竞速 + 域名解析保底；解耦内核 API 失活重启计数与全节点断流告警计数，超时 `0ms` 节点在延迟升序下恒定置底；顶栏快捷区新增离线/全节点超时警示胶囊（支持脉冲提示与点击一键重测自愈），并拦截后台健康状态广播避免底部通知条重复刷屏。
- **内核热更原子替换、TUN 临时降级锁盘与零 UAC 极速退出（BUG-319 / BUG-324 ~ BUG-326 / BUG-328 ~ BUG-331 / BUG-334 / BUG-335 / BUG-346 / BUG-347 / BUG-351 / BUG-352）**：内核在线升级改为 `.staging` 暂存校验后原子替换，完整解析 `PE32+`（AMD64 COFF 与节表结构）以严格区分「下载包损坏」与「杀软瞬态文件锁占用」，切换前等待旧内核释放句柄并在失败时自动回滚快照；新增非阻塞注册表 SCM 三态核验（未安装 / 已禁用或缺失 / 正常启用）与服务操作全局重入互斥锁，冷启动服务繁忙触发会话级临时回退时锁住磁盘 `enable_tun_mode` 真值不被覆盖；正常退出改走无锁服务 IPC 直停内核并兜底清理残留进程，彻底消除普通用户退出时误弹 UAC 提权框的问题；加固命名管道 HTTP 只读幂等重连与 WebSocket 15 秒封顶指数退避重连。
- **连接期 SSRF 纵深防护、通用协议 URI 与深链解析加固（BUG-323 / BUG-327 / BUG-333 / BUG-340 / BUG-353）**：补齐 IPv4 映射 IPv6 地址（`::ffff:a.b.c.d`）及局域网后缀拦截，在 WinHTTP 建立连接阶段直接校验实际目标 IP（`WINHTTP_OPTION_CONNECTION_INFO`）并对 `30x` 重定向逐跳复检，根除 DNS Rebinding 时序绕过漏洞；统一 9 类代理节点 URI 预处理管线（兼容非标 VMess `sni` 字段与复杂密码后缀），修复 `clash://install-config` 深链携带嵌套未编码参数时的截断问题。
- **原生 Direct2D 交互体验、快捷键守卫与 13 国语言补齐（BUG-332 / BUG-337 ~ BUG-339 / BUG-341 ~ BUG-343 / BUG-350 / BUG-354 ~ BUG-358 / BUG-360 / BUG-361）**：手动点击节点支持 `0ms` 乐观高亮切换（失败按世代守卫自动回滚）并同步重置守护宽限期；节点卡片右侧延迟区支持独立点击单点测速且不切换当前出口；列表可见序列未变时原地更新卡片、高频流量刷新局部脏区隔离、皮肤双因子滑块拖动期零同步写盘（200ms 防抖 / 松手单次落盘）；统一 `Ctrl+F` / `F4` / `F1` 全局快捷键的输入态、IME 组合态与模态遮挡守卫，过滤框 `Esc` 优先取消自动测速倒计时并保留过滤词；矮屏幕高度（`<= 830px`）下在设置顶栏常驻右上角帮助菜单按钮防止展开裁切，优化日志查看器滚动锚点与系统托盘左键单击唤醒/隐藏切换，并同步补齐全部 13 种内置语言的新增提示词条。

## v0.2.10 (2026-11-12)

### 原生无障碍声音读屏（Win32 UI Automation）全量补齐
- **纯自绘 Direct2D 控件树原生桥接 Windows UI Automation (`WM_GETOBJECT`)**：在零 WebView2 / 零浏览器内核开销的前提下，完整实现 `IRawElementProviderSimple` / `IRawElementProviderFragment` / `IRawElementProviderFragmentRoot` COM 无障碍提供程序树，支持 **Windows 讲述人（Narrator）**、**NVDA**、**JAWS** 及 Accessibility Insights 直接发现、逐项聚焦、框选定位并语音朗读全界面自绘控件。
- **六大 UIA 标准控制模式与复合控件虚拟子项全覆盖**：全面实现 `IInvokeProvider`（按钮/菜单项点击）、`IToggleProvider`（开关/置顶/皮肤格状态读写）、`IValueProvider`（过滤框/订阅框/端口框与弹窗输入框读写）、`IRangeValueProvider`（各皮肤双因子滑块精确调节）、`ISelectionItemProvider`（代理节点切换、流量接管/分流分段单选、连接标签页）及 `IExpandCollapseProvider`（13 国语言选择器与帮助菜单展开状态），并为自绘复合区域（快捷栏活跃节点卡与延迟胶囊、订阅条目动作钮、连接面板表头、首启两步式向导及全部模态弹窗）补齐虚拟无障碍子项。
- **13 种内置语言实时读屏热切换与智能降噪**：接入惰性 `i18n` 版本校验（`ensure_i18n_fresh`），切换 13 种界面语言（`简体中文`、`繁體中文`、`English`、`日本語`、`한국어`、`Русский`、`Deutsch`、`Español`、`Bahasa Indonesia`、`Türkçe`、`فارسی`、`العربية`、`Татарча`）后无需等待重绘即可即时向读屏器输出对应语种 `aria-label`（`layout.a11y.*` 等）文案；自动聚合节点行（名称 + 协议 + 延迟 + 当前节点标注 + `PositionInSet/SizeOfSet`）与底部四宫格流量摘要，屏蔽碎片化装饰图元干扰。
- **实时通知语音播报、模态焦点隔离与全键盘无障碍导航**：动态绑定 Windows `UiaRaiseNotificationEvent` 实现 `SnackbarW` 顶部通知条免抢焦点实时语音播报；在打开清洁安装首启引导、编辑订阅、调试日志等模态弹窗或展开设置抽屉时自动屏蔽底层被遮挡控件，提供 `Tab` / `Shift+Tab` 模态环绕焦点遍历与 `Space` / `Enter` 激活，并在控件动态销毁时自动断开 COM 引用杜绝悬空访问。

## v0.2.9 (2026-11-12)

### 同步跟进 Clash Mini v3.2.5（BUG-316）：双层防误杀与零断流自愈守护
- **第一层事前免疫——常驻零渲染防清理哨兵窗口对**：针对 Windows 下隐藏至系统托盘后因无可见顶层窗口被 360 加速球等第三方内存清理工具以 `TerminateProcess(hProcess, 99)` 无差别强杀导致托盘图标消失、后台防断流守护停摆的问题，在主线程创建全生命周期常驻的 1×1 屏幕外全透明（`alpha=0`）哨兵窗口对——既满足系统可见性契约彻底豁免无窗口清理，又完全不在任务栏与 `Alt+Tab` 露面，渲染与 CPU 开销为零。
- **第二层事后兜底——超轻量级伴生看门狗（`--guardian`）与退出码取证**：启动时拉起纯内核事件阻塞态的伴生看门狗进程（`0% CPU`、约 `1 MB` 工作集）；主进程正常退出、系统关机注销或对端互斥接管时毫秒级同步退出，而在后台隐身/轻量模式下遭遇外部强杀或致命异常时，同步向 `clashwin.log` 写入含退出码特征诊断（如 `99/0x63` 360 强杀特征码、`0xC0000005` 等）、存活时长与自愈代数的取证记录，并在通过关机、卸载/安装器、对端客户端及单实例端口四重安全校验后静默自愈拉起主进程（含快速崩溃退避上限保护）。
- **系统服务内核零断流无感挂载（`--guardian-respawn`）**：伴生看门狗静默拉起主进程时，直接以无闪烁零渲染模式进入托盘轻量态；若 `clash_verge_service` 持有的 `win-mihomo` 内核仍在健康运行，直接无感挂载现有内核并延续当前出口节点健康守护，全程不重建 TUN 网卡、不打断正在进行的下载或通话。
- **双端互斥礼貌协作与监测日志闭环**：互斥接管终止对端客户端前先置位其主动退出事件避免误报崩溃日志，看门狗检测到对端客户端运行时自动让位；同时在 `CoreMonitor` 补齐偶发节点抖动复检恢复后的闭环日志记录。

## v0.2.8 (2026-11-12)

### 通知反馈彻底修复与全场景补齐
- **根治通知栏同线程自死锁导致操作无弹窗的问题**：修复 `NoticeService` 在主线程（UI 线程）推送通知时持非递归互斥锁同步回调 `SnackbarW::refresh()` 重入取快照引发 `resource_deadlock_would_occur` 异常并被静默吞没的致命缺陷；改为锁外派发通知回调并支持多订阅者独立句柄，确保主线程与后台线程触发的所有操作提示都能 100% 即时弹出。
- **全操作链通知覆盖与 13 语种对齐**：补齐订阅导入/切换/删除、端口保存、亮暗主题/皮肤/语言切换、单节点右键测速、`F1` 全局自动优选、排序模式切换、单条连接断开与清空历史记录等全场景的「进行中 + 结果」通知，统一补全全部 13 套语言包文案，并将原先 1~1.5 秒一闪而过的提示统一延长至清晰的 2~2.5 秒。

### 同步跟进 Clash Mini v3.2.4（BUG-315）
- **物理网络连通性双频快速探测重构（打破 TUN 断流死锁）**：将后台守护的物理网络探测升级为「在线 60s / 离线 5s」双频节流，优先对公网高可用 IP（`223.5.5.5:53`、`119.29.29.29:53`、`1.1.1.1:53`）发起毫秒级 TCP 握手，在局域网封禁 53 端口时自动回退测速地址 DNS 解析兜底——彻底消除虚拟网卡常亮导致的假在线误判，以及 TUN 模式下节点断流引发 DNS 卡死导致的自愈死锁。
- **断流自愈感知与二次切点全面提速**：1:1 同步缩短三大计时阈值——节点健康数据新鲜度阈值从 `60s` 缩短至 `20s`，节点切换宽限期从 `60s` 缩短至 `20s`，自愈失败冷却保护从 `60s` 缩短至 `25s`，突发断流自愈响应收敛至 15~25 秒。
- **隐身轻量模式主循环防意外退出加固**：在 `WM_DESTROY` 与主消息循环补充显式退出状态守卫（`Lifecycle::is_exiting()`），防止非预期窗口销毁或外部消息误终止常驻托盘与后台防断流守护线程。

### 其它稳定性与交互细节修复
- **Direct2D 设备丢失自愈修复**：修复 `D2dRenderTarget::recreate()` 在销毁旧渲染目标时误清空 `hwnd_` 导致远程桌面切换或显卡驱动重置后窗口无法恢复绘制的问题。
- **代理分流卡片与全新安装日志目录修复**：启动与刷新设置时自动同步代理分流分段控件状态；已处于自定义路由档位时再次点击「添加网址」支持随时重开添加弹窗；修复全新安装首次启动时 `%APPDATA%` 父目录尚未创建导致日志文件初始化失败的问题。

## v0.2.7 (2026-11-11)

### 用户可见修复与交互完善
- **像素级布局与浮层防裁剪精修**：修正基础设置 4 个小开关标签与开关实体的水平底边对齐；悬浮提示框（Tooltip）改为独立顶层透明分层窗口承载，彻底消除靠近主窗口边缘时被截断一半的问题；补齐各图标按钮缺失的悬停提示与鼠标移出清理；右键菜单支持视口边界钳制与滚轮滚动；窄窗（271px）下各模态对话框自适应贴合不溢出。
- **操作与结果通知全面补齐**：修复服务安装/重装通知重复前缀、订阅导入/更新与配置切换期间误弹 `IPC timeout` 错误条、标题栏阴影穿透污染右上角通知条像素等问题；并为代理分流切换（全局代理 / GFWList）、基础设置 4 项开关（开机自启 / 静默启动 / Allow LAN / IPv6）、手动/轮换切换节点、`⚡` 批量测速择优、窗口置顶及断开全部连接补齐清晰明确的状态与结果提示。
- **节点过滤条件与窗口几何跨重启记忆**：启动时自动读回上次保存的过滤词与排序方式并回填界面（程序化回填不误触发自动测速，且保留用户输入原始大小写）；安装/升级脚本保留 `window_state.json`，并修正高 DPI 副屏/缩放屏脱屏判定口径。
- **设置开关即时响应与真实初始态**：设置命令全面改为后台异步执行，点击即刻位移不卡 UI 线程；Allow LAN / IPv6 启动时读取真实配置状态。

### 工程与发版体系重构
- **发版全链路重构**：统一 `constants.h` / `clashwin.rc` / `clashwin.nsi` 版本同步与跨文件单测门禁，统一本地与 CI 的隔离回归测试运行器 `scripts/run_tests.ps1`，消除发布前清空 `updater/app-update.json` 导致的分支分叉与窗口期断档，修复 `pre-push` 钩子在 Windows PowerShell 5.1 下的兼容性并收敛常青文档 `docs/KNOWN_LIMITATIONS.md`。

## v0.2.6 (2026-11-11)

### 用户可见修复
- **UAC 提权前先自报家门**：安装 TUN 系统服务触发 UAC 前，先弹出来源明确的「Clash Win 需要管理员权限…」确认框，解决此前弹窗只显示「未知发布者」、用户无法判断请求来源的问题（与 Clash Mini 对齐）。
- **内核缺失自动恢复**：启动时若检测不到 `win-mihomo.exe`，自动从 GitHub（MetaCubeX/mihomo）下载并校验内核到 `cores/` 后重试，等价于 Clash Mini「内核始终存在」的保证；不再因内核缺失导致节点表空白。
- **sidecar 内核路径修正**：`dirs.cpp` 的 `sidecar_core_path()` 原先错误指向 exe 根目录，已修正为 `<exe>\sidecar\win-mihomo.exe`。

### 工程 / 发版流程
- **根治发布漏内核**：新增三道锁——`build_installer.ps1` 硬性要求内核存在、`release.yml` 在归档缺失时从上游补齐并按基线校验哈希、`release.yml` 断言 setup 体积 > 35MB；预检脚本新增 GATE 6 校验本地 sidecar 含内核。v0.2.5 的 `setup.exe` 因外部归档漏内核且流程无拦截，曾打出不含内核的安装包。
- **构建环境治理**：新增 `scripts/build.ps1` 标准编译入口，构建前剥离 WorkBuddy 透明代理注入的大小写重复 `*_PROXY` 变量，根治 MSBuild「字典关键字冲突」编译崩溃。

## v0.2.5 (2026-11-11)

### UI detail parity (1:1 with Clash Mini)

- **Task Manager now shows "Clash Win"**: the main executable gained a `VERSIONINFO` resource (FileDescription / ProductName = "Clash Win"), 1:1 with Clash Mini's own "Clash Mini" branding, instead of falling back to the raw `clashwin.exe` image name.
- **Basic-settings 2×2 switch grid spacing aligned**: equal `1fr 1fr` columns, 24px column gap, 13px labels — matches Clash Mini's `basic-settings-card.tsx`. Previously the grid used a 6px gap with unequal columns, jamming the two columns together and pushing the right-column labels (Allow LAN / Allow IPv6) far from their toggles.
- **Narrow-window breakpoint consolidated**: the 285px "stop rendering the connections panel" rule now references the single `win::kMiniWidthThreshold` constant instead of three duplicated literals. No behavior change.

### Runtime correctness

- **Single-instance hardened**: a second launch no longer starts a second copy under load. The lock-port bind failure is now treated exactly like Clash Mini's `check_singleton` — wake the existing window and exit — instead of a "maybe an instance is alive" probe that could time out and wrongly continue.
- **Lite mode now truly idles**: entering lite mode stops the 1-second stealth timer and the 5-second TUN-guard probe thread (the latter previously spun up a worker thread every 5s); silent boot defers creating the render layer until after entering lite mode, matching Clash Mini's "don't build a window you're about to hide" optimization. This closes the gap where Clash Win looked heavier than Clash Mini under Task Manager in lite mode.

---

## v0.2.4 (2026-11-10)

### Coexistence fix — TUN broke when installed over a running Clash Mini

- **Kill rule now actually kills the other side's elevated core**: previously the coexister opened `PROCESS_TERMINATE` with no counterpart and silently skipped any process it could not open. When the other client's core ran elevated (e.g. Clash Mini in TUN mode) the `TerminateProcess` was denied and the other side survived — leaving two cores running and the TUN hijacked. The kill path now identifies the other side by process name up front and (on the always-elevated TUN entry path) kills the other core before (re)starting our own.
- **Orphan Wintun/Mihomo adapter cleanup added**: after killing the other side, any leftover `Wintun`/`Mihomo` virtual adapter (and its routes) is now removed. This was the root cause of "browsers dead after switching to TUN" on a machine where Clash Mini was already in TUN mode — the orphaned adapter collided with / was reused as a stale adapter when Clash Win opened TUN.

### Internal

- Version literal in the release-readiness gate (`test_t35_release_readiness.cpp`) bumped to 0.2.4.

---

## v0.2.3 (2026-11-10)

### Kernel & coexistence

- **Kernel upgraded to mihomo v1.19.33**: the bundled core is now the **official MetaCubeX/mihomo v1.19.33 gvisor release build** (sourced directly from the upstream GitHub Release, not copied from Mini), renamed `mini-mihomo.exe` → `win-mihomo.exe`. The rename is a **user-ratified special case** documented in the constitution (`移植基本准则.md`) as part of the exclusive-coexistence naming chain (verge-mihomo → mini-mihomo → win-mihomo).
- **Exclusive coexistence policy landed (§5.2.2 "borrow & return")**: launching or installing one client terminates the other's client/core processes (strong-kill `TerminateProcess`, ownership judged by exe directory path, so a same-name core inside the other's own directory is no longer mis-killed by the installer) and starts the shared `clash_verge_service` from its own install path (service-registry takeover). **Nothing on the other side's disk is ever deleted.**

### Parity fixes (1:1 with Clash Mini v3.2.3)

- **Filter auto-select debounce 800 → 700 ms (G1)**: matches Mini's current debounce interval.
- **Focus-lost trigger removed (G2)**: the Win-added "trigger auto-select when the search box loses focus" behavior is removed — Mini only triggers on debounce completion or Enter.
- **IME composing guard (G3)**: pressing Enter while an IME composition is active (confirming pinyin candidates) no longer triggers auto-select, matching Mini's `!isComposing && keyCode !== 229` guard.
- **Same-word dedup (G4)**: re-submitting the identical filter word no longer re-triggers auto-select.
- **Log dialog layout (G6–G8)**: 44 px header, title 15 px / weight 600, level dropdown widened to 95 px, divider moved to match Mini's current metrics.
- **Log dialog tool buttons as real icons (G9)**: pause / sort-order / clear are now 18 px MUI vector glyphs (`pause_circle`-equivalent, `swap_vert` with vertical flip for descending) instead of text glyphs.
- **Log wrapping (G10)**: log payload wraps (`anywhere`) instead of ellipsis-truncating; row height grows dynamically, no horizontal scrollbar in narrow windows.

### Internal

- Regression assertions updated for the kernel rename (3 test files still expected `mini-mihomo.exe`; code itself was correct).
- Ledger corrections: the "auto-update install/restart not wired" record was stale (the chain was already fully wired: download → verify → install → exit to installer); `geodata-loader` resolved — **neither side sets it, both inherit mihomo's `memconservative` default, 1:1 with no change needed**.

## v0.2.2 (2026-11-09)

### Audit-gap parity fixes (1:1 with Clash Mini v3.2.2)

- **Node list 3-column grid (❌1)**: a wide window (>285px content) now lays out nodes as a 3-column card wall, same as Mini; the narrow window stays single-column (centered).
- **Alive probe guard (❌2)**: dead nodes (`alive===false`) render as Timeout(0) instead of a stale green delay — closes the 2026-10-24 incident recurrence path.
- **Self-heal refresh on no-candidate (❌3)**: auto-select now broadcasts `refresh_proxies` even when no candidate survives, so the UI never sticks on stale delay/alive.
- **On-demand probe gating (❌4)**: `probe_metrics_with_fallback` skips redundant probes when the active node is healthy & fresh (<60s) or freshly confirmed dead (<10s), reproducing Mini's fix for probe self-excitation false-deaths.
- **Self-heal alert = 3-fail reset-to-zero (△8)**: removed the standalone 5-fail popup; at 3 consecutive failures the counter resets and only an API-unreachable → core-restart → restart-fail ladder raises the alert (matches Mini `monitor.rs`).
- **Settings switch rollback on patch failure (❌5)**: a failed `patch_verge`/`patch_clash` snaps the toggle back to the authoritative value (Mini `mutateInfo` behavior).
- **Autostart `StartupApproved` write-back (❌6)**: enabling autostart also writes `StartupApproved\Run`, so the config wins over Task Manager's disable.
- **Autostart reads real registry state (❌7)**: the "launch on boot" switch reflects the actual Run-key state, not the verge config value.
- **Paint error boundary (❌8)**: `Widget::paint` wraps `on_paint` in try/catch and draws a placeholder rect instead of crashing the whole tree on a single widget exception.
- **First-run "later" opens drawer (❌9)**: dismissing the first-run guide with "later" auto-opens the settings drawer to point at the subscription card.
- **Auto-update manifest closure (❌10)**: `release.yml` now generates `updater/app-update.json` with a real `platforms.windows-x86_64` (url + sha256 + signature) on publish.

### Internal

- Added missing `#include <shellapi.h>` in `src/system/coexist.cpp` (build fix).
- Version bumped to 0.2.2 (independent of Clash Mini 3.2.2).

## v0.2.1 (2026-11-08)

### Exclusive coexistence + installer polish

- **Exclusive coexistence policy**: Clash Win and Clash Mini now **exclude each other** instead of sharing one machine silently. Launching one client terminates the other's client/core processes and **takes over the shared `clash_verge_service`**; mutual exclusion is also enforced **at install time**. Consequently all network ports / controller secret / identifiers return to a **1:1 alignment with Clash Mini's official scheme** (`10801` mixed / `10802` SOCKS / `10803` HTTP / `7899` guard fallback / `9098` controller / `33335` single-instance), replacing the staggered-port coexistence layout of v0.1.0.
- **Installer**: the installer window stays **always on top**; the IME candidate window follows the caret; the node filter **auto-selects** on completion.
- **Tests**: new release-readiness regression suite (`t35`) — title-bar color formula 1:1, tooltip 100 ms, `::selection` colors, update-manifest ownership (no `Clash.Mini` / no `3.2.2`, version == `kAppVersion`) and a registration-set assertion (every `UT_TEST` file must be registered in `CMakeLists.txt`).

### Notes

- v0.2.2 supersedes v0.2.1 as the current version line (`include/clashwin/config/constants.h:20 kAppVersion`).

---

## v0.1.0 (2026-11-07)

### First release — Windows-native 1:1 port

- **Core**: a ground-up C++20 / Win32 / Direct2D implementation of the Clash Mini v3.2.2 client. No Tauri, no WebView2, no JavaScript runtime — a single native executable plus the upstream Mihomo sidecar.
- **UI**: frameless Win32 window with `WM_NCHITTEST` hit-testing, `WM_SIZING` geometry clamping and Per-Monitor v2 DPI; Direct2D/DirectWrite rendering of the six skins (`retro-3d`, `original`, `modern-flat`, `frosted-glass`, `cyberpunk`, `monochrome`), with per-skin factor sliders wired to visible effects.
- **Workflows**: subscription import (19 protocols), automatic node benchmarking and fastest-node switching, rule/GeoIP updates, mini traffic-monitor layout, right-click routing rules, streaming-unlock detection, first-run onboarding, autostart, tray and quit recovery.
- **Coexistence**: a dedicated port set (`10811` mixed / `10812` SOCKS / `10813` HTTP / `10814` guard / `9108` controller / `33337` single-instance), its own secret (`adapted-by-clash-win`), config directory `io.github.clash-win.clash-win`, and a shared `clash_verge_service` Windows service — designed to run alongside Clash Mini on one machine. Do not enable TUN mode or system proxy on both clients at once.
- **Localization**: 13 UI languages under `locales/`; the NSIS installer starts in Simplified Chinese, English or Russian.
- **Distribution**: two artifacts per release — `ClashWin_0.1.0_x64-setup.exe` (NSIS, per-machine, requires UAC) and `ClashWin_0.1.0_x64_portable.zip` (extract-and-run, `.config/PORTABLE`), with `SHA256SUMS.txt` for offline verification.
- **Verification**: full regression passed (`EXIT=0`, `0 failed`) — see the delivery records in `docs/06-验收状态台账.md` and `docs/evidence/`. The case count is intentionally not pinned here: the suite grows with each regression addition.

See the [v0.1.0 GitHub Release](https://github.com/qiu-yuxiao/clash-win/releases/tag/v0.1.0) for the historical v0.1.0 release notes and [Known limitations](docs/KNOWN_LIMITATIONS.md) for the current evergreen disclosure list.

### Notes

- The only public difference from Clash Mini is the display name `Clash Mini` → `Clash Win`.
- **The v0.1.0 release assets are unsigned** — no Authenticode signature is applied. The signing step (`scripts/sign.ps1` / `installer/build_installer.ps1`) needs an elevated session, unavailable in the non-interactive release environment, so the signature status is recorded but does not block the release; Windows shows "Unknown publisher". This matches Clash Mini v3.2.2 installers, which likewise carry no Authenticode signature. Application in-app auto-update is an **empty channel** in v0.1.0. The five sidecar executables are not committed. These are documented, not hidden — see Known Limitations (a)–(f).
