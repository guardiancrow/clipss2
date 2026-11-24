# clipss2 0.1.3 SEC-MAINT-3B の第三者ソフトウェア通知

libpng v1.6.59（`cd952f49f95bb27154ae77dbb103032d95f6e580`、libpng-2.0）を使用する。この版の LICENSE は、収録済みの [libpng ライセンス原文](../../../../licenses/libpng/copyright) とバイト単位で一致する。上流の著作権表示を保持する。[libpng v1.6.59](https://github.com/pnggroup/libpng/releases/tag/v1.6.59)

libpng の overlay は Microsoft vcpkg の固定 port（`8235a7aece54307d71f711944d6339dd6dccd50c`、tree `129ad6200060f2e0cce22d36b954d7dff9dc4f8a`）に由来する。CMake 設定を調整する `cmake.patch` と、genout のコンパイラーターゲットを伝える `genout-compiler-target.patch` を適用する。[固定 port](https://github.com/microsoft/vcpkg/tree/8235a7aece54307d71f711944d6339dd6dccd50c/ports/libpng) [Microsoft vcpkg の MIT ライセンス](../../../../licenses/vcpkg/LICENSE.txt)

他の依存と既存の Microsoft 利用・再頒布条件は 3A の記録を継承する。[3A の通知](../THIRD-PARTY-NOTICES.md)
