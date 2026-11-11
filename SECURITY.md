# Security Policy

## Supported versions

| Version | Supported |
| :--- | :--- |
| `0.2.x` (latest release) | ✅ |
| Older / pre-release builds | ❌ |

Clash Win is currently on its `0.2.x` line. Security fixes are delivered as a new `0.2.x` release; there are no long-term support branches yet. Releases and checksums are published at [qiu-yuxiao/clash-win/releases](https://github.com/qiu-yuxiao/clash-win/releases).

## Reporting a vulnerability

**Please do not open a public issue for a security vulnerability.** Use GitHub's private vulnerability reporting for this repository:

- [Report a vulnerability privately](https://github.com/qiu-yuxiao/clash-win/security/advisories/new)

Include, where possible:

- the affected Clash Win version and form (`setup.exe` / portable zip),
- Windows version and build number,
- reproduction steps and a proof of concept,
- the impact you believe it has, and the relevant `file:line` if you have located it.

You will receive an acknowledgement as soon as the report is triaged; there is no guaranteed response SLA for this volunteer project. Please keep the report private until a fix is released.

## Scope

In scope: the native C++20 client in `src/`, `include/`, the NSIS installer in `installer/`, the packaging scripts in `scripts/portable.ps1`, and the update-manifest parsing in `src/update/`.

Out of scope / upstream: the Mihomo core and the Clash Verge service shipped as sidecar binaries are **upstream components** — report issues with them to their own projects. Vulnerabilities in subscription links, third-party rule providers, or the user's own proxy configuration are the operator's responsibility.

## Security-relevant properties (disclosed, not hidden)

These are registered in [docs/KNOWN_LIMITATIONS.md](docs/KNOWN_LIMITATIONS.md); they are stated here so users can make an informed trust decision:

- **Binary trust**: **Release assets carry no commercial Windows Authenticode signature.** Windows will show "Unknown publisher" and SmartScreen may warn on first run. `SHA256SUMS.txt` is the download trust anchor — always verify the downloaded file against it before running it. This matches Clash Mini v3.2.2 installers, which likewise carry no Authenticode signature; Mini's `.exe.sig` files are Tauri updater (minisign) signatures, a different mechanism.
- **Sidecar provenance**: the five `sidecar/*.exe` binaries are **not committed** (`.gitignore` excludes `*.exe`). Their upstream originals and the byte/SHA256 baseline are documented in [docs/05-sidecar-内核资产.md](docs/05-sidecar-内核资产.md); `scripts/verify_sidecar.ps1` checks 5/5. Obtain them only from a source you trust and verify them before use.
- **Update channel**: the in-app updater is wired end-to-end — the release workflow generates `updater/app-update.json` with the setup asset URL, its SHA256 and a Base64 minisign signature, signed with the project-owned private key held only in the repository secret `MINISIGN_PRIVATE_KEY`. The client verifies that signature against the public key compiled into `src/config/update_keys.h` (key id `BEFA887F30C3BAAD`) and **refuses to install** on a missing/mismatched signature, a missing public key or a failed download, with a user-visible notice (`src/update/client_updater.cpp`). This authenticates the update *bytes* against the project's signing key; it does **not** make the binary Authenticode-trusted (see "Binary trust"), so `SHA256SUMS.txt` remains the check for a manually downloaded release.
- **Privileges**: the NSIS installer is per-machine (`RequestExecutionLevel admin`) and installs/shares the `clash_verge_service` Windows service; enabling TUN mode or the system proxy writes machine/user network settings. On a machine also running Clash Mini, enable exactly one active proxy path.
- **No telemetry**: Clash Win does not phone home beyond the subscription/rule URLs the user configures and the (signature-gated) update manifest endpoint.

## Verification of downloads

```powershell
# In the folder containing the downloaded artifacts:
Get-FileHash .\ClashWin_<version>_x64-setup.exe -Algorithm SHA256
Get-FileHash .\ClashWin_<version>_x64_portable.zip -Algorithm SHA256
# Compare against the SHA256SUMS.txt attached to the same Release.
```
