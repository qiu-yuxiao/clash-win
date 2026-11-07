# Clash Win v0.1.0 — Release Notes

**Release date**: 2026-11-07
**Repository**: https://github.com/qiu-yuxiao/clash-win

---

## Version & positioning

- **Clash Win v0.1.0** starts the project's **own** version line and increments independently from here. It is **not bound to Clash Mini 3.2.2**; `3.2.2` is only the source-reference version used when citing the code being ported (constitution §5.3, `include/clashwin/config/constants.h:20`).
- **Positioning**: a **Windows-native C++20 / Win32 / Direct2D 1:1 port of Clash Mini v3.2.2**. The only public difference is the display name `Clash Mini` → `Clash Win` (constitution §5.1). No web engine, no JavaScript runtime.
- This is the first release. It is **not** claimed to be a perfect or complete 1:1 copy — the registered gaps are listed in [KNOWN_LIMITATIONS_0.1.0.md](KNOWN_LIMITATIONS_0.1.0.md) and repeated below in brief.

## What is in this release

| Area | Content |
| :--- | :--- |
| Runtime | Single native `clashwin.exe` (C++20), Direct2D/DirectWrite rendering, six skins |
| Workflows | Subscription import (19 protocols), auto node benchmarking/switching, rule & GeoIP updates, mini traffic monitor, right-click routing, streaming-unlock detection, first-run guide, tray, autostart |
| Localization | 13 UI languages (`locales/`); installer starts in Simplified Chinese / English / Russian |
| Coexistence | Ports `10811/10812/10813/10814/9108/33337`, secret `adapted-by-clash-win`, config dir `io.github.clash-win.clash-win`, shared `clash_verge_service` |
| Verification | Full regression passed (`EXIT=0`, `0 failed`, `0 assertion failures`) — see [交付记录](06-验收状态台账.md) and `evidence/`. The case count is deliberately not pinned: the suite grows as regression cases are added. |

## Artifacts

| File | Form | Notes |
| :--- | :--- | :--- |
| `ClashWin_0.1.0_x64-setup.exe` | NSIS installer | per-machine (`Program Files`), **requires UAC** at install time; installs/shares the Windows service |
| `ClashWin_0.1.0_x64_portable.zip` | Portable | extract & run, `.config/PORTABLE` present, no installer, no registry writes |
| `SHA256SUMS.txt` | Checksums | SHA256 of the two artifacts above — verify before running |

> **Both artifacts are unsigned in v0.1.0** (no Authenticode signature). Windows will show "Unknown publisher" and SmartScreen may warn; the signing step needs an elevated session that was unavailable in the non-interactive release environment, so the signature status is recorded but does not block the release. `SHA256SUMS.txt` is the trust anchor. See [Known Limitations (d)](KNOWN_LIMITATIONS_0.1.0.md) and [SECURITY.md](../SECURITY.md).

Download: https://github.com/qiu-yuxiao/clash-win/releases/tag/v0.1.0

```powershell
Get-FileHash .\ClashWin_0.1.0_x64-setup.exe -Algorithm SHA256
Get-FileHash .\ClashWin_0.1.0_x64_portable.zip -Algorithm SHA256
```

## Quick start

1. Download either artifact from the Release above and (recommended) verify its SHA256.
2. Installer: double-click and accept the UAC prompt. Portable: extract to any folder and run `clashwin.exe`.
3. Click the **[Gear]** button, paste a subscription link / node config, click **[Import Node Info]**, wait for the node to activate.
4. Browsers work immediately (system proxy is set automatically). For apps with a manual proxy option, use `127.0.0.1` and port `10812` (SOCKS) or `10813` (HTTP); otherwise enable **TUN Mode**.

> If Clash Mini / Clash Verge is installed on the same machine, **do not enable TUN mode or system proxy on both clients at the same time** — enable exactly one active proxy path. Clash Win uses a staggered port set specifically so both can coexist.

## Known limitations (summary)

Full detail and evidence in [KNOWN_LIMITATIONS_0.1.0.md](KNOWN_LIMITATIONS_0.1.0.md):

- (a) Direct2D has no CSS `backdrop-filter` primitive; the frosted-glass **Blur** effect has not been migrated to a D2D 1.1 effect pipeline and is **not implemented** in v0.1.0.
- (b) The global **0.15 s hover transition** is not implemented.
- (c) **In-app auto-update is an empty channel** in v0.1.0 (`updater/app-update.json` → `platforms: {}`). A project-owned minisign keypair must be created before the channel can be enabled; the embedded public key is still Mini's, but **no signature verification or download is triggered** in this release.
- (d) **v0.1.0 release assets are unsigned — no Authenticode signature.** The signing step (`scripts/sign.ps1` / `installer/build_installer.ps1`) requires UAC elevation, unavailable in the non-interactive release session, so the signature status is recorded but does not block the release. Windows will show "**Unknown publisher**" and SmartScreen may warn; verify downloads against `SHA256SUMS.txt`. This matches the current state of Clash Mini v3.2.2 installers, which likewise do not carry an Authenticode signature.
- (e) **Clean-machine acceptance and the manual UAC #0 / #5 visual checks were not performed** on this machine due to physical/environmental conditions.
- (f) The **five sidecar executables are not committed** (`.gitignore` excludes `*.exe`); they are described in [05-sidecar-内核资产.md](05-sidecar-内核资产.md) and checked by `scripts/verify_sidecar.ps1`. A fresh clone compiles but cannot run until those binaries are supplied.

None of the above is presented as "already perfect 1:1" or "no known differences".

## Upstream & license

Clash Win is a sibling project derived from the feature set of Clash Mini (itself a derivative of Clash Verge Rev), with Mihomo as the proxy engine. Upstream copyrights are acknowledged; see [README.md](../README.md#copyright-and-licensing) and [LICENSE](../LICENSE). Licensed under **GPL-3.0-only**.

## Links (this repository only)

- [Releases / 下载](https://github.com/qiu-yuxiao/clash-win/releases)
- [Wiki / FAQ](https://github.com/qiu-yuxiao/clash-win/wiki)
- [Issues](https://github.com/qiu-yuxiao/clash-win/issues) · [Discussions](https://github.com/qiu-yuxiao/clash-win/discussions)
- [Changelog](../CHANGELOG.md) · [Contributing](../CONTRIBUTING.md) · [Security policy](../SECURITY.md)
- [Known limitations v0.1.0](KNOWN_LIMITATIONS_0.1.0.md)
