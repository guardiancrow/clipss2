# S1 source-release NOTICE契約とsnapshot scope

NOTICE契約はschema2です。source-developmentとsource-release-approvedの宣言を区別し、どちらもbinaryReleaseApproved=false、binaryProfiles=[]です。
現行Readerはv3 source-developmentを使用します。固定skcmsのBSD-3-Clause確認だけを反映した新世代で、sourceReleaseApproved=false、approvalReferenceId=nullです。旧v2 catalog/anchor/inputは元snapshotの記録としてbytesを保持し、fallbackや現在の承認として使用しません。
正式宣言のopenRightsは空、resolvedRightsはs1-included-source-rights-review / s1-final-source-identity / s1-publication-owner-approvalの既知3件です。
developmentはopenRightsに同じ3件、resolvedRights空、approvalReferenceId=nullを持ちます。
正式宣言のapprovalReferenceIdは非個人的なopaque IDであり、所有者の証明や秘密情報ではありません。
[NOTICE](../THIRD-PARTY-NOTICES-S1.md)、[帰属表](../cmake/privacy/ATTRIBUTION.md)、[原文一覧](../licenses/SOURCES.md)を併読してください。

Readerはstrict helperだけをimportし、Legacy L2/v1または旧candidate Readerへfallbackしません。
reviewed Reader code → 固定v3 catalog SHA → v3 anchor size/SHA → direct15/raw45のidentityを検査します。
source-direct6 / reference-only17、原文23件、Highwayのdual-license全文/Apache選択/CC0例外、PATENTSを保持します。
source-inputs28と中立origin/v2 spec/manifestの連鎖は不変更です。
追加したraw-44は固定skcmsの原LICENSE、raw-45は[限定確認記録](SKCMS-LICENSE-REVIEW.md)です。他componentの権利判断は不変更です。skcmsのreference-onlyは実装をsource集合へ同梱しない区分であり、pendingではありません。

一般の構造検査:

```powershell
pwsh -NoProfile -File tools/validate_s1_source_notices.ps1 -ValidateSourceState
```

正式宣言tupleも要求する検査（現在のdevelopment状態はSourceReleaseRequiredで拒否）:

```powershell
pwsh -NoProfile -File tools/validate_s1_source_notices.ps1 -ValidateSourceRelease
```

Git metadataや生成物を含まないclean exportの全path/directory閉集合検査には、選択したmodeへ-PublicationViewを追加します。

```powershell
pwsh -NoProfile -File tools/validate_s1_source_notices.ps1 -ValidateSourceRelease -PublicationView
```

PublicationViewは.git/build/cache/余剰file/空directory/reparseを拒否するため、通常のGit作業repositoryでそのまま成功する機能ではありません。
旧-ValidateCandidateとRead-S1NoticeCandidateはCandidateContractUnsupportedで拒否します。

返却値declaredSourceReleaseApprovedはmetadataの宣言だけです。旧sourceReleaseApproved返却fieldは提供しません。
ownerApprovalEvidenceVerified、publicationPermissionVerified、wholeScopeBytesAuthenticated、productBuild、binaryAbiRuntimeVerifiedはfalseです。
NOTICE60件のhashと295 pathの閉集合を、全295件の内容認証や所有者認証と混同しません。code/catalog同時改変への署名認証も提供しません。

正式承認はPRIVATEのRightsAcceptance / SourceApprovalに結合された特定の全source集合Sに適用し、その後GitAdmissionでtree/commitへ対応付け、PublicationApprovalで公開先repository ID/ref/具体的操作を確認します。
これらの承認や全source Sを公開metadata自身へ埋めず、自己参照hashを作りません。公開ReaderはPRIVATE記録を要求せず、実承認は別の信頼済みOwner判断と全source照合で判定します。
同じsourceが1 byteでも変われば、過去の承認は新checkout全体の承認にはなりません。Reader直接hash対象外のC++やREADME等が変わっても同様です。

通常のcode/PR/Issue処理と新しい正式release snapshotの承認は別です。通常PRごとの承認再取得・強制development遷移・無関係な固定SHA更新は不要です。
NOTICE/第三者コードを変更する場合は必要な原条件・帰属・固定連鎖の検証を行います。新しい正式snapshotの準備ではdevelopment状態を明示的に選択でき、最終予定bytesを再固定し、新Sの所有者承認を外部記録します。
過去metadataは元snapshotの記録として残せますが、それが将来HEAD全体の承認を保証するとは表示しません。

今回の正規更新は、旧v2を上書きせず、新v3 catalog/anchor/inputを作成し、development tupleを選択する経路です。原文追加とskcms policy差分を確認し、変更されたNOTICE文書のraw identityとPublicationViewのpath集合を更新した後、anchor、catalog、Reader内catalog SHAの順に固定します。Readerが実行時にhashを更新する機能や、WriterによるOwner承認の自動発行はありません。
旧15 direct・旧22原文・固定28入力のbytesを保全し、他component policyの差分がないことと新連鎖を限定テストします。正式なsource-release-approvedへの昇格は、新Sの別のOwner判断が得られた場合だけ行います。
[BUILDING](../BUILDING.md)の実install/build/runtime未実証は、NOTICE構造検査の受入によって解消しません。
