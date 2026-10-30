# SEC-BR1 third-party notice delta

This document describes the local SEC-BR1 build contract. It is not a release
approval or a replacement for the notices accompanying v0.1.0.

| Component | SEC-BR1 version | Use | License and attribution |
| --- | --- | --- | --- |
| Brotli | 1.2.1 | brotlicommon.dll, brotlidec.dll, brotlienc.dll | MIT; [complete copyright and license](licenses/brotli/copyright) |
| vcpkg Brotli port | tree `0702c3d1505dae90ff22bcbdc68921c74bb8651f` | unmodified official port definition, install.patch, pkgconfig.patch and usage | [vcpkg MIT license](licenses/vcpkg/LICENSE.txt) |

Brotli Authors' copyright, permission notice and warranty disclaimer are retained
in full. The [upstream 1.2.1 LICENSE](https://github.com/google/brotli/blob/db24a98aed38a16de78cc05ca15cdd8dc2a0d92d/LICENSE)
has the same 1084 bytes and SHA-256
`3D180008E36922A4E8DAEC11C34C7AF264FED5962D07924AEA928C38E8663C94`
as the preserved license file. No license text or attribution is removed.

The other 15 vcpkg packages, managed jpegli source pins, Shipping Privacy v2
patches, and license choices are unchanged. Their original licenses and patent
notices remain in [licenses/SOURCES.md](licenses/SOURCES.md) and
[the S1 source notice](THIRD-PARTY-NOTICES-S1.md).
Those documents retain their historical version and approval references.
Microsoft components remain subject to their separate conditions; this update
does not extend the project's MIT license to them.

Before any new binary publication, prepare and validate a complete new package
notice and source/binary identities. Do not use the old v0.1.0 package profiles
or their approvals for SEC-BR1 binaries.
