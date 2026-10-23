# clipss2 S1 Binary NOTICE — Release向け草案

対象はsource revision `93545d458da33cef397fb77458336ea06339dcb6`のR2受入済み18 PEです。
GitHub Releases向けPortable ZIPという配布方式は承認済みですが、**MS-002-Lと下流条件は未確定、最終Binary Releaseは未承認**です。
このNOTICEは権利情報の表示であり、公開許可や受領者の同意成立を証明しません。旧Source/R3 private profileの承認・hashを引き継いで上書きするものではありません。

clipss2自作部分: Copyright (c) 2026 guardiancrow, [MIT原文](LICENSE)。
以下の日本語説明や短いlicense名は原文に代わるものではありません。原文のcopyright・条件・免責・特許条項を保持します。

This software is based in part on the work of the Independent JPEG Group.

| 成分 | version / 利用 | 対象PE | 原文 |
| --- | --- | --- | --- |
| aom | 3.15.0; aggregate copyright / patent terms | aom.dll | [copyright](licenses/aom/copyright) |
| Brotli | 1.2.0; MIT | brotlicommon.dll, brotlidec.dll, brotlienc.dll | [copyright](licenses/brotli/copyright) |
| C++/WinRT | 2.0.250303.1; MIT、header組込み | clipss2.exe | [LICENSE](licenses/cppwinrt/LICENSE.txt) |
| managed jpegli | 031a0077f5799a6041004267fc12b956c1f52a20; BSD-3-Clause、static組込み | clipss2.exe | [LICENSE](licenses/jpegli/LICENSE), [PATENTS](licenses/jpegli/PATENTS) |
| managed libjpeg headers | 8ecba3647edb6dd940463fedf38ca33a8e2a73d1; IJG/aggregate、header利用 | clipss2.exe | [LICENSE.md](licenses/jpegli-headers/LICENSE.md), [README.ijg](licenses/jpegli-headers/README.ijg) |
| managed Highway | 1.3.0 / 271a9a0ed9de1232d9117f1572c3fe28f8542ec1; static組込み | clipss2.exe | [dual-license原文](licenses/highway-managed/LICENSE), [Apache-2.0](licenses/apache-2.0/LICENSE.txt) |
| Highway | 1.4.0; shared | hwy.dll | [dual-license原文](licenses/highway-vcpkg/copyright), [Apache-2.0](licenses/apache-2.0/LICENSE.txt) |
| Little CMS | 2.19.1#1; MIT | lcms2-2.dll | [copyright](licenses/lcms/copyright) |
| libavif | 1.4.2#1; aggregate copyright / patent terms | avif.dll | [copyright](licenses/libavif/copyright) |
| libjpeg-turbo | 3.2.0; IJG / Modified BSD aggregate | jpeg62.dll, turbojpeg.dll | [copyright](licenses/libjpeg-turbo/copyright), [README.ijg](licenses/libjpeg-turbo/README.ijg) |
| libjxl | 0.12.0; BSD-3-Clause | jxl.dll, jxl_cms.dll, jxl_threads.dll | [copyright](licenses/libjxl/copyright), [PATENTS](licenses/libjxl/PATENTS) |
| libpng | 1.6.58; libpng-2.0 | libpng16.dll | [copyright](licenses/libpng/copyright) |
| libwebp | 1.6.0#3; BSD-3-Clause、PATENTS全文をcopyright内に保持 | libwebp.dll, libsharpyuv.dll | [copyright/PATENTS](licenses/libwebp/copyright) |
| libyuv | 1916; BSD-3-Clause | libyuv.dll | [copyright](licenses/libyuv/copyright), [PATENTS](licenses/libyuv/PATENTS) |
| zlib | 1.3.2#2; Zlib | z.dll | [copyright](licenses/zlib/copyright) |
| Microsoft STL headers | 使用MSVC 14.44.35207のheader表示はApache-2.0 WITH LLVM-exception | C++ header生成コードの保守的なcoverage | [Apache/LLVM exception全文](licenses/msvc-stl/LICENSE.txt) |
| skcms | 96d9171c94b937a1b5f0293de7309ac16311b722; BSD-3-Clause | 参考coverageのみ。今回の製品targetにmanaged skcms objectなし、libjxl側SKCMS=OFF | [LICENSE全文](licenses/skcms/LICENSE) |

Highwayは両versionともApache-2.0を選択し、dual-license原文を省略しません。`hwy/contrib/random/random-inl.h`のCC0-1.0例外も保持しています。
Shipping Privacy v2ではjpegli/managed Highwayとvcpkg Highway/libjxlの診断file識別子を変更しています。libjxl overlayには依存・MSVC対応と変更告知patchがあります。これらはclipss2側の変更であり、upstreamによる推奨を意味しません。
skcms原文の同梱は保守的な権利情報coverageです。コンパイルされていないmanaged CMS targetを製品へ含めた証明ではありません。`jxl_cms.dll`は今回LCMS2を利用します。

## Microsoftコードの区分と残件

- MS-001: Microsoft VC++ Runtime x64は外部導入です。Runtime DLL・installer・Windows標準DLL・開発用.libをZIPに含めません。
- MS-002-T: Release /MDでもCRT startup/terminationは静的に組み込まれる標準動作です。異常混入・違反の証拠ではなく、この事実だけで個別照会を必須としません。
- MS-002-L: MSVC Tools由来startup/補助コードと、適用契約下の再頒布許諾・下流条件の完全な対応は**NEEDS CLARIFICATION**です。標準リンクという事実、SDK指定、STLのOSS条件で許諾を代用しません。
- MS-002-SDK: Windows SDK 10.0.26100.0のum/x64・ucrt/x64の指定.libには、プログラムに組み込まれた形での配布規定があります。対象指定と、適用される下流保護・合意条件の履行を分けます。
- MS-003: 前回の本人申告と条項照合による使用資格判断を継承します。再頒布許諾の証明とは別です。

使用環境はMSVC 14.44.35207/compiler 19.44.35229.0、Windows SDK 10.0.26100.0です。
STL原文はMicrosoft STL vs-2022-17.14 tagが指すcommit `1f6e5b16ec02216665624c1e762f3732605cf2b4`のLICENSE.txtです。取得原文のCRLFだけをLFへ統一し、本文は保持しています。installed header全件との同一性証明ではなく、CRT起動コード・SDK全体へApache/LLVM exceptionを拡張しません。

clipss2自作部分のMITを、Microsoft・第三者コードの許諾へ一括して拡張しません。既存のskcms/Highway等の判断と原文を保持します。
独自EULA・クリック式同意画面は必要性が確認されない限り追加しない方針です。README/NOTICEの掲載やダウンロードだけで、Microsoftに関する下流条件への有効な同意を認定しません。条件の適用・履行方法は公開前の判断事項です。これは独自MITソース全体へ新しい制限を課すものではありません。

初回は未署名を基本案とし、最終ZIPのSHA-256を公開する方針です。hashや未署名配布方針は、Microsoftの許諾・署名・最終Owner公開承認の代わりになりません。
