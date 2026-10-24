[日本語](README.md) | [English](README.en.md)

# clipss2 — v0.1.0

clipss2 is a screen capture tool for Windows. **[v0.1.0 is available on GitHub Releases](https://github.com/guardiancrow/clipss2/releases/tag/v0.1.0)** as a Windows x64 Portable ZIP.

## Download and setup

Before downloading, using, or redistributing the binary package, review the conditions on the Release page, the [Microsoft component-specific conditions](packaging/binary/MICROSOFT-COMPONENT-TERMS-v4.txt), and the [Portable README](packaging/binary/README-portable-s1-release-v3.md). See the [Binary NOTICE](packaging/binary/THIRD-PARTY-NOTICES-S1-BINARY-RELEASE-v3.md) for attribution and license notices.

The distributor requires recipients and redistributors of the Covered SDK Code, as defined in the conditions document, to agree to its component-specific conditions before using or redistributing that code. These conditions preserve the separate MIT license for clipss2-authored code and the licenses of independent third-party components. Providing the document, requesting agreement, and establishing legally effective agreement are distinct matters. Downloading, extracting, or running the package alone does not establish explicit agreement. This English README does not establish the legal effectiveness of the conditions or guarantee any recipient's agreement.

- The package is Windows x64 Portable, built in Release mode with `/MD`, with no installer. It uses D3D11 and Windows.Graphics.Capture.
- **Microsoft Visual C++ v14 Runtime x64 must be installed separately.** If needed, install it using [Microsoft's official guidance](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist). Runtime DLLs, a Runtime installer, and Windows system DLLs are not bundled; the application does not install the Runtime automatically.
- **The binaries are unsigned.** Windows or security software may warn that the publisher is unverified. SHA-256 verifies content identity; it is not a substitute for code signing or publisher authentication.

1. Download `clipss2-0.1.0-windows-x64-portable.zip` from the Release page and extract the entire ZIP into a new, writable folder.
2. Keep `clipss2.portable` and all 17 app-local DLLs beside `clipss2.exe`.
3. Run `clipss2.exe --help` to view the CLI options. Starting without arguments runs the application in the system tray; `--settings` opens the settings UI.

Portable settings are stored in `settings.conf` beside the executable, and the default image output folder is `captures`. The distributed ZIP contains no user settings or captured images.

| Published ZIP | Details |
| --- | --- |
| Filename | `clipss2-0.1.0-windows-x64-portable.zip` |
| Size | 7,968,423 bytes |
| Contents | 46 files, including one EXE, 17 DLLs, the portable marker, the product LICENSE, third-party notices and required LICENSE/PATENTS files, and the Microsoft component-specific conditions |
| SHA-256 | `3803495B4798C6B7D6B30BC0A0E08495FC275DB4E0A576A78CCD60BEBBF0FECC` |

To check the downloaded ZIP in PowerShell, run the following command and compare the result with the SHA-256 above:

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath ./clipss2-0.1.0-windows-x64-portable.zip
```

## Implemented features and verified scope

The following features are implemented:

- Window capture, Region capture within a single monitor, and Monitor capture.
- Saving as PNG, JPEG, WebP, AVIF, or JPEG XL.
- File, Clipboard, and Both output modes; saved settings; and a portable marker.

These are implementation capabilities, not a claim that every feature has been verified on an actual Windows PC.

R2 acceptance testing on an actual Windows PC was limited to Windows 11 25H2 x64 build 26200.9711, the SDR/fallback path, and Runtime 14.51.36247.0. It covered Window Capture of an owned window (HWND), saving a 772×546 PNG using File output, normal exit, and related limited checks. The limited regression suite recorded 12 passes and no failures. These observed OS and Runtime versions are not established minimum requirements.

A user reported successful extraction, startup, capture, and saving on another PC with an earlier R3 candidate containing the same 18 PE binaries (one EXE and 17 DLLs). The OS and Runtime versions, capture mode, image format, and hash verification on that PC were not recorded. This was not verification of the complete published ZIP on another PC.

Acceptance testing has not covered the full tray, settings, and selection GUIs; actual Clipboard writes; Region or Monitor capture; all codecs and profiles; Windows 10; HDR/WCG; or all GPU, DPI, and driver combinations. HDR/WCG and regions spanning multiple monitors are not guaranteed. The limited R2 behavior, Privacy, and dependency checks do not guarantee all environments or features, general ABI compatibility, or bitwise reproducibility of independent rebuilds. Results from other earlier configurations or the old f3 configuration do not establish success for the new S1 configuration.

See also the [implemented usage specification (from an earlier source snapshot)](docs/USAGE.md), [OS/API boundaries and unverified environments](docs/SUPPORT.md), and [portable operation and release-preparation records](docs/PORTABLE-RELEASE.md). These linked documents are in Japanese.

## v0.1.0 publication status and source identity

Formal publication and post-publication verification were completed in Phase 3-H. The SDK-2 status is based on the Owner's adopted interpretation of the agreement and verification that the requirements for this release's covered code, distribution conditions, and presentation of those conditions were implemented. The distributor's continuing obligations and the limitations concerning agreement through anonymous downloads and redistribution remain in effect.

| Item | Status |
| --- | --- |
| Source publication | COMPLETE |
| SDK-2 contractual assessment | OWNER APPROVED |
| SDK-2 implementation of conditions | IMPLEMENTED AND VERIFIED |
| SDK-2 | CLEAR (within the scope of this release) |
| Binary Release G3 | OWNER APPROVED / PUBLISHED / VERIFIED |

The product build source revision is `93545d458da33cef397fb77458336ea06339dcb6`. The source commit targeted by tag `v0.1.0` is `a250e4b7ee9082e326e6d77eeb91716e7433b722`. The tag also includes subsequent documentation and packaging preparation. Documentation and packaging commits must not be presented as revisions used to rebuild the product.

GitHub's automatically generated Source archives contain the 343-file source tree at the tag. They are separate from the Binary ZIP used to run the application and do not include the accepted EXE and DLLs as a binary distribution. README updates after publication are ordinary documentation updates; they do not change the fixed tag or ZIP, rebuild the product, or reissue v0.1.0.

## Licenses and development documentation

clipss2-authored code is governed by the [original MIT License](LICENSE), including its warranty disclaimer and limitation of liability. The MIT license does not apply to all Microsoft and third-party components as a whole; those components retain their respective licenses.

The Microsoft component-specific conditions apply only to the Covered SDK Code defined in that document: Microsoft-owned object code actually incorporated from the reviewed Windows SDK Distributable Code inputs. They do not extend to independently licensed clipss2-authored code or third-party components merely because those components are distributed together. The document is a project-authored notice, not a Microsoft endorsement or individual permission from Microsoft. Refer to the original conditions for their full scope, restrictions, and warranty and liability provisions; this README does not replace them or create a new license translation.

The [S1 NOTICE](THIRD-PARTY-NOTICES-S1.md), [index of original LICENSE/PATENTS files](licenses/SOURCES.md), and [S1 Reader guide](docs/SOURCE-S1-NOTICES.md) distinguish `source-direct` from `reference-only` entries. For the BSD-3-Clause review of the pinned skcms revision, see the [review scope and packaging conditions](docs/SKCMS-LICENSE-REVIEW.md).

The supported public build configuration is **Shipping Privacy v2 / MSVC x64 / Ninja single-config / Release**. The normal OFF configuration, the Historical Shipping v1 reproducibility contract, old binaries, and PRIVATE audit evidence are outside its scope. See the [build and verification status and G2 handoff](BUILDING.md) and [Release Notes / release-body template](docs/RELEASE-NOTES-0.1.0-V4.md). Statements in release-preparation documents that publication was pending, approval had not been granted, or conditions had not yet been implemented describe the state at that time. The current v0.1.0 publication status is given in this README and on the Release page.

## Development history and fixed contracts

The initial S1 publication was a **source-only** snapshot containing no product binaries. It was followed by R1 dependency builds and the Release build, R2 limited technical acceptance, and R4-D acceptance of a private 45-file candidate ZIP. Phase 3-D then accepted the 46-file v4 candidate with the Microsoft component-specific conditions. That candidate became the published ZIP. The R4-D 45-file ZIP, the v3 candidate, and their fixed contracts are preserved as historical records.

The old schema2 source-approval declaration applies only to the specific source set and Git tree/commit snapshot explicitly approved by the Owner. Its lack of binary approval, empty `binaryProfiles`, and the Source Reader's approval flags describe the scope of the earlier source contract. They do not mean that the v0.1.0 Binary Release, which separately received Owner approval and passed post-publication verification, remains unapproved.

The Reader's v3 `source-development` generation incorporates the license review of the pinned skcms revision. Old v2 approval metadata, a public Reader PASS, or `declaredSourceReleaseApproved=true` alone does not establish actual Owner approval or permission to publish a different snapshot. The final v0.1.0 source tree and history were published with separate Owner approval; approval was not automatically inherited from an earlier snapshot.

The [cmake/privacy/README.md](cmake/privacy/README.md), whose raw hash is fixed, remains a historical document for the earlier source snapshot. Its statements that work had not been performed describe that earlier state. The old PublicationView's 295 files, the second-stage HEAD's 323 files selected for the tag archive, and the v0.1.0 tag's 343 files are distinct sets.

The canonical branch for regular development is `main` in `guardiancrow/clipss2` on GitHub, and local development uses the same Git history. PRs and issues belong in that repository. The old Internal repository is to be preserved as a PRIVATE archive after migration is complete. Ordinary PRs do not require renewed formal release approval, a forced transition to `source-development`, or updates to fixed NOTICE SHA values.

Changes to NOTICE files or third-party code require verification of the applicable original conditions, attribution, and fixed hash chains. When a new formal release snapshot is designated, its status and complete source identity must be fixed again. Approval of an earlier snapshot does not automatically extend to a future HEAD. See the [NOTICE guide](docs/SOURCE-S1-NOTICES.md) for details.
