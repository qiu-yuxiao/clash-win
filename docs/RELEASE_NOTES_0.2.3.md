# Clash Win v0.2.3 — Release Notes

**Release date**: 2026-11-10
**Repository**: https://github.com/qiu-yuxiao/clash-win

---

## Version & positioning

- **Clash Win v0.2.3** continues the project's **own** version line (started at `0.1.0`) and increments independently; it is **not bound to Clash Mini's version**, which is only the source-reference version used when citing the code being ported (constitution §5.3, `include/clashwin/config/constants.h`).
- **Positioning**: a **Windows-native C++20 / Win32 / Direct2D 1:1 port of Clash Mini**, with the display name `Clash Mini` → `Clash Win` as the only public difference. No web engine, no JavaScript runtime.
- **v0.2.3 is a kernel + coexistence + parity-maintenance release**: it upgrades the bundled core to the official upstream mihomo v1.19.33 build, lands the user-ratified exclusive-coexistence policy, and closes the G1–G10 behavior/layout gaps found in the Mini v3.2.3 increment audit.

## What is in this release

| Area | Content |
| :--- | :--- |
| Runtime | Single native `clashwin.exe` (C++20), Direct2D/DirectWrite rendering, six skins |
| Kernel (v0.2.3) | `win-mihomo.exe` = **official MetaCubeX/mihomo v1.19.33 gvisor build**, retrieved directly from the upstream GitHub Release (SHA256 verified at adoption) |
| Coexistence (v0.2.3) | Exclusive coexistence fully landed: launch **and** install-time mutual kill (client + core + service takeover), ownership judged by exe directory path, never deletes the other side's disk files |
| Parity fixes (v0.2.3) | G1–G4 filter behavior + G6–G10 log dialog layout aligned to Clash Mini v3.2.3 — see table below |
| Workflows | Subscription import (19 protocols), auto node benchmarking/switching, rule & GeoIP updates, mini traffic monitor, right-click routing, streaming-unlock detection, first-run guide, tray, autostart |
| Localization | 13 UI languages (`locales/`); installer starts in Simplified Chinese / English / Russian |
| Auto-update | CI generates a real `updater/app-update.json` manifest (`platforms.windows-x86_64` url + sha256 + minisign signature) on publish; the client verifies it against the project-owned public key in `src/config/update_keys.h` |
| Verification | Full regression passed locally (`678 cases`, `0 failed`, `EXIT=0`) on the v0.2.3 tree |

## Parity fixes in v0.2.3

| # | Area | Fix |
| :--- | :--- | :--- |
| G1 | Node filter | Auto-select debounce 800 → 700 ms, matching Mini v3.2.3 |
| G2 | Node filter | Removed the Win-added "trigger auto-select on focus loss" — Mini triggers on debounce completion or Enter only |
| G3 | Node filter | IME composing guard: Enter during pinyin composition no longer triggers auto-select (Mini `!isComposing && keyCode !== 229`) |
| G4 | Node filter | Same-word dedup: re-submitting the identical filter word does not re-trigger |
| G6–G8 | Log dialog | 44 px header, title 15 px/600, level dropdown 95 px, divider position per Mini v3.2.3 |
| G9 | Log dialog | Pause / sort / clear are 18 px MUI vector icons (sort icon vertically flipped for descending) |
| G10 | Log dialog | Payload wraps (`anywhere`) with dynamic row height instead of ellipsis + horizontal scrollbar |

## Kernel & coexistence changes in v0.2.3

- **Core binary**: `sidecar/win-mihomo.exe` is the official `mihomo-windows-amd64-gvisor-v1.19.33.zip` payload from MetaCubeX/mihomo (`1CDE41CFFFD3DE0AEDAD63C0A8515E1CE5EEA507E8133AED0F637D316C93800A`, 61,247,488 bytes). The name `win-mihomo` is a **user-ratified special case** to the naming-parity rule, completing the coexistence naming chain (verge-mihomo → mini-mihomo → win-mihomo) so each client's kill-by-ownership logic can distinguish sides.
- **Mutual exclusion**: starting or installing Clash Win kills Clash Mini's client/core processes and vice versa (strong-kill, ownership by install-directory path — a same-name core inside the other client's own directory is not mis-killed), then takes over the shared `clash_verge_service` registration to point at its own service binary. The other client's installation directory, configuration, and assets are never touched.
- **`geodata-loader` clarified**: neither Clash Mini nor Clash Win sets it explicitly; both inherit mihomo's `memconservative` factory default. 1:1, no change shipped.

## Artifacts

| File | Form | Notes |
| :--- | :--- | :--- |
| `ClashWin_0.2.3_x64-setup.exe` | NSIS installer | per-machine (`Program Files`), **requires UAC** at install time; installs/shares the Windows service |
| `ClashWin_0.2.3_x64_portable.zip` | Portable | extract & run, `.config/PORTABLE` present, no installer, no registry writes |
| `SHA256SUMS.txt` | Checksums | SHA256 of the two artifacts above — verify before running |

> **Both artifacts are unsigned in v0.2.3** (no Authenticode signature), same as Clash Mini's current installers. Windows will show "Unknown publisher" and SmartScreen may warn; `SHA256SUMS.txt` is the trust anchor. See [Known Limitations (d)](KNOWN_LIMITATIONS_0.2.3.md) and [SECURITY.md](../SECURITY.md).

Download: https://github.com/qiu-yuxiao/clash-win/releases/tag/v0.2.3

```powershell
Get-FileHash .\ClashWin_0.2.3_x64-setup.exe -Algorithm SHA256
Get-FileHash .\ClashWin_0.2.3_x64_portable.zip -Algorithm SHA256
```

## Quick start

1. Download `ClashWin_0.2.3_x64-setup.exe` (installer) or `ClashWin_0.2.3_x64_portable.zip` (portable) and verify against `SHA256SUMS.txt`.
2. If Clash Mini is running, Clash Win will close it on first launch (mutual exclusion — by design; nothing of Mini's is deleted).
3. Import a subscription, pick a node, done. The UI layout, behavior and visuals follow Clash Mini 1:1 except the display name.

## Upgrade from v0.2.2

- The in-app updater (`Settings → 检查更新` or the tray item) now sees v0.2.3 through the signed manifest on `main`; download → verify → install → relaunch is fully automatic.
- Manual upgrade: run the new setup.exe over the old install (settings preserved), or re-extract the portable zip.

---

*Clash Win is a private parity-verification project (constitution §1: not for public distribution). Every shipped change is anchored to a Clash Mini `file:line` or an explicit user ruling.*
