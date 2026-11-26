# clipss2 0.1.3 — Third-Party Notices

この通知は Windows x64 Portable 版の第三者成分に適用される権利情報です。構築元と入力の結合は BUILD-IDENTITY.json と SBOM.cdx.json に記録します。
自作部分のMITと各成分の権利を分離し、原文の条件・免責・特許条項を保持します。本NOTICE自体は追加の許諾やMicrosoft・upstreamの承認を与えるものではありません。

clipss2自作部分: Copyright (c) 2026 guardiancrow, [MIT原文](LICENSE)。
青い角丸プレートと四隅のキャプチャフレームのアプリアイコンもclipss2独自制作のMIT対象素材です。
外部の画像・アイコン・ロゴ・フォント輪郭は取り込んでいません。開発用SVG/PNGや再生成ツールは本Portable ZIPに同梱しません。
以下の日本語説明や短いlicense名は原文に代わるものではありません。原文のcopyright・条件・免責・特許条項を保持します。

This software is based in part on the work of the Independent JPEG Group.

| 成分 | version / 利用 | 対象PE | 原文 |
| --- | --- | --- | --- |
| AOM | 3.15.3; aggregate copyright / patent terms | aom.dll | [copyright](licenses/aom/copyright) |
| Brotli | 1.2.1; MIT | brotlicommon.dll, brotlidec.dll, brotlienc.dll | [copyright](licenses/brotli/copyright) |
| C++/WinRT | 2.0.250303.1; MIT、header組込み | clipss2.exe | [LICENSE](licenses/cppwinrt/LICENSE.txt) |
| managed jpegli | 031a0077f5799a6041004267fc12b956c1f52a20; BSD-3-Clause、static組込み | clipss2.exe | [LICENSE](licenses/jpegli/LICENSE), [PATENTS](licenses/jpegli/PATENTS) |
| managed libjpeg headers | 8ecba3647edb6dd940463fedf38ca33a8e2a73d1; IJG/aggregate、header利用 | clipss2.exe | [LICENSE.md](licenses/jpegli-headers/LICENSE.md), [README.ijg](licenses/jpegli-headers/README.ijg) |
| managed Highway | 1.3.0 / 271a9a0ed9de1232d9117f1572c3fe28f8542ec1; static組込み | clipss2.exe | [dual-license原文](licenses/highway-managed/LICENSE), [Apache-2.0](licenses/apache-2.0/LICENSE.txt) |
| Highway | 1.4.0; shared | hwy.dll | [dual-license原文](licenses/highway-vcpkg/copyright), [Apache-2.0](licenses/apache-2.0/LICENSE.txt) |
| Little CMS | 2.19.1#1; MIT | lcms2-2.dll | [copyright](licenses/lcms/copyright) |
| libavif | 1.4.2#1; aggregate copyright / patent terms | avif.dll | [copyright](licenses/libavif/copyright) |
| libjpeg-turbo | 3.2.1#1; IJG / Modified BSD aggregate | jpeg62.dll, turbojpeg.dll | [copyright](licenses/libjpeg-turbo/copyright), [README.ijg](licenses/libjpeg-turbo/README.ijg) |
| libjxl | 0.13.0; BSD-3-Clause | jxl.dll, jxl_cms.dll, jxl_threads.dll | [copyright](licenses/libjxl/copyright), [PATENTS](licenses/libjxl/PATENTS) |
| libpng | 1.6.59#1; libpng-2.0 | libpng16.dll | [copyright](licenses/libpng/copyright) |
| libwebp | 1.6.0#4、上流 backport; BSD-3-Clause、PATENTS全文をcopyright内に保持 | libwebp.dll, libsharpyuv.dll | [copyright/PATENTS](licenses/libwebp/copyright) |
| libyuv | 1943、MSVC 互換修正; BSD-3-Clause | libyuv.dll | [copyright](licenses/libyuv/copyright), [PATENTS](licenses/libyuv/PATENTS) |
| zlib | 1.4; Zlib | libz.dll | [copyright](licenses/zlib/copyright) |
| libspng | 0.7.4#1; BSD-2-Clause と libpng 由来の通知 | spng.dll | [LICENSE](licenses/libspng/LICENSE), [libpng 由来の原文](licenses/libspng/LICENSE.libpng) |
| Microsoft STL headers | 使用MSVC 14.44.35207のheader表示はApache-2.0 WITH LLVM-exception | C++ header生成コードの保守的なcoverage | [Apache/LLVM exception全文](licenses/msvc-stl/LICENSE.txt) |
| skcms | 96d9171c94b937a1b5f0293de7309ac16311b722; BSD-3-Clause | 参考coverageのみ。今回の製品targetにmanaged skcms objectなし、libjxl側SKCMS=OFF | [LICENSE全文](licenses/skcms/LICENSE) |

Highwayは両versionともApache-2.0を選択し、dual-license原文を省略しません。`hwy/contrib/random/random-inl.h`のCC0-1.0例外も保持しています。
Shipping Privacy v2ではjpegli/managed Highwayとvcpkg Highway/libjxlの診断file識別子を変更しています。libjxl overlayには依存・MSVC対応と変更告知patchがあります。これらはclipss2側の変更であり、upstreamによる推奨を意味しません。
skcms原文の同梱は保守的な権利情報coverageです。コンパイルされていないmanaged CMS targetを製品へ含めた証明ではありません。`jxl_cms.dll`は今回LCMS2を利用します。

TurboJPEG の PNG loader は共有 spng.dll を使い、libpng16.dll と spng.dll が共有 libz.dll を参照します。libjpeg-turbo の元ソースにある内蔵 libspng と zlib は製品へリンクしません。managed JPEGli、managed Highway とヘッダー由来のコードは上表に含みます。overlay の由来は [SOURCES](SOURCES.md) を参照してください。

## Microsoft由来部分と外部Runtime

使用環境はMSVC 14.44.35207/compiler 19.44.35230.0、Windows SDK 10.0.26100.0、Release /MDです。動的CRT利用時にもCRT起動・終了コード等は製品へ組み込まれます。Windows APIへのimport libraryと、組込みコード・定数は区別します。

自作部分のMITやSTLのOSS条件を、Microsoftの非STL CRT/SDK全体へ拡張しません。STL原文はMicrosoft STL vs-2022-17.14 tagが指すcommit `1f6e5b16ec02216665624c1e762f3732605cf2b4`のLICENSE.txtです。取得原文のCRLFだけをLFへ統一し、本文は保持しています。installed header全件の同一性証明ではありません。

Microsoft VC++ Runtime x64は外部前提です。Runtime DLL・installer・Windows標準DLL・開発用.libは同梱しません。導入方法はREADMEのMicrosoft公式案内を参照してください。外部Runtimeの利用条件を、製品内の全Microsoftコードへの一括許諾として扱いません。

Microsoft由来部分について本NOTICEにより独自の再許諾を行うものではありません。第三者の権利・適用条件を自作部分のMITで置き換えません。

Microsoft SDK由来の対象コードに関する条件は、同梱の[MICROSOFT-COMPONENT-TERMS.txt](MICROSOFT-COMPONENT-TERMS.txt)を参照してください。対象範囲は同文書に限定され、自作部分のMITおよび独立した第三者成分のライセンスを置き換えません。
