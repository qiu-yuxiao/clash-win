# CONTRIBUTING

Thank you for your interest in contributing to **Clash Win**! This guide covers the development setup, the 1:1 parity discipline, and the checks a change must pass before it can be merged.

> **Read first**: `移植基本准则.md` (the project constitution) in the repository root. It defines the only permitted deviations from Clash Mini v3.2.2, the version line, and the submission/evidence rules. Its rules take precedence over this file.

## Relationship to Clash Mini

Clash Win is a **Windows-native C++20/Win32/Direct2D 1:1 port** of Clash Mini v3.2.2. The only public difference is the display name `Clash Mini` → `Clash Win`. Consequences for contributors:

- **Do not invent behavior.** If Mini's source (TS/TSX + Rust) has a behavior, reproduce it — including its quirks — rather than "improving" it.
- **Do not add features Mini does not have.** If a native implementation requires something Mini lacks (Webview2 lifecycle, zip extraction), implement semantic equivalence and register the "form difference" with its rationale in `docs/`.
- **Reference Mini with `file:line`.** Every parity claim should cite the Mini source location. Mini is a read-only reference; never modify it.
- **Conflicts go to the maintainer.** When a ruling record and Mini v3.2.2 source evidence conflict, stop and report both `file:line` quotes. Do not self-adjudicate (§3.2 of the constitution).

## Development Setup

### Prerequisites

| Requirement | Version |
| :--- | :--- |
| OS | Windows 10/11 **x64** (Clash Win is Windows-only) |
| IDE / compiler | Visual Studio 2022 (or VS Build Tools) with **Desktop development with C++** |
| CMake | 3.20+ |
| NSIS | 3.x (only needed to build the installer) |
| Python | 3.x (test runner) |
| PowerShell | 5.1+ (packaging / verification scripts) |

Third-party C/C++ dependencies are vendored under `third_party/`; no vcpkg or package manager bootstrap is required.

### Configure and build

```powershell
# 1. Configure (VS2022 generator, x64)
cmake -B build -G "Visual Studio 17 2022" -A x64
#    or: cmake --preset default

# 2. Build Release
cmake --build build --config Release
#    or: cmake --build --preset release

# 3. Run
.\build\bin\Release\clashwin.exe
```

### Sidecar kernels (important)

The five upstream binaries under `sidecar/` (`mini-mihomo.exe`, the service, and helpers) are **not committed** — `.gitignore` excludes `*.exe`. A fresh clone therefore **compiles but cannot run**. See `docs/05-sidecar-内核资产.md` for the baseline (byte sizes and SHA256 values) and how to obtain the upstream originals.

```powershell
# After placing the 5 upstream binaries into sidecar/:
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify_sidecar.ps1
# Exit 0 = 5/5 match the baseline. Any mismatch -> do not build, test or release.
```

Never commit, replace, or "fix" these binaries, and never add an `*.exe` to a PR.

### Run the tests

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run_tests.ps1                 # full suite, isolated temp directory
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run_tests.ps1 -Filter t35     # name-substring filter
```

The unified PowerShell runner copies `clashwin_test.exe` plus `sidecar/`, `locales/`, `resources/`, and `assets/locales/` into an isolated run directory, sets `CLASHWIN_REPO_ROOT` and `TMP`/`TEMP`, and writes the full log to `build/cwtest_logs/tests_full.log`. The pass criterion is **`EXIT=0` + `0 failed` + `0 assertion failures`** — the case count grows as new regressions are added.

Every new or fixed defect **must** come with a regression case, and that case must be validated by temporarily reverting the fix (case fails) and restoring it (case passes). New test `.cpp` files must also be registered in the `add_executable(clashwin_test ...)` source list in `CMakeLists.txt` — an unregistered file silently contributes zero executed cases.

### Build the distributables

```powershell
# Portable ZIP (layout: clashwin.exe + sidecar\ + resources\ + locales\
#               + .config\PORTABLE + 用户必读.txt)
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\portable.ps1 -Version <version>

# NSIS installer
powershell -NoProfile -ExecutionPolicy Bypass -File installer\build_installer.ps1 -Version <version>
```

Both write to `dist/`: `ClashWin_<version>_x64_portable.zip` and `ClashWin_<version>_x64-setup.exe`. Official releases rely on `SHA256SUMS.txt` plus Minisign Ed25519 signatures for in-app updates — see [Known Limitations](docs/KNOWN_LIMITATIONS.md). Do **not** pass `-SkipStage` when preparing a release artifact.

### Pre-submit checks

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ui_src_gate.ps1       # UI source attribution gate
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release_preflight.ps1 # 7-gate static & baseline preflight
```

`release_preflight.ps1` verifies version single-source-of-truth parity (`constants.h` / `clashwin.rc` / `clashwin.nsi`), update-manifest safety, `CHANGELOG.md` completeness, CMake test registration, 5/5 upstream `sidecar/` SHA256 hashes, `ui_src_gate.ps1`, and script UTF-8 BOM / hook LF encoding.

## Committing and submitting

1. Fork the repository and create a topic branch.
2. Make small, verifiable steps: one change → rebuild → full test run → evidence (`docs/evidence/`).
3. Commit with a clear message. **Signed commits are required** — see [GitHub: signing commits](https://docs.github.com/en/authentication/managing-commit-signature-verification/signing-commits).
4. Add an explicit `git add` for every new file (including untracked test sources); `git commit -a` / `git add -u` will silently omit them. Verify `git status --porcelain` is empty before asking for a release build.
5. Open a PR using `.github/PULL_REQUEST_TEMPLATE.md`, citing Mini `file:line` for parity claims.

Code changes and local commits are within normal contributor authority. **Pushing a tag, creating a Release, or uploading assets is a release action** — follow `.github/workflows/release.yml` and the repository maintainer's release decision; do not tag or publish from a feature branch.

## Versioning

Clash Win has its **own** version line starting at `0.1.0` and incrementing independently (constitution §5.3). It does **not** track Clash Mini's `3.2.2`; that number is only the source-reference anchor used when citing Mini code. The single source of truth is `kAppVersion` in `include/clashwin/config/constants.h`; `README`, `CHANGELOG.md`, installer defines, portable package names and Release tags must all agree with it.

## Copyright and Licensing

Copyright (c) 2026 秋雨潇潇 <qiuyuxiao@gmail.com> (portions relating to the native rewrite and custom modifications).
Copyright (c) the original authors of Clash Verge Rev and Clash Mini (upstream lineage).

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0-only)**. By contributing, you agree that your contributions are licensed under the same terms. See [LICENSE](LICENSE).

## Releasing（发版）

Releasing follows a **mandatory, deterministic process**. Full runbook: [`docs/RELEASE_PROCESS.md`](docs/RELEASE_PROCESS.md).

- **Single entry point**: `scripts/release.ps1`. Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release.ps1 -Version <X.Y.Z>` first for a zero-remote-side-effect local rehearsal (syncs `constants.h` / `src/clashwin.rc` / `installer/clashwin.nsi`, runs all 7 preflight gates, builds Release + runs full isolated regression, and packages + verifies both `portable.zip` and `setup.exe` locally). Then run with `-SkipBuild -Push -Yes` to push `dev`/`main` + tag, watch CI, download & verify `dist/` assets, and sync `updater/app-update.json` back across branches.
- **Install the hook right after cloning**: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\install_hooks.ps1`. Pushing any `v*` tag automatically triggers `release_preflight.ps1` and **rejects the push if any gate fails**.
- **Never hand-type `git tag` + push**: CI (`release.yml`) runs `validate` and `release_preflight.ps1` after restoring `sidecar/`; any failure aborts the workflow before publishing.

> Pushing a tag, creating a Release, or uploading assets is a **release action** reserved for the maintainer per [`docs/RELEASE_PROCESS.md`](docs/RELEASE_PROCESS.md) — never tag or publish from a feature branch by hand.
