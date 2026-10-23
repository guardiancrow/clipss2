# R4-D: 最終候補・Microsoft判断資料・公開ゲート

## 範囲と承認境界

OwnerのR4-D指示は、最終公開向け文書、新規profile・生成許可、新規隔離先での**非公開の最終候補生成・受入**を許可しています。GitHub公開、第三者配布、tag作成、署名、Git変更操作は許可していません。公開文書に内部の未承認bannerを付けないことと、公開を承認することは別です。

Packaging基点は`2009cfec9ae43c5af79579536301de1af7650030`、branchは`release/shipping-s1-v2`です。製品source revisionは`93545d458da33cef397fb77458336ea06339dcb6`です。既存18 PE、43件の非文書payload、旧Source・R3・R4-A・R4-B profile/anchor/auth/ZIP/receiptを不変更とします。

内部状態は`candidateGenerationAuthorized=true`、`binaryReleaseApproved=false`、`publicDistributionAllowed=false`、`redistributionGrantVerified=false`、`downstreamFulfillmentVerified=false`、`ownerResidualRiskAccepted=false`です。未署名は今回生成する成果物の状態として確定しますが、最終公開操作のOwner承認は未了です。

## 最終文書の構成

`README-portable-s1-release-v2.md`は版、必要環境、使い方、未署名、実測環境、限定的な別PC申告、MIT範囲を記載します。最低OS/Runtime版や全機能成功を作りません。`THIRD-PARTY-NOTICES-S1-BINARY-RELEASE-v2.md`は既存第三者対応表・原文・変更告知を保持し、Microsoft由来部分をMITへ一括包含しない説明に整理します。これらのpublic-facing文書に、法的許諾の確認済み・Microsoft承認済みという表明を追加しません。

未確定の解釈・下流条件・配布主体の責任・公開未承認は本書と固定profile/auth/receiptに保持します。公開文書の短縮により未解決事項をクローズするものではなく、文書だけで権利条件が満たされたと判定しません。独自EULA・クリック式同意を追加せず、追加の必要性も認定していません。

## MS-002の最終判断資料（既存証拠の継承）

Skill v1.1.0の移行再評価は既に完了しています。今回は同一資料の再監査、詳細member調査、Microsoft照会を行いません。T/L/PのIDは同Skill参照台帳のものです。

| 区分 | 確認済みの範囲 | 今回の結論 |
| --- | --- | --- |
| 技術事実 MS-002-T | 標準Release /MDでもstartup/terminationは静的に組み込まれる。既定入力とR2/R3の18 PE証拠を継承（T01/T02） | VERIFIED。異常混入・違反・照会必須の証拠ではない |
| SDKの対象指定 | installed SDK 10.0.26100.0契約と、SDK REDISTのum/x64・ucrt/x64の指定.lib対応（L02/P01） | CONDITIONAL。MSVC Toolsへ指定を流用しない |
| MSVCの個別許諾 MS-002-L | VS2022の適用契約/REDISTと、実際のstartup・補助コード全体への許諾対応が未確定（L03/L05） | NEEDS CLARIFICATION。禁止の認定でもLEGAL PASSでもない |
| OSSの一般的な配布実務 | Portable ZIP、原文/NOTICE分離、外部Runtime、未署名配布・hash提示というOwner承認済み方式 | 実務上の構成であり、Microsoftの個別許諾の代替根拠ではない |
| 配布主体の判断 | 解釈の採用、残存リスク、契約履行、公開操作の責任 | Owner判断未了。リスク受容で権利や条件を免除できない |

installed SDK原文はEULAID `WIN10SDK.RTM.AUG_2018_en-US`、raw SHA-256 `0F4A26AC9DC50066F8A1BFEAAF3F092D1B9E4791DF5487B9F3C31E7C3DC4D7F5`として既存確認を継承します。Community 2022 §4の対象コード配布・付加機能・下流保護条件、SDK §2(a)(i)の対象指定、§2(a)(ii)の配布条件、§2(a)(iii)の制限を区別します。[Community 2022](https://visualstudio.microsoft.com/license-terms/vs2022-ga-community/)、[SDK REDIST](https://learn.microsoft.com/en-us/legal/windows-sdk/redist)、[VS2022 REDIST](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution)、[Build Tools 2024-03](https://visualstudio.microsoft.com/license-terms/vs2022-ga-diagnosticbuildtools/)は前回確認済みの参照先であり、今回再取得した結果ではありません。

| 条件・論点 | 現行構成で対応済みの事項 | 残る不足・限界 |
| --- | --- | --- |
| 主要機能の追加 | キャプチャ・画像保存等の独自機能 | 特になし。これだけで許諾対象の指定を証明しない |
| .libの扱い | .libそのものを同梱せず、対象コードはEXE/DLLへリンク済み | SDK指定をMSVCの許諾へ拡張しない |
| copyright・帰属・商標 | 自作MIT、第三者原文/NOTICE、製品copyright、Microsoftの承認を示さない表示 | 原文表示と契約上の合意を同一視しない |
| 対象platform・Runtime | Windows x64、Microsoft Runtime外部導入 | 外部installerへの同意を組込みstartupコードの条件へ流用しない |
| 下流の同等保護・合意 | 権利区分の説明と原文参照を提示できる | 誰に何をいつどう提示し、どの条件への合意をどの根拠で充足とするかが未特定。MIT免責・NOTICE閲覧・downloadだけで充足したとは扱わない |
| SDKの継続義務・制限 | Windows向け構成、Microsoftコードを独自MITへ無差別に再許諾しない設計 | 配布主体の補償・輸出規制等の履行はNOTICEで代替できない。履行判断の記録が必要 |

下流条件は、適用されるMicrosoft既存条項を対象成分ごとに提示・参照する方法も検討対象ですが、その方法で必要な保護・合意が成立するという判断は未取得です。独自EULAやクリック画面が必須であるとの新たな根拠もありません。必要性が確認された場合だけ対応範囲を判断します。自作MITソース全体へ新しい制限を拡張しません。

今回新たな具体的不履行・明示的禁止を発見したという結論ではありません。一方、下流条件の充足も証明していません。十分な一次資料または適用版を特定した専門家評価等に基づく判断を記録する必要があります。Microsoftへの個別照会は唯一・必須の解決手段ではありません。従来の公開保留は継続します。

MS-001の非同梱方針、MS-003の本人申告と前回条項判断、STL/skcms/Highway等の既存判断は不変更です。

## Version・tag・Release Notes

現CMake `project(VERSION 0.1.0)`と既存buildを基準に、version **0.1.0**、tag **v0.1.0**、ZIP名 **clipss2-0.1.0-windows-x64-portable.zip**を提案します。製品・CMake・PEのversionを変更する操作ではありません。確認したローカルtagは空で、remote tag/Releaseの衝突は未確認です。tagの最終採用はOwner判断とします。

tagの対象は、本作業の文書・Packaging変更を別途レビュー・commitした**最終Packaging commit**を提案します。まだ存在するcommitとして記載しません。製品build revision `93545d4...`と区別し、受入receiptには今回のPackaging基点`2009cfe...`と実行した新writer/CLIのbytes/hashを記録します。

`docs/RELEASE-NOTES-0.1.0.md`はGitHub Release Notesの案です。ZIP生成後に実測size/SHA-256を記載します。NotesはZIP閉集合の外に置き、採用・掲載・公開は未承認です。外側の検証記録にNotesのhashを記録し、ZIP/profileへの自己参照hashを作りません。

## 新世代契約・生成・受入

`profile-s1-binary-release-v2.json`はR4-A v1を親とし、43件を同じidentityで継承し、README/NOTICEの2件だけを差し替えます。R4-B ZIP/receiptとR2/R3の固定証拠を結び付けます。独立`authorization-s1-r4d-final-private-v1.json`は現Packaging基点・新profileに対する非公開生成だけを許可します。旧profile/auth/Reader/writerの固定SHAやHEAD条件は変更しません。

新入口は`tools/package_s1_binary_final.ps1`です。R4-Bで検証済みのpath/CreateNew/ZIP検査primitiveを不変更で再利用し、新profile・生成許可・現HEADの検査は新世代で行います。新規workspace/build/r4-d-final-private-または隔離fixture用r4-d-final-tests-配下のみを使用します。既存rootは空でも拒否し、削除・上書き・resume・公開への自動昇格を行いません。

CreatePrivateCandidateの前にPlanOnly、生成後にValidateCandidateを使います。必要引数はRoot、R2EvidenceRoot、R3EvidenceRoot、R4BEvidenceRootです。出力はpayload・ZIP・新規expanded・owner・最後にCreateNewするacceptance receiptです。成功receiptがないpartialは保全し、受入しません。18 PE、43件の継承、45ファイル閉集合、文書参照、ZIP metadata/entry/hash、展開内容、owner/receiptを検証します。再ビルド、実キャプチャ、PE全解析は行いません。

## 最終Owner判断と公開Blocker

1. MS-002-Lの採用判断：対象コード・適用契約・許諾指定・根拠・未確定範囲。
2. 下流条件：対象者、対象成分、提示内容・時点・方法、必要な保護・合意を満たすという判断根拠。
3. 配布主体としての継続義務・残存リスクの判断。法的許諾の創設や契約条件の免除とはしない。
4. 公開README/NOTICE/Notes、確認環境・未確認範囲、未署名状態、version/tag/ZIP名の採用。
5. 最終Packaging commit・repository・Release本文・正確なasset size/SHA-256を特定した公開操作の許可。

公開Blockerは許諾対応と下流条件の判断記録不足、および具体的な最終成果物への公開許可未了です。通常のCRT startupの存在・最低OS版の未検証自体を新たな違反とはしません。本作業の非公開候補生成が成功してもLEGAL PASSや公開承認へ変更しません。
