# clipss2 — S1 source release

Windows向けのスクリーンキャプチャツールです。Window、単一モニター内のRegion、Monitorを選び、PNG / JPEG / WebP / AVIF / JPEG XLへ保存する実装を含みます。File / Clipboard / Both、設定保存、Portable markerの実装も含みます。

初回配布は **source-only** です。EXE/DLL、ZIP、installer、署名済み製品やBinary Releaseは含みません。
公開支援構成は **Shipping Privacy v2 / MSVC x64 / Ninja single-config / Release**。
通常OFFとHistorical Shipping v1の再現契約・旧バイナリ・PRIVATE監査証拠は対象外です。

[構築・検証状況](BUILDING.md)、[操作の説明](docs/USAGE.md)、[OS/APIと未検証範囲](docs/SUPPORT.md)を参照してください。
S1で実測済みなのはsource contract、read-only PlanOnly、限定source transformationとNOTICEの構造検査です。
自動Installは未完成で、本source cutの依存Install入口は未実装です。実configure/build・ABI・runtime・独立再ビルドのbitwise再現性は未実証です。
過去の別構成・旧f3の動作実績を新S1の成功には読み替えません。

自作部分は[MIT原文](LICENSE)。第三者成分を一律MITとは扱いません。
[S1 NOTICE](THIRD-PARTY-NOTICES-S1.md)、[原LICENSE/PATENTS一覧](licenses/SOURCES.md)、[S1 Reader](docs/SOURCE-S1-NOTICES.md)でsource-directとreference-onlyを区別します。
schema2のsource承認宣言は、別途所有者が明示承認した特定のsource集合と、後で対応付けるGit tree/commitのsnapshotにだけ適用します。binaryは非承認、binaryProfilesは空です。
公開ReaderのPASSやdeclaredSourceReleaseApproved=trueだけでは所有者の実承認・掲載許可を証明しません。掲載は別のPRIVATE admissionと具体的公開許可を得た同一snapshotに限定します。

通常開発の正本はGitHub guardiancrow/clipss2のmainで、ローカルも同じGit履歴を使います。PR/Issueは同じrepositoryで扱い、旧Internalは移行完了後にPRIVATEアーカイブへ保全します。
通常のPRごとに正式release承認の再取得、source-developmentへの強制遷移、NOTICE固定SHAの更新を要求しません。
NOTICEや第三者コードの変更には該当する原条件・帰属・固定連鎖の検証を行い、新しい正式release snapshotを指定するときに状態と全source identityを再固定します。
将来のHEADに過去snapshotの承認を自動継承しません。詳細は[NOTICEガイド](docs/SOURCE-S1-NOTICES.md)を参照してください。
