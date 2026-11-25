# clipss2 0.1.3 SEC-MAINT-3D の第三者ソフトウェア通知

libjpeg-turbo 3.2.1（`7b4993786d856cbccabfc0ed2f923ba000e326cc`）を使用する。上流の LICENSE.md と README.ijg は収録済み原文とバイト単位で一致する。JPEG API、TurboJPEG API、SIMD コードには、上流通知に従って IJG、BSD、Zlib の各条件を適用する。[LICENSE.md](../../../../licenses/libjpeg-turbo/copyright) [README.ijg](../../../../licenses/libjpeg-turbo/README.ijg)

libjpeg-turbo の overlay は Microsoft vcpkg の固定 port（`8235a7aece54307d71f711944d6339dd6dccd50c`、tree `c6ed1937a4a4ca36b7b1d98e5da68c63d4fa8dfa`）に由来する。公式の CMake processor 回避パッチと doc/header オプションのパッチを変更せず移植した。[固定 port](https://github.com/microsoft/vcpkg/tree/8235a7aece54307d71f711944d6339dd6dccd50c/ports/libjpeg-turbo) [vcpkg の MIT ライセンス](../../../../licenses/vcpkg/LICENSE.txt)

TurboJPEG の PNG loader は外部 libspng 0.7.4（`fb768002d4288590083a476af628e51c3f1d47cd`）を使用する。固定 baseline の公式 port と 2 件のビルド用パッチを継承した。BSD-2-Clause の原文と、libpng 由来の箇所に付されたライセンス・著作権表示を収録する。[libspng LICENSE](../../../../licenses/libspng/LICENSE) [libpng 由来の原文](../../../../licenses/libspng/LICENSE.libpng)

製品では libspng と libpng が同じ共有 zlib 1.4 を読み込む。libjpeg-turbo のアーカイブに収録された内蔵 zlib と libspng はソースとして残る。製品へのリンク対象は 3D の SBOM と Release リンク証跡に記録する。Managed JPEGli と既存 Microsoft 利用・再頒布条件を継承する。[共有 zlib の通知](../3c/THIRD-PARTY-NOTICES.md)
