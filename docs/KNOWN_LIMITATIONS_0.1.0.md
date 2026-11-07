# Clash Win v0.1.0 — Known Limitations

**Scope**: Clash Win v0.1.0 (2026-11-07). This file is an explicit, honest register of what is **not** implemented or **not** verified in the first release. It exists so that "1:1 port" is not misread as "no known differences".

> Clash Win is a Windows-native C++20/Win32/Direct2D **1:1 port of Clash Mini v3.2.2**, with the display name `Clash Mini` → `Clash Win` as the only public difference. Its own version line starts at `0.1.0` and increments independently; `3.2.2` is only the source-reference anchor. The items below are **deviations from a complete parity claim** and are listed rather than hidden.

| # | Area | Status | Evidence |
| :--- | :--- | :--- | :--- |
| (a) | frosted-glass Blur | Not implemented | `docs/04-工程技巧.md`; constitution §六.7 |
| (b) | Global hover transition | Not implemented | `src/ui/` widget rendering |
| (c) | In-app auto-update | Empty channel | `updater/app-update.json`, `src/config/update_keys.h` |
| (d) | Release asset signing | **Unsigned (no Authenticode)** | `scripts/delivery_check.ps1:128-132` (INFO-only), Release assets |
| (e) | Clean-machine / UAC visual checks | Not verified | `docs/06-验收状态台账.md` |
| (f) | sidecar binaries | Not committed | `.gitignore`, `docs/05-sidecar-内核资产.md` |

---

## (a) Direct2D has no CSS `backdrop-filter` primitive — frosted-glass Blur not implemented

**What is missing**: Mini's `frosted-glass` skin uses a CSS `backdrop-filter: blur(...)` driven by the skin's `Blur` factor. **Direct2D has no CSS `backdrop-filter` primitive**; a real implementation would require the **D2D 1.1 migration** — the Direct2D 1.1 effect pipeline (`ID2D1Effect` / Gaussian blur over a backdrop bitmap, with the associated device-context and composition changes). **v0.1.0 has not made that D2D 1.1 migration, so the frosted-glass Blur effect is not implemented.** The skin's other factor (Opacity) and its color/geometry are reproduced; the Blur slider does not produce a real blur.

**Consequence**: with the `frosted-glass` skin, the Blur/val2 slider has no visible blur effect. This is a rendering-pipeline gap, **not** a mapped-to-something-else fake: the project does not pretend the slider works by substituting an unrelated visible property (constitution §六.7).

---

## (b) Global hover 0.15 s transition not implemented

**What is missing**: Mini applies a global ~0.15 s hover color/opacity transition (CSS transition on interactive elements). The native widget system renders discrete hover states; the **0.15 s interpolation is not implemented** in v0.1.0. Hover states themselves (normal/hover/pressed/focused) are present and pixel-verified; only the animated interpolation is absent.

**Consequence**: hover feedback is immediate rather than eased. No functional behavior depends on the transition.

---

## (c) In-app auto-update is an empty channel in v0.1.0

**What is missing**: `updater/app-update.json` ships with `"platforms": {}`. `src/update/client_updater.cpp` therefore parses a manifest with no platform entry, `has_update` evaluates false, and no download or signature verification is triggered. A working channel requires, **as a whole**: a project-owned update endpoint + manifest, a **project-owned minisign keypair** (the corresponding public key compiled into the client), a signed setup asset in a Release, and the changelog/notes wiring.

**Current key state**: `src/config/update_keys.h` still embeds the public key inherited from Clash Mini (`src-tauri/tauri.conf.json:32` lineage). Because `platforms` is empty, **no signature verification or download path is reached** in v0.1.0 — the key is inert. It must be replaced with a Clash Win-owned keypair **before** the update channel is enabled; otherwise a future signed Mini asset could be mistaken for a valid Clash Win update.

**Consequence**: "Check for updates" in v0.1.0 will not offer or install anything. Users update by downloading a new Release manually. Do not treat the updater as a security control in this release (see [SECURITY.md](../SECURITY.md)).

---

## (d) v0.1.0 release assets are unsigned — no Authenticode signature

**What it is**: the published v0.1.0 artifacts (`ClashWin_0.1.0_x64-setup.exe` and `ClashWin_0.1.0_x64_portable.zip`) carry **no Authenticode signature**. They ship as **unsigned assets**; the signature status is **recorded but does not block the release** — `scripts/delivery_check.ps1:128-132` prints `INFO Authenticode: ...` and never fails on it. **No certificate, local or otherwise, was applied to the final published assets.**

**Why**: Authenticode signing on the release machine requires an elevated (administrator) session. The local self-signed code-signing certificate `CN=ClashWin` has its private key in `LocalMachine\My`, and in the current non-elevated session `Set-AuthenticodeSignature` returns `UnknownError` / `NotSigned`. Both `scripts/sign.ps1` and `installer/build_installer.ps1` request UAC elevation to reach that store, which is unavailable in a non-interactive session. Rather than block the release on a signature that would in any case be a **local self-signed certificate with no public trust**, v0.1.0 is published unsigned and the gap is disclosed here.

**Consequences**:

- on any machine, Windows shows "**Unknown publisher**" and SmartScreen may warn. There is no timestamp against a public TSA and no publicly trusted chain (no OV/EV certificate, no Azure Trusted Signing);
- verify every download against `SHA256SUMS.txt` in the Release before running it — the checksum is the only integrity / trust anchor offered for v0.1.0.

**Relation to Mini**: this matches the current state of **Clash Mini v3.2.2 installers, which do not carry an Authenticode signature** (`certificateThumbprint: null` / `timestampUrl: ""` in the Mini Windows bundle config). Mini's `.exe.sig` files are **Tauri updater (minisign) signatures**, not Authenticode — the two mechanisms are different and should not be confused.

**Future**: a publicly trusted code-signing certificate requires a separate procurement decision and an elevated signing environment; it is not part of v0.1.0.

---

## (e) Clean-machine acceptance and UAC #0 / #5 visual checks not performed

**What is missing**: `docs/06-验收状态台账.md` records `EVIDENCE=NOT-CLEAN-MACHINE` and `P0_DECLARATION=DEFERRED` — the **independent-availability P0** chain (a machine that has never had Clash Mini installed: service bootstrap from absent → UAC install → `sc query` RUNNING → kernel Session#=0 → three URLs reachable) was **not executed** on this machine, because it physically has Clash Mini installed, a TUN default route, and service/session state that cannot be removed without disrupting the working environment.

The **manual visual items** of the UAC acceptance list — #0 (zombie PID cleanup / reboot) and #5 (visual confirmation of the UAC publisher line) — likewise require a human at a clean console and **were not performed**. Items #1–#4 (UAC packaging, signature verification, `/KEEP` ×2, uninstall) were executed and recorded on earlier local test builds; that "signature verification" refers to a locally signed test installer, **not** to the published v0.1.0 assets, which are unsigned (see (d)).

**Consequence**: v0.1.0 has **not been proven** to bootstrap itself on a machine without Clash Mini. The installer does contain the service-install path, but treat first-run on a genuinely clean machine as unverified.

---

## (f) The five sidecar executables are not committed

**What is missing**: `.gitignore` excludes `*.exe`, so the five upstream binaries under `sidecar/` (`mini-mihomo.exe`, the service binary, and helpers) are **not in the repository**. Byte sizes and SHA256 baselines are documented in [05-sidecar-内核资产.md](05-sidecar-内核资产.md); `scripts/verify_sidecar.ps1` checks 5/5 and must pass before any build, test or release. The upstream originals themselves must be obtained from a trusted source — the exact upstream retrieval path/version tag is still an open item in that document.

**Consequence**: a **fresh clone compiles but cannot run** — it produces `clashwin.exe` with no kernel or service component. The published Release assets bundle the sidecar so end users get a runnable package; source-only consumers must supply the binaries themselves and verify them. The release CI (`.github/workflows/release.yml`) restores them from a maintainer-controlled archive and re-runs the 5/5 baseline check.

---

## Not claimed

This release does **not** claim:

- "perfect 1:1 parity" or "no known differences";
- a publicly trusted code-signing chain (the v0.1.0 assets are unsigned);
- a working in-app auto-update channel;
- verified clean-machine bootstrap;
- that a source-only checkout is runnable without the sidecar binaries.

For the full release scope see [RELEASE_NOTES_0.1.0.md](RELEASE_NOTES_0.1.0.md), the [Changelog](../CHANGELOG.md), and the [acceptance ledger](06-验收状态台账.md). Report other problems via the [issue templates](https://github.com/qiu-yuxiao/clash-win/issues/new/choose); disclose security issues privately per [SECURITY.md](../SECURITY.md).
