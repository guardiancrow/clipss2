# Phase SEC-BR1 — Brotli 1.2.1 local build

SEC-BR1 addresses [CVE-2026-111497](https://github.com/google/brotli/releases/tag/v1.2.1)
by selecting Brotli 1.2.1 in a separate build contract. Publication of v0.1.1,
push, tags, and Release changes are outside this phase.

## Dependency selection and preservation

The historical S1 input ledger fixes Brotli 1.2.0 at vcpkg baseline
`04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4`, port tree
`0fb79bb13588702a9e06057745b4aed95ce5c861`, upstream `google/brotli@v1.2.0`.
Its 28 raw input records, ledger SHA-256
`80BD9B28EF236F29E31BC12B1D6C16120C9F7E60AC0ACF4F861691C6DF5C64AB`,
presets, baseline, overlay closure, receipts and publication identities remain
historical evidence. Adding a port to that overlay would invalidate its closed
file set. Editing the original manifest/config/presets would also break its raw
hash contract.

[SEC-BR1 inputs](../cmake/security/sec-br1/source-inputs-v1.json) instead use:

- input ID `clipss2-source-inputs-sec-br1-v1`, scope `sec-br1`;
- the same builtin baseline, target triplet, host triplet and manifest bytes;
- a dedicated manifest directory, with the new Brotli overlay before the
  unchanged Shipping Privacy v2 overlay;
- official Brotli 1.2.1 port tree
  [`0702c3d1505dae90ff22bcbdc68921c74bb8651f`](https://api.github.com/repos/microsoft/vcpkg/git/trees/0702c3d1505dae90ff22bcbdc68921c74bb8651f);
- upstream tag `v1.2.1`, revision
  `db24a98aed38a16de78cc05ca15cdd8dc2a0d92d`, and the official port's archive
  SHA-512, plus SHA-256/size records for the actual new input files;
- independent output roots under `build/sec-br1-r1/` and distinct receipt/marker
  names. Neither old installed bytes nor old success markers admit this contract.

All five imported port files retain their upstream Git blob identities.
`install.patch`, `pkgconfig.patch`, and `usage` are byte-identical to the old
port; only version/source selection changes. Its host dependencies remain
`vcpkg-cmake` and `vcpkg-cmake-config`, already present in the pinned closure.
No other package version or port tree changes.

libjxl remains 0.12.0 with the unchanged v2 patches, system Brotli selection and
dynamic linkage. Its port does not constrain Brotli to 1.2.0. Upstream's
[1.2.0-to-1.2.1 diff](https://github.com/google/brotli/compare/v1.2.0...v1.2.1)
changes decoder block-switch error handling and version identifiers, with an
additional error code in a previously reserved slot. This supports source/API
compatibility; actual build and runtime tests are separate evidence.

## Reachability and residual uncertainty

`src/codec/jxl/jxl_codec.cpp` encodes canonical captured BGRA8 as a naked
codestream with structured sRGB, without JPEG reconstruction or container boxes.
The lossless and lossy validators both call `decode_and_inspect`, which checks
`JXL_SIG_CODESTREAM` before creating/feeding a libjxl decoder. It subscribes to
basic-info, color, frame and full-image events; it does not request box decoding
or JPEG reconstruction.

In libjxl 0.12.0, the identified Brotli decoder consumers are
[compressed box content](https://github.com/libjxl/libjxl/blob/v0.12.0/lib/jxl/box_content_decoder.cc)
and [JPEG reconstruction metadata](https://github.com/libjxl/libjxl/blob/v0.12.0/lib/jxl/jpeg/dec_jpeg_data.cc).
The [container dispatcher](https://github.com/libjxl/libjxl/blob/v0.12.0/lib/jxl/decode.cc)
uses these for requested `brob` box decompression and `jbrd` reconstruction.
Container signatures are rejected by the product before this dispatcher.

On this inspected path, ordinary screenshot encoding and its round-trip
validation do not establish attacker-controlled Brotli decoding. This is a
source-level finding, not a comprehensive exploitability proof. Decoder DLLs
are nevertheless distributed in v0.1.0 and contain the affected code. A caller
of Brotli APIs, future container/JPEG-import features, alternate entry points,
or a defect in an input guard could change reachability. No CVE exploit PoC,
fuzz campaign, or formal call-graph proof is claimed. Existing v0.1.0 downloads
remain affected components and are not repaired by a local rebuild.

## Local procedure

From the repository root in an MSVC x64 developer shell:

```powershell
pwsh -NoProfile -File tools/build_shipping_dependencies_sec_br1.ps1 -Mode PlanOnly
pwsh -NoProfile -File tools/prepare_jpegli.ps1
pwsh -NoProfile -File tools/build_shipping_dependencies_sec_br1.ps1 -Mode Derive
pwsh -NoProfile -File tools/build_shipping_dependencies_sec_br1.ps1 -Mode Install -BuildToolsEligibilityConfirmed
pwsh -NoProfile -File tools/build_shipping_dependencies_sec_br1.ps1 -Mode ValidateInstalled
pwsh -NoProfile -File tools/build_shipping_dependencies_sec_br1.ps1 -Mode Configure
cmake --build build/sec-br1-r1/product
pwsh -NoProfile -File tools/build_shipping_dependencies_sec_br1.ps1 -Mode ValidateProduct
ctest --test-dir build/sec-br1-r1/product -R '^(brotli_sec_br1|libjxl_smoke|jxl_codec|png_codec|webp_codec|avif_codec|jpegli_backend|jpeg_backend_policy|capture_image_output)$' --output-on-failure
```

Source acquisition is explicit. Use `prepare_jpegli.ps1` only with absent roots
or already verified fixed checkouts; do not import old installed/build caches.
`Derive` accepts the original 1481 tuples and unchanged v2 transformations,
writing only a new `build/sec-br1-r1/managed-jpegli/`.
Install refuses even empty preexisting vcpkg/dependency/installed/product roots.
Configure likewise refuses an existing product build root. Failure preserves
partial roots and logs; neither mode implements automatic cleanup or resume.

The original S1 preset remains a historical 1.2.0 entry point. SEC-BR1 uses its
explicit script instead of changing the fixed preset. Configure selects the
new scope; the old S1 CMake path remains available and independently checked.
Existing S1 fixtures remain tests of that historical contract.

Install isolates downloads, registries, temporary files and build roots and
clears inherited cache/overlay overrides. ValidateInstalled checks 16 exact
package versions/triplets, package lists and payloads, 19 installed DLL names,
the full installed inventory, distinct receipt and marker, and Brotli license
bytes. These self-recorded receipts check consistency, not signed provenance,
general ABI compatibility, or release approval.

The installed set contains `libwebpdecoder.dll` and `libwebpdemux.dll` from the
unchanged WebP port. They are not linked by the product. ValidateProduct checks
the separate 17-DLL deployed set and compares every DLL to the admitted installed
bytes; do not copy all installed DLLs into a future package.

The three Brotli DLLs are built by the official port with the unchanged dynamic
triplet and installed in `build/sec-br1-r1/installed/x64-windows-shipping-privacy/bin`.
The vcpkg CMake integration copies linked runtime DLLs beside the product/test
executables. The new runtime test checks both 1.2.1 version APIs, an encoder /
decoder round trip and invalid-header rejection. It is not a CVE regression PoC.

Offline contract fixtures, without downloading or compiling:

```powershell
pwsh -NoProfile -File tests/sec_br1_contract_test.ps1 -EvidenceRoot "$PWD/build/sec-br1-tests"
pwsh -NoProfile -File tests/s1_dependency_install_test.ps1 -EvidenceRoot "$PWD/build/sec-br1-legacy-tests"
```

The new [notice delta](../THIRD-PARTY-NOTICES-SEC-BR1.md) retains Brotli's complete
MIT original; 1.2.1 uses identical license bytes. Other license/patent texts and
Owner decisions are preserved. Microsoft toolchain decisions are inherited
from `clipss2-g2-inheritance-2026-10-23` for the same owner, MSVC 14.44.35207,
SDK 10.0.26100.0, Release /MD and external Runtime policy. No new qualification
review or publication approval follows from this phase.

BUILDING and older packaging/readiness documents contain earlier publication
status statements and fixed 1.2.0 references. They describe their recorded
snapshots; SEC-BR1 does not rewrite those approvals or hashes. The new entry
point and notice identify the changed local build explicitly. A future release
needs new source/binary/package identities, complete notices, privacy checks,
and Owner approval.
