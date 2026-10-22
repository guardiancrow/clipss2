# S1 skcms rights review

確認日: 2026-10-22。対象はShipping Privacy v2 / scope s1-v1 / MSVC x64のみ。

## 固定identityと原文

- Upstream: https://skia.googlesource.com/skcms
- revision: `96d9171c94b937a1b5f0293de7309ac16311b722`
- [固定LICENSE](https://skia.googlesource.com/skcms/+/96d9171c94b937a1b5f0293de7309ac16311b722/LICENSE)
- 保全原文: [licenses/skcms/LICENSE](../licenses/skcms/LICENSE)、1635 bytes、SHA-256 `E59BB5C5C6BA426A9AC4BA9FE667AD14C5166B12AA25BE8AF1D122B14FBE2E36`
- 原文のGit blob: `6c7c5be360194ef93331dfb2b0c01114ad9c263f`

HTTPで取得した原文のraw bytesは、中立originの1481 tuple中の`third_party/skcms/LICENSE`と一致した。
origin checkout/gitlink、v2 specのpin、managed manifest、取得script、CMake内のpinも同じrevisionである。依存checkoutの取得や実コンパイルを行った結果ではない。

## 実装上のコンパイル範囲

managed jpegliは`031a0077f5799a6041004267fc12b956c1f52a20`。
[third_party/skcms.cmake](https://github.com/google/jpegli/blob/031a0077f5799a6041004267fc12b956c1f52a20/third_party/skcms.cmake)の`target_link_skcms`と、`lib/jpegli_cms.cmake`を固定revisionで確認した。
`JPEGLI_ENABLE_SKCMS=ON`、`BUILD_SHARED_LIBS=OFF`のS1では、`jpegli_cms`を構築するときのsourceとして次の2 translation unitが定義される。

- `skcms.cc`
- `src/skcms_TransformBaseline.cc`

AVX2/AVX512を追加する条件はClang向けであり、MSVCでは`src/skcms_TransformHsw.cc`と`src/skcms_TransformSkx.cc`は追加されず、`SKCMS_DISABLE_HSW`と`SKCMS_DISABLE_SKX`が設定される。
ローカルの`clipss2_jpeg_codec`は`jpegli-static`をリンクする。固定`lib/jpegli.cmake`の`JPEGLI_INTERNAL_LIBS`はhwy、Threads::Threads、ATOMICS_LIBRARIESであり、`jpegli_cms`を含まない。`lib/jpegli_lists.cmake`の`JPEGLI_INTERNAL_JPEGLI_SOURCES`にもskcms/CMS sourceは含まれない。
managed subdirectoryはEXCLUDE_FROM_ALLで追加されるため、現在の`--target clipss2`の明示依存ではskcmsのtranslation unitはコンパイル対象にならない。`JPEGLI_ENABLE_SKCMS=ON`だけを根拠に最終EXEへskcmsが組み込まれるとは扱わない。`jpegli_cms`を別途構築・配布する場合には上記2件が対象になる。これは固定CMake/source一覧の静的照合であり、実buildのcompile_commands/link mapやbinary実測ではない。

確認したbuild定義のraw SHA-256も既存origin tupleと一致する。

| managed jpegli相対path | SHA-256 |
| --- | --- |
| third_party/CMakeLists.txt | F2246BF6B1E1F688E99C159030E771F25C46E302DD501EACDF94181653821597 |
| third_party/skcms.cmake | 5785529509296E833E0E5B0CB5720FBE25A325BBE3EBDE557B53F7B0C5E0A3DC |
| lib/jpegli_cms.cmake | 0C406B5F9BD850430A974CA05D2EABD0A4A1B7ECE75102F42BCABACCE597F30D |
| lib/jpegli.cmake | E2A56FC0E0F05BCF8AC54BC25675528F8944436994112724545CFEF1342608B8 |
| lib/jpegli_lists.cmake | FFD2A937DEE70046F265FF9F0259110C95D1BCEF21891462341D75C0C3E8DA33 |

2 translation unitと、そのlocal include閉包（下表のheader）をHTTPで限定確認した。各raw SHA-256は既存origin tupleと一致し、いずれもroot LICENSEを指すBSD-style表示である。v2変換はこれらを変更しない。

| skcms相対path | bytes | SHA-256 |
| --- | ---: | --- |
| skcms.cc | 117750 | C4B69D55184AF6A7689005B58C3FDC68181D5C32895D4E7030351408D3193C1C |
| src/skcms_TransformBaseline.cc | 1434 | 8F1F0977C03AD208D11ECD3ED300C07D591596C98AFB23F04512B430BF90E78E |
| skcms.h | 207 | F3431FE230076DDA4DCAD5C3B1CC4789EF6D71967F254C6FDAC32A3B97BB94DE |
| src/skcms_public.h | 20691 | AA5F042D104287D47268D6F1EFC5800C5D0497B6680150744729F379EB5B5CE3 |
| src/skcms_internals.h | 6333 | E0636AD492F32D6AB67788CF4711B048763EA876C87140B30DFC25A19692DCB2 |
| src/skcms_Transform.h | 4450 | 06E60773F6DD0354F7200B256C9C960F1F20DF0EEC3EB7BEE0757F2A44C6D0C8 |
| src/Transform_inl.h | 55737 | F20790DEBFDCC51B60FA4E35CD56F92E364D4F5C167D89538F00C57E9EF8F91F |

root/LICENSEと対象fileの権利表示から、上記の取得・変更・コンパイル・条件付きsource/binary再頒布にはBSD-3-Clauseを適用する。確認範囲で追加のライセンス例外・別条件は検出していない。
`skcms.h`にはGoogle Inc. 2023、BaselineとTransform headerにはGoogle LLC 2018のcopyrightもあるため、source再頒布時は各fileの表示も保持する。
profiles、fuzz、bench、test-only program、Bazel/Ninja用tool、Clang専用TUの製品利用・再頒布を今回一括承認しない。別revisionやcompiler、対象集合が変われば影響範囲を再評価する。

## Packaging条件と判断の境界

source再頒布ではcopyright、3条件、免責全文と各fileの権利表示を保持する。
skcmsを含むbinaryの再頒布では、Google Inc. 2018のcopyright、3条件、免責全文を配布物に付随する文書または資料に再掲する。該当するBinary Packagingは`licenses/skcms/LICENSE`を実配布ZIP等へ同梱し、Binary NOTICEから対応付けること。短いライセンス名やURLだけで代用しない。最終binaryへの包含有無は後続の実build受入で確認する。
Google Inc.およびcontributorの名前を、事前の書面許可なく製品の推奨・宣伝に使用しない。
固定jpegliの`third_party/CMakeLists.txt`はconfigure時に`LICENSE.skcms`をbuild treeへコピーするが、これは配布ZIPへの同梱完了を意味しない。今回Packagingは未実施。

この範囲のskcmsの`licenseExpression`/`selectedLicense`は`BSD-3-Clause`、`selectionStatus`は`declared`とする。reference-onlyはsource集合にskcms実装を同梱しない分類であり、未審査の意味ではない。
旧v2 catalog/anchor/inputは元snapshotの記録としてbytesを保持する。現在のNOTICEはschema2の新しいv3 source-development集合へ再固定し、source/binary release承認、Owner証明、署名認証を付与しない。
MS-001のRuntime非同梱方針、MS-002の再頒布未解決、MS-003の次回ビルド前確認を、このskcms判断によって解決済みにしない。他ライブラリの判断も変更しない。
