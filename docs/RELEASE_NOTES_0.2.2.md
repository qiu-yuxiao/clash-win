# Clash Win v0.2.2 — Release Notes

**Release date**: 2026-11-09
**Repository**: https://github.com/qiu-yuxiao/clash-win

---

## Version & positioning

- **Clash Win v0.2.2** continues the project's **own** version line (started at `0.1.0`) and increments independently; it is **not bound to Clash Mini 3.2.2**, which is only the source-reference version used when citing the code being ported (constitution §5.3, `include/clashwin/config/constants.h:20`).
- **Positioning**: a **Windows-native C++20 / Win32 / Direct2D 1:1 port of Clash Mini v3.2.2**. The only public difference is the display name `Clash Mini` → `Clash Win` (constitution §5.1). No web engine, no JavaScript runtime.
- **v0.2.2 is a parity-maintenance release**: it closes the audit gaps found in the `Win和mini的差别.md` audit against Clash Mini v3.2.2 — 9 confirmed-missing items + 2 sub-items + 1 morphology item (△8). Every change is aligned to a specific Mini `file:line`.

## What is in this release

| Area | Content |
| :--- | :--- |
| Runtime | Single native `clashwin.exe` (C++20), Direct2D/DirectWrite rendering, six skins |
| Parity fixes (v0.2.2) | 11 audit items closed — see "Parity fixes in v0.2.2" below |
| Coexistence (v0.2.1) | Clash Win and Clash Mini now **mutually exclude** each other (launching one terminates the other's client/core + takes over the shared `clash_verge_service`); ports / proxy-secret / identifiers return to 1:1 with Clash Mini's scheme |
| Workflows | Subscription import (19 protocols), auto node benchmarking/switching, rule & GeoIP updates, mini traffic monitor, right-click routing, streaming-unlock detection, first-run guide, tray, autostart |
| Localization | 13 UI languages (`locales/`); installer starts in Simplified Chinese / English / Russian |
| Auto-update (v0.2.2) | CI generates a real `updater/app-update.json` manifest (`platforms.windows-x86_64` url + sha256 + minisign signature) on publish; the client verifies it against the project-owned public key in `src/config/update_keys.h` (private key only in the `MINISIGN_PRIVATE_KEY` Actions secret) |
| Verification | Full regression passed (`EXIT=0`, `0 failed`, `0 assertion failures`) on the local build; 11 parity items are code-complete and marked 已处理, pending final desktop confirmation (用户已确认 gate) |

## Parity fixes in v0.2.2

| # | Area | Fix |
| :--- | :--- | :--- |
| ❌1 | Node list | Wide window (>285px content) lays out nodes as a 3-column card wall; narrow window stays single-column (centered) — matches Mini `proxy-render.tsx:88` / `use-render-list.ts:88` |
| ❌2 | Delay | Dead nodes (`alive===false`) render as Timeout(0) instead of a stale green delay — closes the 2026-10-24 incident recurrence path (Mini `delay.ts:279-281`) |
| ❌3 | Self-heal | `refresh_proxies` is broadcast even when no candidate survives, so the UI never sticks on stale delay/alive (Mini `monitor.rs:1131-1139`) |
| ❌4 | Probe | On-demand probe gating skips redundant probes for healthy/fresh (<60s) or freshly-dead (<10s) nodes — reproduces Mini's fix for probe self-excitation false-deaths (`monitor.rs:64/69/673-683`) |
| △8 | Self-heal alert | 3 consecutive failures reset the counter to zero; only an API-unreachable → core-restart → restart-fail ladder raises the alert (Mini current behavior; removed the standalone 5-fail popup) |
| ❌5 | Settings | A failed `patch_verge`/`patch_clash` snaps the toggle back to the authoritative value (Mini `mutateInfo` behavior) |
| ❌6 | Autostart | Enabling launch-on-boot also writes `StartupApproved\Run` (REG_BINARY, `0x02` = approved), so the config wins over Task Manager's disable |
| ❌7 | Autostart | The launch-on-boot switch reflects the actual Run-key state (Mini `autostart.rs:65`), not the verge config value |
| ❌8 | Rendering | `Widget::paint` wraps `on_paint` in try/catch and draws a placeholder rect instead of crashing the whole tree on a single widget exception (Mini `AreaErrorFallback` boundary) |
| ❌9 | First-run | Dismissing the first-run guide with "later" auto-opens the settings drawer to point at the subscription card (Mini `first-run-guide-dialog.tsx:469-473`) |
| ❌10 | Auto-update | `release.yml` now generates `updater/app-update.json` with a real `platforms.windows-x86_64` (url + sha256 + signature) on publish |
| ❌11 | Coexistence | Mini/Win mutual-exclusion policy (kill the other's processes + service at launch/install, shared `clash_verge_service`, never delete the other's disk files) — landed in v0.2.1 (commit 264624b) |

## Artifacts

| File | Form | Notes |
| :--- | :--- | :--- |
| `ClashWin_0.2.2_x64-setup.exe` | NSIS installer | per-machine (`Program Files`), **requires UAC** at install time; installs/shares the Windows service |
| `ClashWin_0.2.2_x64_portable.zip` | Portable | extract & run, `.config/PORTABLE` present, no installer, no registry writes |
| `SHA256SUMS.txt` | Checksums | SHA256 of the two artifacts above — verify before running |

> **Both artifacts are unsigned in v0.2.2** (no Authenticode signature). Windows will show "Unknown publisher" and SmartScreen may warn; the signing step needs an elevated session that is unavailable in the non-interactive release environment, so the signature status is recorded but does not block the release. `SHA256SUMS.txt` is the trust anchor. See [Known Limitations (d)](KNOWN_LIMITATIONS_0.2.2.md) and [SECURITY.md](../SECURITY.md).

Download: https://github.com/qiu-yuxiao/clash-win/releases/tag/v0.2.2

```powershell
Get-FileHash .\ClashWin_0.2.2_x64-setup.exe -Algorithm SHA256
Get-FileHash .\ClashWin_0.2.2_x64_portable.zip -Algorithm SHA256
```

## Quick start

1. Download either artifact from the Release above and (recommended) verify its SHA256.
2. Installer: double-click and accept the UAC prompt. Portable: extract to any folder and run `clashwin.exe`.
3. Click the **[Gear]** button, paste a subscription link / node config, click **[Import Node Info]**, wait for the node to activate.
4. Browsers work immediately (system proxy is set automatically). For apps with a manual proxy option, use `127.0.0.1` and port `10802` (SOCKS) or `10803` (HTTP); otherwise enable **TUN Mode**.

> **Clash Win and Clash Mini now mutually exclude each other** (v0.2.1 change). Launching one terminates the other's client and core processes and takes over the shared service. Do **not** attempt to run both at the same time — they are designed to be exclusive, not coexistent.

## Known limitations (summary)

Full detail and evidence in [KNOWN_LIMITATIONS_0.2.2.md](KNOWN_LIMITATIONS_0.2.2.md):

- (a) Direct2D has no CSS `backdrop-filter` primitive; the frosted-glass **Blur** effect is **not implemented** in v0.2.2.
- (b) The 0.15 s hover transition is implemented for the `Button` family (incl. skin cells); other self-drawn hover surfaces remain discrete.
- (c) **In-app auto-update is wired with a project-owned minisign keypair**: the public key (key id `BEFA887F30C3BAAD`) is compiled into the client, the private key lives outside the repo and is injected as the `MINISIGN_PRIVATE_KEY` Actions secret. Missing signature / bad signature / download failure / missing public key all abort with a visible notice.
- (d) **v0.2.2 release assets are unsigned — no Authenticode signature.**
- (e) **Clean-machine acceptance and the manual UAC #0 / #5 visual checks were not performed** (this machine has Clash Mini installed; `NOT-CLEAN-MACHINE`).
- (f) The **five sidecar executables are not committed** (`.gitignore` excludes `*.exe`); CI restores them from a maintainer-controlled archive (`vars.SIDECAR_ARCHIVE_URL`).

The 11 parity fixes are code-complete and marked 已处理 in the audit; the final 用户已确认 (desktop confirmation) is pending the maintainer's rebuild-and-review.

## Upstream & license

Clash Win is a sibling project derived from the feature set of Clash Mini (itself a derivative of Clash Verge Rev), with Mihomo as the proxy engine. Upstream copyrights are acknowledged; see [README.md](../README.md#copyright-and-licensing) and [LICENSE](../LICENSE). Licensed under **GPL-3.0-only**.

## Links (this repository only)

- [Releases / 下载](https://github.com/qiu-yuxiao/clash-win/releases)
- [Wiki / FAQ](https://github.com/qiu-yuxiao/clash-win/wiki)
- [Issues](https://github.com/qiu-yuxiao/clash-win/issues) · [Discussions](https://github.com/qiu-yuxiao/clash-win/discussions)
- [Changelog](../CHANGELOG.md) · [Contributing](../CONTRIBUTING.md) · [Security policy](../SECURITY.md)
- [Known limitations v0.2.2](KNOWN_LIMITATIONS_0.2.2.md)
