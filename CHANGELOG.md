# Changelog

All notable changes to **Clash Win** are documented here.

Version format follows the project's **own** line starting at `0.1.0` (constitution §5.3): Clash Win versions increment independently and are **not** bound to Clash Mini `3.2.2`, which is only the source-reference anchor for the port.

Format reference: `Changelog.md` in the Clash Mini repository (newest first, `## vX.Y.Z` headings with grouped change bullets). Content here is Clash Win's own history, not a copy of Mini's entries.

---

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

See [Release notes](docs/RELEASE_NOTES_0.1.0.md) for the exact release scope and [Known limitations](docs/KNOWN_LIMITATIONS_0.1.0.md) for v0.1.0's explicitly registered gaps.

### Notes

- The only public difference from Clash Mini is the display name `Clash Mini` → `Clash Win`.
- **The v0.1.0 release assets are unsigned** — no Authenticode signature is applied. The signing step (`scripts/sign.ps1` / `installer/build_installer.ps1`) needs an elevated session, unavailable in the non-interactive release environment, so the signature status is recorded but does not block the release; Windows shows "Unknown publisher". This matches Clash Mini v3.2.2 installers, which likewise carry no Authenticode signature. Application in-app auto-update is an **empty channel** in v0.1.0. The five sidecar executables are not committed. These are documented, not hidden — see Known Limitations (a)–(f).
