# Clash Win v0.2.2 — Known Limitations

**Scope**: Clash Win v0.2.2 (2026-11-09). This file is an explicit, honest register of what is **not** implemented or **not** verified in this release. It exists so that "1:1 port" is not misread as "no known differences".

> Clash Win is a Windows-native C++20/Win32/Direct2D **1:1 port of Clash Mini v3.2.2**, with the display name `Clash Mini` → `Clash Win` as the only public difference. Its own version line starts at `0.1.0` and increments independently; `3.2.2` is only the source-reference anchor. The items below are **deviations from a complete parity claim** and are listed rather than hidden.

| # | Area | Status | Evidence |
| :--- | :--- | :--- | :--- |
| (a) | frosted-glass Blur | Not implemented | `docs/04-工程技巧.md`; constitution §六.7 |
| (b) | Hover 0.15 s transition | Implemented for `Button`; other hover surfaces discrete | `src/ui/button.cpp` (`advance_hover`), §(b) below |
| (c) | In-app auto-update | Wired, signature pending | `updater/app-update.json`, `src/config/update_keys.h`, `.github/workflows/release.yml` |
| (d) | Release asset signing | **Unsigned (no Authenticode)** | `scripts/delivery_check.ps1`, Release assets |
| (e) | Clean-machine / UAC visual checks | Not verified | `docs/06-验收状态台账.md` |
| (f) | sidecar binaries | Not committed (CI-restored) | `.gitignore`, `docs/05-sidecar-内核资产.md` |

---

## (a) Direct2D has no CSS `backdrop-filter` primitive — frosted-glass Blur not implemented

**What is missing**: Mini's `frosted-glass` skin uses a CSS `backdrop-filter: blur(...)` driven by the skin's `Blur` factor. The skin's other factor (Opacity) and its color/geometry are reproduced; the Blur slider does not produce a real blur.

**Why both candidate routes were rejected in v0.2.2（判断依据）**:
1. **DWM system backdrop / acrylic**（`DwmSetWindowAttribute` + `DWMWA_SYSTEMBACKDROP_TYPE`）blurs **the desktop behind the window**. Mini's window is **opaque** (`.transparent(false)`, `src-tauri/src/utils/resolve/window.rs:122`) and its `backdrop-filter` blurs **the app's own already-painted background** (`.theme-panel` `backdrop-filter: blur(16px …)`, `src/assets/styles/index.scss:78`; crystal cards `:89`). Adopting the system backdrop would require making the whole window transparent + material-backed, adding a Windows 11 22H2+ dependency with a Win10 fallback. That is a different effect and a whole-window drawing-semantics change, not a 1:1 alignment.
2. **Direct2D `GaussianBlur` effect**（`ID2D1Effect`）only exists on the **D2D 1.1 `ID2D1DeviceContext`**. This app renders through the **D2D 1.0 `ID2D1HwndRenderTarget`** (`src/ui/d2d_context.cpp:135`/`:237`) created from a plain `ID2D1Factory` (`src/ui/d2d_context.h:49`) with no D3D11 device/swap chain. Enabling the effect chain means migrating the whole render backend (window + offscreen test targets), i.e. a rendering-pipeline rewrite.
3. **CPU blur fallback** would need to read the already-drawn window area back into a bitmap and blur it per frame. The current `IRenderTarget` has no window-target readback, and a 16 px Gaussian over large panels every frame is an unacceptable CPU cost.

**Consequence**: with the `frosted-glass` skin, the Blur/val2 slider has no visible blur effect. This is a rendering-pipeline gap, **not** a mapped-to-something-else fake: the project does not pretend the slider works by substituting an unrelated visible property (constitution §六.7).

---

## (b) Hover 0.15 s transition — implemented for the `Button` family, not "global"

**What is implemented**: `Button`（对应 MUI ButtonBase 的原生控件，含设置抽屉底部皮肤格 `SkinCell`）在 Enter/Leave 时按 **150 ms** 插值悬停底色：主题渐变按六个色停逐停插值、单色皮肤在两个颜色之间插值；曲线用 smoothstep 近似 CSS `ease`（← `src/utils/button-styles.ts:188/258/313/399/464`）。动画只在悬停状态切换时启动，由 `on_paint` 推进；`t >= 1` 时动画起点归零、不再 `invalidate()`，**过渡结束即停帧，不引入持续重绘**（实现见 `src/ui/button.cpp` 的 `set_hover_target` / `advance_hover`）。

**What remains discrete（未动画）**: 其余自绘 hover 面保持瞬变——代理/连接列表行、侧栏导航项、右键菜单项、开关与滑块，以及若干在对话框内用局部 `hover` 布尔自绘的按钮（`quick_bar.cpp`、`utility_modals.cpp`、`add_url_dialog.cpp`、`first_run_guide_dialog.cpp`）。Mini 侧这些元素对应的过渡时长/曲线并不统一（0.1–0.3 s 不等，部分 `transition: none`），因此没有按"全局 0.15 s"统一改写；本版只对齐了按钮这一族的 0.15 s 语义。

**Deviation note（retro-3d 渐变）**: CSS 不对 `linear-gradient` 做插值（retro-3d 的悬停底色是渐变图片，Mini 里会直接跳到新渐变）；Clash Win 对六个色停做 150 ms 插值，视觉更平滑，但严格说比 Mini 多了一段渐变过渡。

---

## (c) In-app auto-update is wired but the signature path is pending

**What changed in v0.2.2**: the empty-channel gap (❌10) is closed. `.github/workflows/release.yml` now generates a real `updater/app-update.json` manifest on publish, populating `platforms.windows-x86_64` with the setup asset URL, its SHA256, and a Base64 minisign signature. The client (`src/update/client_updater.cpp`) parses and verifies this manifest.

**What is still pending**:
- The minisign **signature** is only produced when the `MINISIGN_PRIVATE_KEY` repository secret is configured. Without it, the manifest is generated **without** a `signature` field and the workflow prints a `::warning::`. `client_updater.cpp` treats minisign verification as a **hard gate** (`:96`/`:460`) — with no signature field, the download-install path **refuses to install**. So the channel is wired end-to-end but **non-functional until the key is configured**.
- The public key compiled into `src/config/update_keys.h` is still **Mini's** (`src-tauri/tauri.conf.json:32` lineage). It must be replaced with a Clash Win-owned keypair before the channel can be safely enabled; otherwise a future signed Mini asset could be mistaken for a valid Clash Win update.

**Consequence**: "Check for updates" in v0.2.2 still will not offer or install anything until a Clash Win-owned minisign keypair is generated and wired into `MINISIGN_PRIVATE_KEY` + `update_keys.h`. Users update by downloading a new Release manually. Do not treat the updater as a security control in this release (see [SECURITY.md](../SECURITY.md)).

---

## (d) v0.2.2 release assets are unsigned — no Authenticode signature

**What it is**: the published v0.2.2 artifacts (`ClashWin_0.2.2_x64-setup.exe` and `ClashWin_0.2.2_x64_portable.zip`) carry **no Authenticode signature**. They ship as **unsigned assets**; the signature status is **recorded but does not block the release** — `scripts/delivery_check.ps1` prints `INFO Authenticode: ...` and never fails on it. **No certificate, local or otherwise, was applied to the final published assets.**

**Why**: Authenticode signing on the release machine requires an elevated (administrator) session. The local self-signed code-signing certificate `CN=ClashWin` has its private key in `LocalMachine\My`, and in a non-elevated session `Set-AuthenticodeSignature` returns `UnknownError` / `NotSigned`. Both `scripts/sign.ps1` and `installer/build_installer.ps1` request UAC elevation to reach that store, which is unavailable in a non-interactive session. Rather than block the release on a signature that would in any case be a **local self-signed certificate with no public trust**, v0.2.2 is published unsigned and the gap is disclosed here.

**Consequences**:
- on any machine, Windows shows "**Unknown publisher**" and SmartScreen may warn. There is no timestamp against a public TSA and no publicly trusted chain (no OV/EV certificate, no Azure Trusted Signing);
- verify every download against `SHA256SUMS.txt` in the Release before running it — the checksum is the only integrity / trust anchor offered for v0.2.2.

**Relation to Mini**: this matches the current state of **Clash Mini v3.2.2 installers, which do not carry an Authenticode signature** (`certificateThumbprint: null` / `timestampUrl: ""` in the Mini Windows bundle config). Mini's `.exe.sig` files are **Tauri updater (minisign) signatures**, not Authenticode — the two mechanisms are different and should not be confused.

**Future**: a publicly trusted code-signing certificate requires a separate procurement decision and an elevated signing environment; it is not part of v0.2.2.

---

## (e) Clean-machine acceptance and UAC #0 / #5 visual checks not performed

**What is missing**: `docs/06-验收状态台账.md` records `EVIDENCE=NOT-CLEAN-MACHINE` and `P0_DECLARATION=DEFERRED` — the **independent-availability P0** chain (a machine that has never had Clash Mini installed: service bootstrap from absent → UAC install → `sc query` RUNNING → kernel Session#=0 → three URLs reachable) was **not executed** on this machine, because it physically has Clash Mini installed, a TUN default route, and service/session state that cannot be removed without disrupting the working environment.

The **manual visual items** of the UAC acceptance list — #0 (zombie PID cleanup / reboot) and #5 (visual confirmation of the UAC publisher line) — likewise require a human at a clean console and **were not performed**. Items #1–#4 (UAC packaging, signature verification, `/KEEP` ×2, uninstall) were executed and recorded on earlier local test builds; that "signature verification" refers to a locally signed test installer, **not** to the published v0.2.2 assets, which are unsigned (see (d)).

**Consequence**: v0.2.2 has **not been proven** to bootstrap itself on a machine without Clash Mini. The installer does contain the service-install path, but treat first-run on a genuinely clean machine as unverified.

---

## (f) The five sidecar executables are not committed

**What is missing**: `.gitignore` excludes `*.exe`, so the five upstream binaries under `sidecar/` (`mini-mihomo.exe`, the service binary, and helpers) are **not in the repository**. Byte sizes and SHA256 baselines are documented in [05-sidecar-内核资产.md](05-sidecar-内核资产.md); `scripts/verify_sidecar.ps1` checks 5/5 and must pass before any build, test or release. The upstream originals themselves must be obtained from a trusted source — the exact upstream retrieval path/version tag is still an open item in that document.

**Consequence**: a **fresh clone compiles but cannot run** — it produces `clashwin.exe` with no kernel or service component. The published Release assets bundle the sidecar so end users get a runnable package; source-only consumers must supply the binaries themselves and verify them. The release CI (`.github/workflows/release.yml`) restores them from a maintainer-controlled archive (`vars.SIDECAR_ARCHIVE_URL`) and re-runs the 5/5 baseline check; **if that repository variable is not configured, the build job fail-fast aborts the release** with an explicit message rather than producing an unrunnable package.

---

## Process note — 用户已确认 gate

The 11 parity fixes in v0.2.2 are code-complete and marked **已处理 (code landed)** in `Win和mini的差别.md`; the final **用户已确认** (maintainer rebuilds and desktop-reviews each item) is pending. v0.2.2 is released on the strength of the code-level 1:1 alignment and the green regression run; the desktop-confirmation pass is the maintainer's next step, not a blocker for shipping.

---

## Not claimed

This release does **not** claim:

- "perfect 1:1 parity" or "no known differences";
- a publicly trusted code-signing chain (the v0.2.2 assets are unsigned);
- a working in-app auto-update channel (signature/key ownership still pending);
- verified clean-machine bootstrap;
- that a source-only checkout is runnable without the sidecar binaries.

For the full release scope see [RELEASE_NOTES_0.2.2.md](RELEASE_NOTES_0.2.2.md), the [Changelog](../CHANGELOG.md), and the [acceptance ledger](06-验收状态台账.md). Report other problems via the [issue templates](https://github.com/qiu-yuxiao/clash-win/issues/new/choose); disclose security issues privately per [SECURITY.md](../SECURITY.md).
