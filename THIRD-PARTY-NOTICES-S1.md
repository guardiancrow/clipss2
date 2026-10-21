# S1 Third-Party NOTICE — source-only

S1 source-release契約用です。source-onlyの特定snapshotを対象とし、Binary NOTICEや製品配布の承認ではありません。構造検査だけで実所有者承認や法的な全面保証を与えません。
自作部分は[MIT原文](LICENSE)。第三者由来の実source15件は[帰属表](cmake/privacy/ATTRIBUTION.md)と[独立Reader](docs/SOURCE-S1-NOTICES.md)へ対応付けます。
原LICENSE/PATENTSは[原文一覧](licenses/SOURCES.md)に保持し、この日本語説明で原文掲載に代えません。

source-directは実同梱のpatch/port/設定等の著作物の対応です。reference-onlyは将来取得・リンクするコードやtoolの説明で、実装ソースやbinaryの同梱を示しません。
元のcomponent revisionは帰属の由来であり、現在のGit commitへの承認やbuild成功を表しません。
HighwayはApache-2.0を選択しますが、元dual-license全文とCC0-1.0例外（hwy/contrib/random/random-inl.h）を保持します。
libjxl/jpegli/libyuvのPATENTS、AOMのcopyright内のPatent License等も原文のまま保持します。
skcms/Microsoftのreference-only pendingは保持します。実同梱条件・最終source identity・所有者承認は、特定snapshotの外部Owner記録と照合する対象です。source承認宣言だけでBinary条件や公開操作許可が完了するわけではありません。
旧L2/v1をscopeから除いても、その歴史的権利問題を解消したとは記録しません。
| Component | Version | 配布上の区分 | License selection | 状態 |
| --- | --- | --- | --- | --- |
| aom | 3.15.0 | reference-only | LicenseRef-aom-aggregate | declared |
| brotli | 1.2.0 | reference-only | MIT | declared |
| cppwinrt | 2.0.250303.1 | reference-only | MIT | declared |
| jpegli | 031a0077f5799a6041004267fc12b956c1f52a20 | reference-only | BSD-3-Clause | declared |
| jpegli-headers | 8ecba3647edb6dd940463fedf38ca33a8e2a73d1 | reference-only | LicenseRef-libjpeg-headers-aggregate | declared |
| lcms | 2.19.1#1 | reference-only | MIT | declared |
| libavif | 1.4.2#1 | reference-only | LicenseRef-libavif-aggregate | declared |
| libjpeg-turbo | 3.2.0 | reference-only | LicenseRef-libjpeg-turbo-aggregate | declared |
| libjxl-root | 0.12.0 | source-direct | BSD-3-Clause | declared |
| libpng | 1.6.58 | reference-only | libpng-2.0 | declared |
| libwebp | 1.6.0#3 | reference-only | BSD-3-Clause | declared |
| libyuv | 1916 | reference-only | BSD-3-Clause | declared |
| zlib | 1.3.2#2 | reference-only | Zlib | declared |
| vcpkg-port | baseline | source-direct | MIT | declared |
| libjxl-cmake | 0.12.0 | source-direct | Apache-2.0 | declared |
| clipss2 | bf7e23256c3b88f6f4e7d9867d11e45f043042b0 | source-direct | MIT | declared |
| highway-source | 1.4.0 | source-direct | Apache-2.0 | declared |
| libyuv-source | 1916 | source-direct | BSD-3-Clause | declared |
| highway-v2-reference | 1.4.0 | reference-only | Apache-2.0 | declared |
| highway-managed-v2-reference | 1.3.0 / 271a9a0ed9de1232d9117f1572c3fe28f8542ec1 | reference-only | Apache-2.0 | declared |
| skcms-reference | 96d9171c94b937a1b5f0293de7309ac16311b722 | reference-only | LicenseRef-skcms-unreviewed | pending |
| microsoft-build-runtime-reference | exact terms/version mapping remains pending | reference-only | LicenseRef-Microsoft-pending | pending |
| vcpkg-tool-reference | 04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4 | reference-only | MIT | declared |
