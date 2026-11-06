# ICON-3 — v0.1.2 local Portable candidate

v0.1.1 is published. v0.1.2 is an unpublished candidate for Owner acceptance;
no push, PR, merge, tag or Release operation is part of this phase.

## Accepted input and scope

ICON-2 implementation commit: `7dc253259f62b136ef58d1bd0655da06247ec0c7`.
The Owner accepted its design/recognizability and Explorer details/small/large
icons at actual 100% and 200% DPI. ICON-2 automated Release and Debug CTest each
passed 38 tests. Neither those checks nor prior version capture/Clipboard
acceptance establish acceptance of the new v0.1.2 ZIP.

The candidate keeps Windows x64 Portable, Release /MD, external VC++ Runtime,
Brotli 1.2.1, libjxl 0.12.0 and all other existing package versions. The only
product version change after ICON-2 is 0.1.2; icon artwork and runtime behavior
are retained. The SVG/ICO keep their adopted hashes from the asset README.

## Independent contract

`cmake/icon-v012/source-inputs-v1.json` binds product/test sources, headers,
icon/SVG/RC/manifest inputs and build configuration/admission code. Its SHA-256
and the clean immutable build commit/tree identify the candidate build.
`cmake/IconRelease.cmake` admits only `icon-v012`, MSVC x64, Ninja single-config,
Release, Shipping Privacy v2, project 0.1.2 and a new run under `build/icon-phase3/`.
The historical `icon-local` scope remains for development and is not release admission.

An original unchanged dependency checkout must be explicitly supplied. The
historical SEC-BR2 read-only validators check that original checkout's SEC-BR1
package identity, installed marker/receipt and managed derivative; they do not
admit the new product sources under an old contract. Old S1/BR1/BR2 ledgers,
bindings, markers, receipts, package profiles, v0.1.0/v0.1.1 tags and artifacts
are retained unchanged. No dependency installation/download/update occurs.

The new run has distinct `build/` (CMake/CTest), `product/` (one fresh EXE and
17 validated DLLs), and `candidate/` (payload, ZIP and independently expanded
tree) directories. Existing run/candidate paths are refused and failed evidence
is preserved. All compiler temporaries and logs stay in the newly owned run.

## Rebuild and package

From the product build source commit in a clean checkout, set the original
dependency checkout in your local environment and choose a fresh run:

```powershell
pwsh -NoProfile -File tools/build_icon_v012.ps1 -Mode PlanOnly
pwsh -NoProfile -File tools/build_icon_v012.ps1 -Mode Build -DependencyRoot $env:CLIPSS2_ICON_DEPENDENCY_ROOT -RunRoot build/icon-phase3/rc1
```

The build imports the installed x64 MSVC environment, validates source and
dependencies, configures/builds, runs CTest, checks source/dependencies again,
stages only the product PE set and writes `BUILD-IDENTITY.json`. It verifies
Release /MD compile flags, x64 PE/imports, Debug CRT absence, tested private path
patterns, 0.1.2 VERSIONINFO, retained manifest and all seven icon resources.
System/runtime imports remain external, not bundled. The existing Owner/MSVC
14.44.35207/SDK 10.0.26100.0 /MD license judgments are inherited within their
established scope; publication approval is not inherited.

After successful build, create the new profile from those actual bytes:

```powershell
pwsh -NoProfile -File tools/package_icon_v012.ps1 -Mode CreateProfile -DependencyRoot $env:CLIPSS2_ICON_DEPENDENCY_ROOT -RunRoot build/icon-phase3/rc1
pwsh -NoProfile -File tools/package_icon_v012.ps1 -Mode CreateCandidate -DependencyRoot $env:CLIPSS2_ICON_DEPENDENCY_ROOT
pwsh -NoProfile -File tools/package_icon_v012.ps1 -Mode ValidateCandidate -DependencyRoot $env:CLIPSS2_ICON_DEPENDENCY_ROOT
```

The generated profile is a new `packaging/public/profile-icon-v012-rc1.json`,
not a modification of an old profile. It contains exact paths, sizes and hashes
for 47 files: 18 PE, 23 original third-party notices and six documents/marker.
It records the product build commit and binds package templates/code independently
of later documentation commits. No source SVG, development PNG, tests, PDB, user
settings, captures or internal evidence enters the Portable ZIP.

Only the pure canonical ZIP/tree primitives from SecBR2Package are reused;
the old profile reader, approval flags and creation gates are never invoked to
admit this candidate. ZIP structure, names, duplicates, traversal, symlinks,
metadata, exact decompressed lengths/hashes and the independently extracted tree
are checked. A separate Source ZIP must be taken from the final packaging/docs
commit and checked against that commit's Git blobs; it is not the build revision.

## Notices and current documentation

`packaging/icon-v012/` contains Portable README, complete NOTICE and the Microsoft
component-specific terms. All 23 original third-party license/patent bytes remain
the same. The original project icon follows the existing MIT LICENSE; Node/Edge
and Windows tools used for generation/build are not redistributed. Microsoft
terms differ from v0.1.1 only in the product version header, with the approved
body retained. Neither anonymous download nor execution is treated as explicit
agreement by itself. Future publication must place the same terms before download
and beside the EXE; a real immutable permalink is added only after authorized push.

The Japanese/English README now identify published v0.1.1 and unpublished v0.1.2.
Historical unpublished/PENDING records retain their original meaning and bytes.
Current acceptance evidence and candidate ZIP hashes belong in the new validation
record; they do not update historical contracts.

## Manual acceptance

Use the newly expanded v0.1.2 Portable ZIP, not the ICON-2 executable. Record PASS,
FAIL or SKIP and the tested Windows/DPI/theme environment for each item:

- [ ] Explorer details/small/large icons at actual 100% and 200% DPI.
- [ ] Standalone Settings and Settings opened from the resident.
- [ ] Taskbar and Alt+Tab visibility, icon and switching behavior.
- [ ] Notification-area icon/menu and clean exit, including recovery if tested.
- [ ] Window Capture of owned content and saving the image.
- [ ] JPEG XL lossless and lossy save/reopen.
- [ ] Clipboard write and paste into another application.
- [ ] PNG/JPEG/WebP/AVIF quick regression.
- [ ] Settings read/save compatibility, with user data preserved separately.
- [ ] Light/dark themes, monitor transitions, Explorer restart and another PC if available.

Automated codec/unit tests or synthetic TaskbarCreated messages do not complete
these real operations. No system icon-cache deletion, global DPI/theme change,
old ZIP overwrite or release permission is implicit in candidate creation.
