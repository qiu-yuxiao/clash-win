# Clash Win v0.2.5 — Release Notes

**Release date**: 2026-11-11
**Repository**: https://github.com/qiu-yuxiao/clash-win

---

## Version & positioning

- **Clash Win v0.2.5** continues the project's **own** version line (started at `0.1.0`) and increments independently; it is **not bound to Clash Mini's version**, which is only the source-reference version used when citing the code being ported (constitution §5.3, `include/clashwin/config/constants.h`).
- **Positioning**: a **Windows-native C++20 / Win32 / Direct2D 1:1 port of Clash Mini**, with the display name `Clash Mini` → `Clash Win` as the only public difference. No web engine, no JavaScript runtime.
- **v0.2.5 is a detail-parity + resource-efficiency release**: it closes a batch of small visual/multi-instance gaps found by side-by-side comparison with Clash Mini, and makes "lite mode" actually idle (closing the gap where Clash Win looked heavier than Clash Mini under Task Manager). No kernel change, no parity-behavior change.

## What is in this release

| Area | Content |
| :--- | :--- |
| Runtime | Single native `clashwin.exe` (C++20), Direct2D/DirectWrite rendering, six skins |
| Kernel | unchanged from v0.2.4 — `win-mihomo.exe` = official MetaCubeX/mihomo v1.19.33 gvisor build |
| Task Manager name | The exe now carries a real version resource, so Task Manager shows **"Clash Win"** instead of the raw `clashwin.exe` |
| Basic settings spacing | The 2×2 switch grid (auto-launch / Allow LAN / silent-launch / Allow IPv6) now matches Clash Mini's spacing exactly (24px column gap, equal-width columns, 13px labels) |
| Single-instance | Launching a second Clash Win is now unconditionally rejected (it wakes the existing window) — matches Clash Mini's `check_singleton` |
| Narrow-window threshold | The 285px "stop rendering the connections panel" breakpoint now uses one shared constant (no magic-number drift) |
| Lite mode | Entering lite mode now truly idles: window timers and the TUN-guard probe thread stop; silent boot defers the render layer so it never pays for a window it is about to hide |

## The fixes in plain language

### 1. Task Manager showed `clashwin.exe` instead of a real name
The main executable had no embedded version resource, so Windows fell back to the raw image name `clashwin.exe`. We added a `VERSIONINFO` block (FileDescription / ProductName = **"Clash Win"**), 1:1 with Clash Mini's own "Clash Mini" branding. Task Manager's "Name" column now reads **Clash Win**.

### 2. Basic-settings switches were mis-spaced vs Clash Mini
The 2×2 grid used a 6px column gap (Clash Mini uses 24px) and gave the two columns unequal widths, so the right-column labels (Allow LAN / Allow IPv6) sat far from their toggles while the two columns were jammed together. Re-aligned to Clash Mini's metrics: equal `1fr 1fr` columns, 24px gap, 13px labels.

### 3. Clash Win could be launched twice
The single-instance guard bound a lock port, but if the bind failed it only bailed out *when* it could confirm an existing instance was alive on that port. Under load the confirmation could time out, and Clash Win would wrongly start a second copy. Clash Mini simply treats "port taken" as "an instance exists, wake it and exit." v0.2.5 adopts that exact semantics — a second launch now always wakes the first window and quits.

### 4. Narrow-window breakpoint had a duplicated magic number
The "don't render the connections panel below 285px" rule was hard-coded in three places, separate from the authoritative `kMiniWidthThreshold` constant. Consolidated to the single constant so a future threshold tweak propagates everywhere. No behavior change.

### 5. Lite mode was not actually idle (the "why does Win look heavier than Mini" fix)
This is the one you noticed in Task Manager. After entering lite mode, Clash Win hid its window but kept two window timers alive: a 1-second "stealth tick" and a 5-second TUN-guard probe (the latter spun up a worker thread every 5s doing admin/service checks). Clash Mini instead *destroys* its webview window, which takes all window timers with it. v0.2.5 stops those timers on entering lite mode and restores them on exit. It also defers creating the render layer on a silent boot that is about to enter lite mode, matching Clash Mini's "don't build a window you're about to hide" optimization.

> **Honest note on memory**: even fully idled, Clash Win's working set will still read slightly higher than Clash Mini's lite mode. That is structural, not a missing port: Mini's lite mode destroys a whole *browser process* (WebView2), while Clash Win is a single native process with no separate renderer to jettison. The GUI render layer is released, but the in-process widget tree + window handle stay resident. This is the trade-off of the native architecture and is documented in Known Limitations (a)/(e).

## Verification

Full regression passed locally (cases, 0 failed, `EXIT=0`) on the v0.2.5 tree, and the pre-release preflight (`scripts/release_preflight.ps1 -Version 0.2.5`) is all-PASS: version single-source-of-truth, sidecar entry names, t35 version gate, kernel-name assertions, and the sidecar 5/5 baseline.

## Artifacts

| File | Form | Notes |
| :--- | :--- | :--- |
| `ClashWin_0.2.5_x64-setup.exe` | NSIS installer | per-machine (`Program Files`), **requires UAC** at install time; installs/shares the Windows service |
| `ClashWin_0.2.5_x64_portable.zip` | Portable | extract & run, `.config/PORTABLE` present, no installer, no registry writes |
| `SHA256SUMS.txt` | Checksums | SHA256 of the two artifacts above — verify before running |

> **Both artifacts are unsigned in v0.2.5** (no Authenticode signature), same as Clash Mini's current installers. Windows will show "Unknown publisher" and SmartScreen may warn; `SHA256SUMS.txt` is the trust anchor. See [Known Limitations](KNOWN_LIMITATIONS_0.2.5.md) and [SECURITY.md](../SECURITY.md).

Download: https://github.com/qiu-yuxiao/clash-win/releases/tag/v0.2.5

```powershell
Get-FileHash .\ClashWin_0.2.5_x64-setup.exe -Algorithm SHA256
Get-FileHash .\ClashWin_0.2.5_x64_portable.zip -Algorithm SHA256
```

## Upgrade from v0.2.4

- In-app updater: the signed manifest on `main` now reports `0.2.5`; download → verify → install → relaunch is automatic.
- Manual: run the new `setup.exe` over the old install (settings preserved), or re-extract the portable zip.

---

*Clash Win is a private parity-verification project (constitution §1: not for public distribution). Every shipped change is anchored to a Clash Mini `file:line` or an explicit user ruling.*
