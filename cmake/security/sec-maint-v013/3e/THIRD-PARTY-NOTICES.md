# clipss2 0.1.3 SEC-MAINT-3E の第三者ソフトウェア通知

libaom 3.15.3（`b141d9c45db236335351c8a4b2a14b3ade27c937`）を使用する。配布用の原文には、libaom の LICENSE と PATENTS、および fastfeat、vector、x86inc のライセンスを収録する。 [libaom 原文](../../../../licenses/aom/copyright)

AOM の overlay は Microsoft vcpkg の公式 port（`8235a7aece54307d71f711944d6339dd6dccd50c`、tree `ab139302320191444fc272f1c186dba45b26b42c`）に由来する。公式の `aom-rename-static.diff` を引き継ぎ、取得済み固定アーカイブの SHA-512 を照合する処理を追加した。 [公式 port](https://github.com/microsoft/vcpkg/tree/8235a7aece54307d71f711944d6339dd6dccd50c/ports/aom) [vcpkg の MIT ライセンス](../../../../licenses/vcpkg/LICENSE.txt)

libyuv 1943（`ef08f21f6d94addd69e5ae521d0f75f14cb644ed`）を使用する。Shipping Privacy v2 の overlay と `cmake.diff` を移植し、`source/row_win.cc` に MSVC 互換の上流修正 `826ab02fcc1af3f45041cef574b318b659802f5d` を適用した。 [libyuv LICENSE](../../../../licenses/libyuv/copyright) [libyuv PATENTS](../../../../licenses/libyuv/PATENTS) [上流の変更](https://chromium.googlesource.com/libyuv/libyuv/+/826ab02fcc1af3f45041cef574b318b659802f5d)

libavif 1.4.2#1 は、これらの共有ライブラリを使用する構成で再構築する。従来の TurboJPEG、外部 libspng、共有 zlib の原文と通知を引き継ぐ。 [3D の通知](../3d/THIRD-PARTY-NOTICES.md)
