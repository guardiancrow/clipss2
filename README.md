[日本語](README.md) | [English](README.en.md)

# clipss2

Windows のスクリーンキャプチャツールです。公開済みの Windows x64 Portable 版は [v0.1.2](https://github.com/guardiancrow/clipss2/releases/tag/v0.1.2) です。このソースの v0.1.3 はセキュリティメンテナンス候補で、Brotli 1.2.1 を維持し、承認済みの codec と共有 zlib を使用します。 [v0.1.3 リリースノート](docs/RELEASE-NOTES-0.1.3.md)

## 導入と更新

ZIP 全体を新しい書込み可能なフォルダーへ展開してください。v0.1.3 候補では clipss2.exe、DLL 18 本、clipss2.portable を同じ場所に置きます。clipss2.exe --help で使い方を確認できます。通常起動は常駐、--settings は設定画面です。

外部の Microsoft Visual C++ Redistributable x64 が必要です。更新前に旧版の常駐・設定画面を終了し、設定と画像を保全してください。新旧の Portable 版を同時に使う場合は別フォルダーへ展開し、設定先を分けてください。 [Runtime の公式案内](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)、[動作環境](docs/SUPPORT.md)、[Portable 運用](docs/PORTABLE-RELEASE.md)

公開ソースからの構築は [v0.1.3 構築手順](docs/BUILD-V013.md) を参照してください。利用・再配布時は、同梱の [第三者ソフトウェア通知](packaging/v013/THIRD-PARTY-NOTICES.md) と [Microsoft 対象コード限定条件](packaging/v013/MICROSOFT-COMPONENT-TERMS.txt) を確認してください。

## 実装機能と確認済み範囲

以下は実装されている機能です。

- Window、単一モニター内のRegion、Monitorの選択。
- PNG / JPEG / WebP / AVIF / JPEG XLへの保存。
- File / Clipboard / Both出力、設定保存、Portable marker。

旧v0.1.0 R2の実機受入は、Windows 11 25H2 x64 build 26200.9711、SDR/fallback経路、Runtime 14.51.36247.0での所有HWNDのWindow Capture、772×546 PNGのFile保存、正常終了等に限定されます。限定Regressionは12件成功、失敗0件です。この実測値を最低必要OS/Runtime版とは断定しません。

同じ18 PEを含む以前のR3候補について、別PCでの展開・起動・キャプチャ・保存成功がユーザーから報告されています。OS/Runtime版、capture mode、保存形式、現地hash照合は未記録で、公開ZIP全体の別PC検証ではありません。

上記の各versionで報告された確認項目以外、Region/Monitor、全codec/profileの組合せ、Windows 10、HDR/WCG、全GPU/DPI/driver等を一律に受入済みとは扱いません。過去の限定した動作・Privacy・依存関係の確認は、全環境・全機能、一般的なABI互換性、独立再ビルドのbitwise再現性の保証ではありません。

[操作の実装仕様（旧Source時点の文書）](docs/USAGE.md)、[OS/APIと未検証範囲](docs/SUPPORT.md)、[Portable運用・公開準備の記録](docs/PORTABLE-RELEASE.md)も参照してください。

## v0.1.0の公開状態とソース識別

Phase 3-Hで正式公開と公開後検証が完了しました。SDK-2の状態は、Ownerが採用した契約解釈と、今回の対象コード・配布条件・提示導線の履行確認に基づきます。配布主体の継続義務や、匿名取得・再配布時の合意成立に関する適用限界は維持します。

| 項目 | 状態 |
| --- | --- |
| Source公開 | COMPLETE |
| SDK-2契約判断 | OWNER APPROVED |
| SDK-2条件履行 | IMPLEMENTED AND VERIFIED |
| SDK-2 | CLEAR（今回の対象範囲） |
| Binary Release G3 | OWNER APPROVED / PUBLISHED / VERIFIED |

製品build source revisionは`93545d458da33cef397fb77458336ea06339dcb6`、tag `v0.1.0`の対象commitは`a250e4b7ee9082e326e6d77eeb91716e7433b722`です。tagには後続の文書・Packaging準備も含みます。文書・Packaging commitを製品の再ビルドrevisionとして表示しません。

GitHub自動生成のSource archiveはtag時点の343ファイルのSource treeであり、実行用のBinary ZIPとは別です。公開後のREADME更新はこの固定tagやZIPの変更・再リリースを意味しません。

## ライセンスと開発資料

自作部分は[MIT原文](LICENSE)に従います。Microsoft・第三者成分を一律MITとは扱いません。[S1 NOTICE](THIRD-PARTY-NOTICES-S1.md)、[原LICENSE/PATENTS一覧](licenses/SOURCES.md)、[S1 Reader](docs/SOURCE-S1-NOTICES.md)ではsource-directとreference-onlyを区別します。固定skcmsのBSD-3-Clause確認は[確認範囲とPackaging条件](docs/SKCMS-LICENSE-REVIEW.md)を参照してください。

公開支援構成は **Shipping Privacy v2 / MSVC x64 / Ninja single-config / Release**です。通常OFFとHistorical Shipping v1の再現契約・旧バイナリ・PRIVATE監査証拠は対象外です。[構築・検証状況とG2引継ぎ](BUILDING.md)、[Release Notes／本文テンプレート](docs/RELEASE-NOTES-0.1.0-V4.md)を参照してください。公開準備時点の資料に残る未公開・未承認・履行待ちの記述は当時の状態であり、現在のv0.1.0の公開状態は本READMEとReleaseページに記載したとおりです。

## 開発・公開履歴と固定契約

最初のS1公開は **source-only** で、製品バイナリを含まないsnapshotでした。その後、R1の実依存構築・Release build、R2の限定技術受入、R4-Dの45ファイルの非公開候補ZIP受入を経て、Phase 3-DでMicrosoft対象コード限定条件を含む46ファイルのv4候補を受け入れました。これがv0.1.0公開ZIPであり、旧候補・固定契約は当時の記録として保全します。

旧schema2のsource承認宣言は、Ownerが明示承認した特定のsource集合とGit tree/commitのsnapshotにだけ適用します。同宣言のbinary非承認・空のbinaryProfilesや、Source Readerの承認フラグは旧Source契約の範囲を表し、別途Owner承認・公開後検証を完了したv0.1.0 Binary Releaseの未承認を意味しません。

Readerのv3 source-developmentは固定skcmsの権利確認を反映する検証世代です。旧v2承認metadataや公開ReaderのPASS、declaredSourceReleaseApproved=trueだけを、別snapshotのOwner実承認・公開許可には読み替えません。v0.1.0の最終Source treeと履歴は別途Owner承認を得て公開済みであり、旧snapshotの承認を自動継承したものではありません。

raw hash固定された[cmake/privacy/README.md](cmake/privacy/README.md)は旧Source snapshotの歴史的文書として維持します。同文書の「未実施」は当時の状態です。旧PublicationViewの295ファイル、第2段階HEADのtag archive対象323ファイル、v0.1.0 tagの343ファイルはそれぞれ別の集合です。

通常開発の正本はGitHub guardiancrow/clipss2のmainで、ローカルも同じGit履歴を使います。PR/Issueは同じrepositoryで扱い、旧Internalは移行完了後にPRIVATEアーカイブへ保全します。通常のPRごとに正式release承認の再取得、source-developmentへの強制遷移、NOTICE固定SHAの更新を要求しません。

NOTICEや第三者コードの変更には該当する原条件・帰属・固定連鎖の検証を行い、新しい正式release snapshotを指定するときに状態と全source identityを再固定します。将来のHEADに過去snapshotの承認を自動継承しません。詳細は[NOTICEガイド](docs/SOURCE-S1-NOTICES.md)を参照してください。
