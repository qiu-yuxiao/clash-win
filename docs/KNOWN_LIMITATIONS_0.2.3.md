# Clash Win v0.2.3 — Known Limitations

**Scope**: Clash Win v0.2.3 (2026-11-10). This file is an explicit, honest register of what is **not** implemented or **not** verified in this release. It exists so that "1:1 port" is not misread as "no known differences".

> Clash Win is a Windows-native C++20/Win32/Direct2D **1:1 port of Clash Mini**, with the display name `Clash Mini` → `Clash Win` as the only public difference. Its own version line starts at `0.1.0` and increments independently. The items below are **deviations from a complete parity claim** and are listed rather than hidden.

| # | Area | Status | Evidence |
| :--- | :--- | :--- | :--- |
| (a) | frosted-glass Blur | Not implemented in this release; **CPU box-blur route approved** (2026-11-10 user ruling) and scheduled for the next release | constitution §六.7, `docs/04-工程技巧.md` |
| (b) | Hover 0.15 s transition | Implemented for `Button`; other hover surfaces discrete | `src/ui/button.cpp` (`advance_hover`) |
| (c) | In-app auto-update | Wired; real download-install round-trip from a v0.2.2 client still to be exercised | `updater/app-update.json`, `src/update/client_updater.cpp` |
| (d) | Release asset signing | **Unsigned (no Authenticode)** | `scripts/delivery_check.ps1`, Release assets |
| (e) | Clean-machine / UAC visual checks | Not verified | `docs/06-验收状态台账.md` |
| (f) | sidecar binaries | Not committed (CI-restored); core now `win-mihomo.exe` = official upstream v1.19.33 build | `.gitignore`, `docs/05-sidecar-内核资产.md` |

---

## (a) Direct2D has no CSS `backdrop-filter` primitive — frosted-glass Blur still pending

**What is missing**: Mini's `frosted-glass` skin uses a CSS `backdrop-filter: blur(...)` driven by the skin's `Blur` factor. The skin's other factor (Opacity) and its color/geometry are reproduced; the Blur slider does not yet produce a real blur.

**Status change in v0.2.3**: the user has ratified the implementation route on 2026-11-10 — a **CPU-side box blur** ("截取面板背后画面 → 三次盒式模糊 → 贴回"), implemented inside the existing D2D 1.0 pipeline without migrating the render backend. This is scoped to the frosted-glass skin's five blurred surfaces only (title bar / drawer / content card / theme panel / crystal card, per Mini's `index.scss` / `layout.scss` blur matrix). Implementation is scheduled for the next release; **v0.2.3 still ships without the blur**, so with the `frosted-glass` skin the Blur slider has no visible effect. This remains an honest gap, not a mapped-to-something-else fake (constitution §六.7).

**Rejected routes (unchanged from v0.2.2)**: DWM system backdrop blurs the desktop behind the window, not the app's own background — wrong effect and a windowing-semantics change; the D2D 1.1 `GaussianBlur` effect requires an `ID2D1DeviceContext` migration, i.e. a render-backend rewrite.

---

## (b) Hover 0.15 s transition — implemented for the `Button` family, not "global"

**What is implemented**: `Button`（含设置抽屉底部皮肤格 `SkinCell`）在 Enter/Leave 时按 **150 ms** 插值悬停底色；曲线用 smoothstep 近似 CSS `ease`。动画只在悬停状态切换时启动，过渡结束即停帧，不引入持续重绘。

**What remains discrete（未动画）**: 代理/连接列表行、侧栏导航项、右键菜单项、开关与滑块及若干对话框内自绘按钮保持瞬变。Mini 侧这些元素的过渡时长/曲线并不统一，因此没有按"全局 0.15 s"统一改写。

**Deviation note（retro-3d 渐变）**: CSS 不对 `linear-gradient` 做插值；Clash Win 对六个色停做 150 ms 插值，视觉更平滑，但严格说比 Mini 多了一段渐变过渡。

---

## (c) In-app auto-update — wired; the real round-trip is still to be exercised

**State**: the chain `check → download (CNG SHA256 + minisign hard gate) → install (`/UPDATE /ARGS` ShellExecuteExW) → exit to installer` is fully wired in `src/update/client_updater.cpp` / `src/ui/main_window.cpp`; CI generates the signed manifest on publish. A v0.2.2 client reading the v0.2.3 manifest gets `has_update=true` for the first time, but **this release does not yet claim the download-install round-trip has been exercised end-to-end from a real client** — that verification belongs to the maintainer's post-release pass.

**Failure modes are notifications, not silence**: download failure, missing/invalid signature, key mismatch, and check failure each surface a visible message; minisign verification is a **hard gate** (no signature → refuse to install).

**Residual**: the updater signature proves "the setup bytes match the publisher's key", not SmartScreen trust — that depends on Authenticode, see (d).

---

## (d) v0.2.3 release assets are unsigned — no Authenticode signature

**What it is**: the published v0.2.3 artifacts carry **no Authenticode signature**, matching Clash Mini's current installers (`certificateThumbprint: null`). The signature status is recorded but does not block the release.

**Why**: Authenticode signing requires an elevated session; a local self-signed certificate would add no public trust, so shipping unsigned + checksum verification is the honest choice (unchanged rationale from v0.2.2).

**Consequences**: Windows shows "**Unknown publisher**" and SmartScreen may warn. Verify every download against `SHA256SUMS.txt` in the Release before running it — the checksum is the only integrity / trust anchor offered for v0.2.3.

---

## (e) Clean-machine acceptance and UAC #0 / #5 visual checks not performed

**What is missing**: `docs/06-验收状态台账.md` records `EVIDENCE=NOT-CLEAN-MACHINE` and `P0_DECLARATION=DEFERRED` — the **independent-availability P0** chain (a machine that has never had Clash Mini installed: service bootstrap from absent → UAC install → `sc query` RUNNING → kernel Session#=0 → three URLs reachable) was **not executed** on this machine, because it physically has Clash Mini installed.

The **manual visual items** of the UAC acceptance list — #0 (zombie PID cleanup / reboot) and #5 (visual confirmation of the UAC publisher line) — likewise require a human at a clean console and **were not performed**.

**Consequence**: v0.2.3 has **not been proven** to bootstrap itself on a machine without Clash Mini. The installer does contain the service-install path, but treat first-run on a genuinely clean machine as unverified.

---

## (f) The five sidecar executables are not committed

**What is missing**: `.gitignore` excludes `*.exe`, so the five binaries under `sidecar/` (`win-mihomo.exe` — the official MetaCubeX v1.19.33 gvisor build — the service binary, and helpers) are **not in the repository**. Byte sizes and SHA256 baselines are documented in [05-sidecar-内核资产.md](05-sidecar-内核资产.md); `scripts/verify_sidecar.ps1` checks 5/5 and must pass before any build, test or release.

**Consequence**: a **fresh clone compiles but cannot run**. The published Release assets bundle the sidecar; the release CI restores it from a maintainer-controlled archive (`vars.SIDECAR_ARCHIVE_URL`) and re-runs the 5/5 baseline check — **if that repository variable is not configured, the build job fail-fast aborts the release** rather than producing an unrunnable package.

---

## Process note — 用户已确认 gate

The v0.2.3 parity fixes (G1–G10), the coexistence policy landing, and the kernel upgrade are code-complete and marked **已处理 (code landed)**; the final **用户已确认** (maintainer rebuilds and desktop-reviews each item) is pending. v0.2.3 is released on the strength of the code-level alignment and the green regression run (`678 cases, 0 failed, EXIT=0`); the desktop-confirmation pass is the maintainer's next step, not a blocker for shipping.

---

## Not claimed

This release does **not** claim:

- "perfect 1:1 parity" or "no known differences" (frosted-glass blur (a) and the discrete hover surfaces (b) remain);
- a publicly trusted code-signing chain (the v0.2.3 assets are unsigned);
- an in-app auto-update download-install round-trip **already exercised end-to-end from a real client** (the v0.2.2 → v0.2.3 path is now offered by the manifest but unexercised — see (c));
- verified clean-machine bootstrap;
- that a source-only checkout is runnable without the sidecar binaries.

For the full release scope see [RELEASE_NOTES_0.2.3.md](RELEASE_NOTES_0.2.3.md), the [Changelog](../CHANGELOG.md), and the [acceptance ledger](06-验收状态台账.md). Report other problems via the [issue templates](https://github.com/qiu-yuxiao/clash-win/issues/new/choose); disclose security issues privately per [SECURITY.md](../SECURITY.md).
