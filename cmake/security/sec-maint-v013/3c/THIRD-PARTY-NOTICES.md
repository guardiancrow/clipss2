# clipss2 0.1.3 SEC-MAINT-3C の第三者ソフトウェア通知

共有 zlib は v1.4（`99623ef7097352da7993ba614161fd6a311e046e`）を使用する。`libz.dll` は libpng が読み込む。この版の LICENSE は収録済みの [zlib ライセンス原文](../../../../licenses/zlib/copyright) とバイト単位で一致する。[zlib v1.4](https://github.com/madler/zlib/releases/tag/v1.4)

zlib overlay は Microsoft vcpkg の固定 port（`8235a7aece54307d71f711944d6339dd6dccd50c`、tree `318707972085eef7ea54f533c69040b05e3bf6b6`）を使用する。`0001-no-version-script-on-ohos.patch` を含め、port ファイルのバイト列を維持した。[固定 port](https://github.com/microsoft/vcpkg/tree/8235a7aece54307d71f711944d6339dd6dccd50c/ports/zlib) [vcpkg の MIT ライセンス](../../../../licenses/vcpkg/LICENSE.txt)

libjpeg-turbo 3.2.0 の `turbojpeg.dll` には、同版のソースに収録された zlib 1.3.2（Zlib）と libspng 0.7.4（BSD-2-Clause、libpng-2.0）が静的に組み込まれている。共有 zlib と内蔵成分それぞれの由来は source inputs と SBOM に記録する。[TurboJPEG のライセンス通知](../../../../licenses/libjpeg-turbo/copyright)

libpng 1.6.59 の原文と帰属、その他の依存、Microsoft の利用・再頒布条件は 3B の記録を継承する。[3B の通知](../3b/THIRD-PARTY-NOTICES.md)
