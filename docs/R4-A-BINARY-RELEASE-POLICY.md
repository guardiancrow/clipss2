# R4-A: S1 Binary Release文書・契約案

## 基点と承認の範囲

Packaging基点は`47f205fb80756273fd42f7be4bff066e2580bb65`、branchは`release/shipping-s1-v2`です。製品のbuild source revisionは`93545d458da33cef397fb77458336ea06339dcb6`で、R2受入済み18 PEを変更しません。新しい文書のcommitを製品の再ビルドrevisionとして表示しません。

OwnerはR4-A依頼で配布方式を承認しました。WinMerge型とはWindowsネイティブPortable ZIPという方式の呼称であり、WinMergeの許諾・配布実績をclipss2の許諾根拠へ流用しません。GitHub Releases、自作部分MIT、第三者NOTICE/原LICENSE/PATENTS分離、Release /MD、VC++ Runtime x64外部導入、installerなし、必要性が確認されるまで独自EULA/クリック式同意なし、初回未署名を基本案、ZIP SHA-256公開という方針です。

これは最終成果物・GitHub Release公開操作・法的許諾の承認ではありません。`binaryReleaseApproved=false`、`publicDistributionAllowed=false`、`r4BGenerationAuthorized=false`を保持します。今回ZIPを生成しません。

## R3との差分と文書構成

| 項目 | R3 private v1 | Release v1設計 |
| --- | --- | --- |
| README/NOTICE | private検証向け | Release向けの別file、レビュー草案・未承認を明示 |
| 配布方式 | 非公開検証のみ | 将来のGitHub Releases Portable ZIPという承認済み方針 |
| 製品/原文 | 18 PE、23第三者原文、MIT LICENSE、marker | すべて同じbytes。45同梱file中README/NOTICEの2件だけを差し替える予定 |
| 別PC | 当時未確認 | 今回のユーザー申告による展開・起動・capture・保存の成功を限定追記 |
| Microsoft論点 | MS-002一括保留 | MS-002-T / MS-002-L / MS-002-SDKと下流条件を分離 |
| 署名/hash | 検証用hash | 未署名を基本案、最終ZIPのSHA-256を公開予定。まだ最終ZIP/hashはない |
| 承認 | false | false。方針承認・残存リスク判断・公開許可を別々に扱う |

新世代は`packaging/public/profile-s1-binary-release-v1.json`と、`packaging/binary/README-portable-s1-release-v1.md`、`THIRD-PARTY-NOTICES-S1-BINARY-RELEASE-v1.md`です。ZIP内では従来と同じ`README.md`と`THIRD-PARTY-NOTICES.md`を使います。内部のOwner判断表・監査ログ・個人情報はZIPに入れません。

新profileはR3 profileのraw identityを親bindingとし、旧Source binding、固定28入力、R2 evidence binding、18 PE、23原文とSTLのLF変換情報を継承します。既存v1/v2/v3のprofile/anchor、R3のREADME/NOTICE/Reader/ZIP/receiptは書き換えません。旧source公開承認も継承しません。

## Microsoft条件：既存証拠の再利用

Skill v1.1.0の一度限りの移行再評価は前の会話で完了しています。本作業は配布文書の差分整理であり、移行再評価・全PE解析・包括ライセンス監査の再実行ではありません。根拠IDは同Skillの参照台帳に従います。

| 論点 | 状態・適用根拠 | 既存文書で対応する部分 | 不足・Ownerが判断する部分 |
| --- | --- | --- | --- |
| MS-001 | CONDITIONAL、L06/L07 | Runtime非同梱、公式x64 Runtimeへの案内 | 対応環境の説明と将来同梱変更時の再評価 |
| MS-003 | 本人申告・前回条項判断を継承、L04/L05 | MSVC 14.44.35207/compiler 19.44.35229.0の既存使用資格判断 | 今回再ビルドなし。将来build時は前提の継続を確認 |
| MS-002-T | VERIFIED、T01/T02 | 標準のRelease /MDとstartup静的組込みを明示 | startupの存在自体を異常・違反・照会必須としない。詳細member一覧の限界も既存記録を継承 |
| MS-002-L | NEEDS CLARIFICATION、L03/L05 | 自作MITとMicrosoft権利を分離。STL/SDKの許諾で代用しない | MSVC Tools startup/補助コードへの適用許諾・指定・下流条件の文書化された判断が不足 |
| MS-002-SDK | CONDITIONAL、L02/P01 | SDK 10.0.26100.0のum/x64・ucrt/x64はSDK REDISTの対象指定あり。SDK .lib自体を配布しない | 指定コードを組み込んだ成果物に適用されるSDK条件の履行・下流への扱い |

SDK installed条項は`WIN10SDK.RTM.AUG_2018_en-US`、既存raw SHA-256は`0F4A26AC9DC50066F8A1BFEAAF3F092D1B9E4791DF5487B9F3C31E7C3DC4D7F5`です。Community 2022 §4、SDK §2(a)(i)～(iii)と、[SDK REDIST](https://learn.microsoft.com/en-us/legal/windows-sdk/redist)、[VS2022 REDIST](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution)、[Build Tools 2024-03条項](https://visualstudio.microsoft.com/license-terms/vs2022-ga-diagnosticbuildtools/)は前回限定確認の結果を再利用します。今回契約を再取得・再解釈していません。

主要機能の付加、製品copyright表示、Microsoft商標で製品を推奨と誤認させない表示、Windows向け配布、MicrosoftコードをMITに一括包含しない説明、.lib非同梱は現行構成で対応しています。

Community/SDKの下流保護・合意条件について、誰に、どの対象コードの条件を、いつ、どう示し、どの方法で履行・合意を扱うかは未確定です。README/NOTICEの存在だけを有効な同意の証明としません。一方、独自EULAやクリック式同意が必須であるという判断もしていません。新たな必要性の根拠が得られるまでUI/独自契約を追加しません。SDKの補償・輸出規制等は配布主体の継続義務であり、NOTICEで代替しません。

不確実な明文許諾、確認された契約上の条件、Ownerの運用上の残存リスク判断を区別します。Ownerが不確実性を理解してリスクを引き受けても、第三者の権利や必要な条件を免除したり、MS-002-Lを法的PASSに変更したりする根拠にはなりません。Microsoft個別照会を必須工程・唯一の解決方法にはしません。十分な一次資料や適用版を特定した専門家評価等による判断も対象にできます。

## 別PC結果の扱い

R3 ZIP SHA-256 `6F7029885A9FFD9E84AE8BA9AEFB14B8BCCF3165AC7D07E456C72715944E763D`についてのユーザー申告を`USER_REPORTED_PASS`として記録します。展開・EXE起動・capture・保存に限る成功です。別PCのOS/Runtime、capture mode、保存形式、現地hash照合はUNKNOWN/NOT RECORDEDです。実施のやり直しは要求せず、既存R2/R3 resultや旧別PC検証表を遡って変更しません。Windows 10、全GUI、Clipboard、全codec、HDR等の保証には拡張しません。

## 最終Owner判断の具体項目

配布方式の選択は承認済みです。以下は未完了の別項目です。

1. **許諾判断の採用**：MS-002-Lの対象コード・適用契約・許諾対応について、採用する資料/専門家評価、結論と未確定範囲を特定する。根拠なくLEGAL PASSへ変更しない。
2. **下流条件の履行方法**：適用条件、対象者、表示内容・時点・方法、合意の扱いを特定する。独自EULA/クリック式同意なしの構成で足りるか、必要性が確認された対応があるかを判断する。
3. **残存リスク・責任**：上記判断後にも残る許諾解釈の不確実性、SDKの継続義務、未署名/SmartScreenと未検証機能を理解し、配布主体としての判断を記録する。権利や条件の放棄ではない。
4. **公開説明**：対応OS/Runtimeの表現と別PCの申告範囲を確定する。未記録の環境版や全機能の成功を創作しない。
5. **署名方針の最終確認**：初回未署名の基本案を対象Releaseについて確定する。署名へ変更してPE bytesが変われば旧18 PE identityをそのまま用いない。
6. **候補生成の許可**：R4-Bで使う新profile/Reader/writer、隔離先、候補の扱いを明示承認する。権利保留中は非公開候補に限り、公開・第三者引渡しを許可しない。
7. **最終公開操作の許可**：生成・展開受入後のZIP名/size/SHA-256、18 PE、同梱NOTICE/原文集合、Release版/tag/対象commit、GitHub repositoryと掲載内容を特定して承認する。方針承認や候補生成許可を公開操作の許可に読み替えない。

## Reader境界とR4-Bへの接続

`packaging/S1BinaryReleasePlan.psm1`はcode-pinned新profileを読み取る設計検証専用Readerです。書込み・ZIP作成・公開・承認フラグの変更機能を持ちません。

```powershell
Import-Module ./packaging/S1BinaryReleasePlan.psm1
Read-S1BinaryReleasePlan -Root $PWD -R2EvidenceRoot <R2証拠の絶対path> -R3EvidenceRoot <R3証拠の絶対path>
```

旧`package_s1_binary.ps1`はprivate v1専用で、新profileを受け付けません。旧ReaderはHEADと製品source revisionの一致を要求するため、Packaging-only commit後のHEADで旧Createをそのまま使いません。固定hashやHEAD検査を無効化せず、R4-Bの明示依頼で新世代用writer/受入接続を実装・検証します。製品build revisionとPackaging revisionを分離し、R2 identityを保持しながら旧契約を迂回しない設計とします。

R4-Bは条件付きで準備可能です。明示的な候補生成依頼と新世代writerの受入後に、まず新規隔離先で非公開候補として扱います。公開承認ではありません。権利条件未確定の間は新profileも公開未承認です。R4-BでREADME/NOTICEを変更する場合は新profileのhashを正規に再固定し、生成後のZIP SHA-256を記録します。現R3 ZIP hashを将来のRelease ZIP hashとして公表しません。
