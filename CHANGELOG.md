# Changelog

All notable changes to **Clash Win** are documented here.

Version format follows the project's **own** line starting at `0.1.0` (constitution §5.3): Clash Win versions increment independently and are **not** bound to Clash Mini `3.2.2`, which is only the source-reference anchor for the port.

Format reference: `Changelog.md` in the Clash Mini repository (newest first, `## vX.Y.Z` headings with grouped change bullets). Content here is Clash Win's own history, not a copy of Mini's entries.

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
