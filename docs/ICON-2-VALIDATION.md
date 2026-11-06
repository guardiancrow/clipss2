# ICON-2 local validation

This change embeds the adopted capture-frame icon in the Windows executable,
Settings and notification area. It does not publish a release or replace any
v0.1.1 source/binary identity. The version remains 0.1.1 locally; this is a changed
development executable and must not be presented as the published binary.

## Local build scope

`icon-local` is a developer-only scope with independent Ninja single-config x64
MSVC Debug/Release build directories under `build/icon-phase2/`. It reuses an
original, unchanged SEC-BR1 dependency checkout, validated read-only through the
existing SEC-BR2 dependency and managed-source validators. These checks validate
that original dependency checkout, not admission of the changed product source
under any historical release contract.

The existing S1/SEC-BR1/SEC-BR2 scopes, ledgers, source bindings, receipts, markers,
preimages, packaging profiles and output directories are not updated. Configure
against `icon-local`; do not rehash new product sources into those contracts or
run historical Configure/ValidateProduct as a substitute for new acceptance.
Future release source/binary/package identities require a separate phase.

No dependency download, install, upgrade or global Git/tool configuration change
is part of this build. The dependency tree must already contain the admitted
release/debug packages and original/derived jpegli sources. The tested toolchain
is the existing MSVC 14.44.35207 (compiler 19.44.35229.0), Windows SDK 10.0.26100.0,
CMake 4.4.4 and Ninja. Release uses the existing `/MD` policy; Debug `/MDd` is for
local development only and is not a redistributable payload.

From the repository root, explicitly identify the original dependency checkout
in your local environment, then choose fresh output roots:

```powershell
pwsh -NoProfile -File tools/build_icon_local.ps1 -DependencyRoot $env:CLIPSS2_ICON_DEPENDENCY_ROOT -Configuration Release -OutputRoot build/icon-phase2/release-example
pwsh -NoProfile -File tools/build_icon_local.ps1 -DependencyRoot $env:CLIPSS2_ICON_DEPENDENCY_ROOT -Configuration Debug -OutputRoot build/icon-phase2/debug-example
```

The helper imports the installed x64 MSVC developer environment if needed,
configures CMake, builds all default targets and runs CTest. It preserves failed
outputs and refuses existing output roots. Logs, compiler temporary files and
JUnit results remain in that newly owned root. Sandbox ownership/desktop rules
may require running the same command in the Owner's normal developer environment;
do not change global `safe.directory` settings to hide an ownership mismatch.

## Automated checks

`app_icon` loads the embedded resource at all seven sizes, checks owned fallback
behavior and exercises `WM_SETICON` replacement at 96/192/144/96 DPI metrics. This
tests icon selection and lifetimes in a hidden test window; it does not prove
actual monitor/theme rendering. Existing tray and other CTest cases remain active.

Inspect the actual built executable, not just the source ICO:

```powershell
pwsh -NoProfile -File tools/verify_app_icon.ps1 -Executable build/icon-phase2/release-example/product/clipss2.exe -ReportPath build/icon-phase2/release-example/pe-icons.json
```

This maps the PE as data without executing its entry point. It checks
`RT_GROUP_ICON` ID 101, seven `RT_ICON` resources byte-for-byte against the adopted
ICO's RGBA PNG frames, VERSIONINFO 0.1.1.0 / 0.1.1, and the retained manifest DPI
and detached-console settings. The report refuses to overwrite an existing file.
The local executable can be launched with `--help`, `--settings` or `--resident`.
Do not run capture actions as an icon test.

For an isolated Settings trial, a **new** product directory can contain a
`clipss2.portable` file with exactly `clipss2-portable-v1` followed by LF. This keeps
the trial independent of per-user settings. Close Settings with Cancel or its
close button; do not save over existing settings. Quit the trial resident through
its own tray menu. Never terminate another running clipss2 instance for this test.

## Owner acceptance checklist

- [ ] Explorer small/medium/large icons show the blue plate and four frames.
- [ ] Standalone Settings title bar, taskbar and Alt+Tab use the custom icon.
- [ ] Settings opened from the resident has the same icon and taskbar behavior.
- [ ] Notification-area icon is legible, opens the existing menu and exits cleanly.
- [ ] Recovery after a real Explorer/taskbar recreation retains one functioning icon.
- [ ] Light/dark desktop themes and actual 100%/200% monitor DPI are acceptable.
- [ ] Existing capture-selection behavior remains unchanged; internal overlays do
  not acquire taskbar buttons.

API checks or a synthetic `TaskbarCreated` message do not mark these visual checks
complete. If icon caching is suspected, first inspect the PE and use the new
executable path; do not delete the system icon cache. Record actual observations
and remaining manual items separately in local evidence under `build/icon-phase2/`.

## License and privacy scope

The icon and helper code are original project assets under the existing MIT
LICENSE; no third-party artwork or new runtime/tool payload is introduced. The
existing `clipss2-g2-inheritance-2026-10-23` tool eligibility and Release linking
decisions apply within their established scope. This local check does not reopen
them or grant release approval. Existing unresolved distribution matters remain
unchanged.

Review only this change's tracked/new source files and icon metadata for paths,
credentials and unrelated material. Local previews, screenshots, build products,
logs and verification reports stay ignored under `build/`; do not copy them into
the public asset directory. This is a change-scoped review, not a full history or
publication audit.
