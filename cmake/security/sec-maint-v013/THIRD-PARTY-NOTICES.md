# clipss2 0.1.3 SEC-MAINT-3A 候補の第三者ソフトウェア通知

この資料は 3A の依存変更を記録する。既存の依存と Microsoft 対象コードに関する条件は、v0.1.2 の完全 NOTICE と条件文書を参照する。候補の配布承認は別途行う。

## JPEG XL 0.13.0

JPEG XL Project の libjxl v0.13.0（commit `5a7da83447212fe4900fc29ce97430b881caa67e`）を使用する。上流の [LICENSE](../../../licenses/libjxl/copyright) と [PATENTS](../../../licenses/libjxl/PATENTS) は、それぞれ v0.13.0 の原文と同じ bytes の既存ファイルを参照する。個別ソースの著作権表示とライセンスを保持する。

- `third_party/CMakeLists.txt` は [Microsoft vcpkg の固定 port](https://github.com/microsoft/vcpkg/tree/8235a7aece54307d71f711944d6339dd6dccd50c/ports/libjxl) に由来する依存解決パッチを適用し、改変を表示する。[Microsoft vcpkg MIT 原文](../../../licenses/vcpkg/LICENSE.txt)
- `lib/jxl/base/status.h`、`lib/jxl/decode.cc`、`lib/jxl/encode.cc` の診断で、ビルド環境のファイルパスに代えて `libjxl` を表示する。Shipping Privacy v2 の改変として記録する。

動的な Brotli 1.2.1 は SEC-BR1 の既存 overlay（上流 google/brotli v1.2.1、MIT）を使う。Highway 1.4.0 と LCMS2 2.19.1#1 も既存構成を継承する。libjpeg-turbo 3.2.0 の TurboJPEG DLL に静的に入る zlib 1.3.2 と libspng 0.7.4 は、配布する `z.dll` や今後の外部 libspng 構成とは別の成分である。
