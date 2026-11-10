# Clash Win v0.2.4 — Release Notes

**Release date**: 2026-11-10
**Repository**: https://github.com/qiu-yuxiao/clash-win

---

## Version & positioning

- **Clash Win v0.2.4** continues the project's **own** version line (started at `0.1.0`) and increments independently; it is **not bound to Clash Mini's version**, which is only the source-reference version used when citing the code being ported (constitution §5.3, `include/clashwin/config/constants.h`).
- **Positioning**: a **Windows-native C++20 / Win32 / Direct2D 1:1 port of Clash Mini**, with the display name `Clash Mini` → `Clash Win` as the only public difference. No web engine, no JavaScript runtime.
- **v0.2.4 is a coexistence-fix release**: it fixes the "switch to TUN and the browser loses all connectivity" regression reported when Clash Win was installed on a machine where Clash Mini was already running in TUN mode. No kernel change, no parity-behavior change.

## What is in this release

| Area | Content |
| :--- | :--- |
| Runtime | Single native `clashwin.exe` (C++20), Direct2D/DirectWrite rendering, six skins |
| Kernel | unchanged from v0.2.3 — `win-mihomo.exe` = official MetaCubeX/mihomo v1.19.33 gvisor build |
| Coexistence fix (v0.2.4) | The "kill the other side" rule now (1) really terminates the other client's **elevated** core instead of silently skipping it, and (2) tears down any leftover `Wintun`/`Mihomo` virtual adapter + routes the other side left behind |
| Verification | Full regression passed locally (`678 cases`, `0 failed`, `EXIT=0`) on the v0.2.4 tree |

## The bug this release fixes

**Symptom**: On a machine where **Clash Mini was already running in TUN mode**, installing / launching Clash Win worked fine in system-proxy mode (inbound + outbound traffic normal), but the moment you switched Clash Win to **TUN mode**, the popup said "switched successfully" yet **all browsing — domestic and foreign — stopped**.

**Root cause (two gaps in the coexist "kill the other side" rule)**:

1. **The kill silently failed on an elevated core.** Clash Mini in TUN mode runs its core with administrator rights. Clash Win's coexist code opened the other process for termination, but if that open was denied (it is, when the target is elevated and Win is not), it just `continue`d and skipped — no warning. So Clash Mini's core survived, and **two cores ran at once**.
2. **The kill left the other side's TUN adapter orphaned.** Even when the other process died, the `Wintun Tunnel` / `Mihomo` virtual adapter (and its `0.0.0.0/0` route) it had created was never removed. When Clash Win then opened TUN, it collided with / reused that stale adapter and the whole traffic takeover failed → browser dead.

**Why system-proxy mode looked "fine"**: proxy traffic goes to `127.0.0.1` (loopback) and does not touch the orphaned adapter's routes, so it kept working; only the full-traffic TUN takeover exposed the conflict.

## Fix

- `system/coexist.cpp` — `kill_other_processes()` now matches the other side by process name up front and, on access-denied, logs an explicit warning instead of silently skipping. Added `remove_orphan_wintun_adapters()` which (when running elevated, which the TUN path always is) removes any `Wintun`/`Mihomo` adapter via `Get-NetAdapter`/`Remove-NetAdapter`. It touches **no disk files** of the other side, consistent with the "never delete the other side's files" rule.
- `app/app.cpp` (startup kill rule) and `cmd/cmd_verge.cpp` (TUN-entry path, always elevated) both call the cleanup. The TUN-entry call is the key one: it kills the other elevated core **and** clears its orphan adapter **before** Clash Win (re)starts its own core and opens TUN.
- `tests/test_t35_release_readiness.cpp` — version gate literal bumped to `0.2.4`.

## Artifacts

| File | Form | Notes |
| :--- | :--- | :--- |
| `ClashWin_0.2.4_x64-setup.exe` | NSIS installer | per-machine (`Program Files`), **requires UAC** at install time; installs/shares the Windows service |
| `ClashWin_0.2.4_x64_portable.zip` | Portable | extract & run, `.config/PORTABLE` present, no installer, no registry writes |
| `SHA256SUMS.txt` | Checksums | SHA256 of the two artifacts above — verify before running |

> **Both artifacts are unsigned in v0.2.4** (no Authenticode signature), same as Clash Mini's current installers. Windows will show "Unknown publisher" and SmartScreen may warn; `SHA256SUMS.txt` is the trust anchor. See [Known Limitations (d)](KNOWN_LIMITATIONS_0.2.4.md) and [SECURITY.md](../SECURITY.md).

Download: https://github.com/qiu-yuxiao/clash-win/releases/tag/v0.2.4

```powershell
Get-FileHash .\ClashWin_0.2.4_x64-setup.exe -Algorithm SHA256
Get-FileHash .\ClashWin_0.2.4_x64_portable.zip -Algorithm SHA256
```

## Upgrade from v0.2.3

- In-app updater: the signed manifest on `main` now reports `0.2.4`; download → verify → install → relaunch is automatic.
- Manual: run the new `setup.exe` over the old install (settings preserved), or re-extract the portable zip.

---

*Clash Win is a private parity-verification project (constitution §1: not for public distribution). Every shipped change is anchored to a Clash Mini `file:line` or an explicit user ruling.*
