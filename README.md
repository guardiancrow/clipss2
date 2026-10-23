# clipss2 — v0.1.0 Release準備

Windows向けのスクリーンキャプチャツールです。Window、単一モニター内のRegion、Monitorを選び、PNG / JPEG / WebP / AVIF / JPEG XLへ保存する実装を含みます。File / Clipboard / Both、設定保存、Portable markerの実装も含みます。

最初のS1公開は **source-only** で、製品バイナリを含まないsnapshotでした。現在はv0.1.0のWindows x64 Portable ZIPを準備しています。R1の実依存構築・Release build、R2の限定技術受入、R4-Dの非公開候補ZIP受入は完了しています。**GitHub Binary Releaseは未公開・公開未承認**です。
候補はEXE 1本とapp-local DLL 17本、Portable marker、製品LICENSE、第三者NOTICE・必要なLICENSE/PATENTSの45ファイルです。Release /MD、Microsoft VC++ Runtime x64外部導入、未署名、インストーラなしの構成です。
利用条件・確認環境は[Portable README](packaging/binary/README-portable-s1-release-v2.md)、権利表示は[Binary NOTICE](packaging/binary/THIRD-PARTY-NOTICES-S1-BINARY-RELEASE-v2.md)、ファイル名・SHA-256は[Release Notes案](docs/RELEASE-NOTES-0.1.0.md)を参照してください。ダウンロード開始の案内ではありません。
公開支援構成は **Shipping Privacy v2 / MSVC x64 / Ninja single-config / Release**。
通常OFFとHistorical Shipping v1の再現契約・旧バイナリ・PRIVATE監査証拠は対象外です。

[構築・検証状況とG2引継ぎ](BUILDING.md)、[操作の実装仕様（旧Source時点の文書）](docs/USAGE.md)、[OS/APIと未検証範囲](docs/SUPPORT.md)を参照してください。
製品build source revisionは`93545d458da33cef397fb77458336ea06339dcb6`です。後続の文書・Packaging commitを製品の再ビルドrevisionとして表示しません。
R2の受入は限定した動作・Privacy・依存関係の確認です。全環境・全機能の動作、一般的なABI互換性、独立再ビルドのbitwise再現性の保証ではありません。今回の文書整備では再ビルドしません。
過去の別構成・旧f3の動作実績を新S1の成功には読み替えません。

自作部分は[MIT原文](LICENSE)。第三者成分を一律MITとは扱いません。
[S1 NOTICE](THIRD-PARTY-NOTICES-S1.md)、[原LICENSE/PATENTS一覧](licenses/SOURCES.md)、[S1 Reader](docs/SOURCE-S1-NOTICES.md)でsource-directとreference-onlyを区別します。
schema2のsource承認宣言は、別途所有者が明示承認した特定のsource集合と、後で対応付けるGit tree/commitのsnapshotにだけ適用します。binaryは非承認、binaryProfilesは空です。
現在のReaderは固定skcmsのBSD-3-Clause確認を反映したv3 source-developmentを検証します。旧v2承認metadataは元snapshotの記録として保持し、今回のsource集合へ承認を引き継ぎません。[確認範囲とPackaging条件](docs/SKCMS-LICENSE-REVIEW.md)を参照してください。
raw hash固定された[cmake/privacy/README.md](cmake/privacy/README.md)は旧Source snapshotの歴史的文書として維持します。同文書の「未実施」は当時の状態です。旧PublicationViewの295ファイルと、第2段階HEADのtag archive対象323ファイルは別の集合で、新集合へのOwner承認は未了です。今回の4文書更新も新snapshot全体の承認を引き継ぎません。
Microsoft SDKの下流条件文書・提示導線・履行方法は確定待ちです。R4-Dの45ファイルZIPは保全し、条件文書を追加する場合は新しいPackaging世代で扱います。
公開ReaderのPASSやdeclaredSourceReleaseApproved=trueだけでは所有者の実承認・掲載許可を証明しません。掲載は別のPRIVATE admissionと具体的公開許可を得た同一snapshotに限定します。

通常開発の正本はGitHub guardiancrow/clipss2のmainで、ローカルも同じGit履歴を使います。PR/Issueは同じrepositoryで扱い、旧Internalは移行完了後にPRIVATEアーカイブへ保全します。
通常のPRごとに正式release承認の再取得、source-developmentへの強制遷移、NOTICE固定SHAの更新を要求しません。
NOTICEや第三者コードの変更には該当する原条件・帰属・固定連鎖の検証を行い、新しい正式release snapshotを指定するときに状態と全source identityを再固定します。
将来のHEADに過去snapshotの承認を自動継承しません。詳細は[NOTICEガイド](docs/SOURCE-S1-NOTICES.md)を参照してください。
