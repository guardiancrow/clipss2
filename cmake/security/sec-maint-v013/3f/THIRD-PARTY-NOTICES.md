# clipss2 0.1.3 SEC-MAINT-3F の第三者ソフトウェア通知

libwebp と SharpYUV は、上流 v1.6.0（`4fa21912338357f89e4fd51cf2368325b59e9bd9`）と既存の公式 vcpkg port を使用する。公式 port の tree は `c2ed152baa0795e889f73476fd0bc8e35dafd40b`。4 件の公式パッチと Microsoft vcpkg の MIT 帰属を引き継ぐ。 [libwebp COPYING と PATENTS](../../../../licenses/libwebp/copyright) [vcpkg の MIT ライセンス](../../../../licenses/vcpkg/LICENSE.txt)

ローカル port-version は `#4`。上流の `27e559f5b05d0ed1678ad57fe23f3b667405d11c` を基に `src/utils/bit_writer_utils.c` の残容量判定を 1 行変更し、C の未定義動作を予防する。上流版は 1.6.0 のまま。将来の採用版または公式 port で同等の修正を確認した時点で、この backport を廃止する。 [上流修正](https://github.com/webmproject/libwebp/commit/27e559f5b05d0ed1678ad57fe23f3b667405d11c)

AOM、libyuv、TurboJPEG、共有 zlib の通知を引き継ぐ。 [3E の通知](../3e/THIRD-PARTY-NOTICES.md)
