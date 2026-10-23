# SDK-2追補：承認済み§4・§6とv4非公開候補

記録ID：`clipss2-sdk2-addendum-2026-10-23`。元の判断記録IDは`clipss2-sdk2-adoption-2026-10-23`です。[旧判断記録](SDK2-ADOPTION-2026-10-23.md)はv3の固定証拠として変更しません。

## 判断主体・根拠・対象

承認日は2026-10-23、判断主体・責任者はOwner／配布主体です。根拠は今回の「Phase 3-D：承認済みMicrosoft条件文書の実装とv4 Private Packaging受入」というOwner指示で、同指示に掲載された§4・§6英文の正式採用・承認です。新しいMicrosoft回答・個別許諾・外部法務意見を取得した記録ではありません。承認済み英文の採否は今回の最終決定として継承し、同じ議論を再審査しません。

対象はclipss2 v0.1.0 Windows x64 Portable、製品build source revision `93545d458da33cef397fb77458336ea06339dcb6`の同一18 PE、Release /MD、VC++ Runtime外部導入です。Packaging基点は`ffac56bd1713d5afe659cb16dc9fe2e34e3a6649`です。後続Packaging commitを製品の再ビルドrevisionにはしません。

適用SDKは10.0.26100.0、EULAは`WIN10SDK.RTM.AUG_2018_en-US`、保全済み原文SHA-256は`0F4A26AC9DC50066F8A1BFEAAF3F092D1B9E4791DF5487B9F3C31E7C3DC4D7F5`です。microsoft-licensing Skillと参照資料v1.3.1、[BUILDINGの既存G2引継ぎ](../BUILDING.md)を適用します。G1、MSVC-1 INTERPRETED-APPROVED、TOOLS-1 CLEAR、SDK-1の既存条件付き判断は再監査しません。

## 第3稿A案から変更した範囲

新しい[条件文書v4](../packaging/binary/MICROSOFT-COMPONENT-TERMS-v4.txt)の§4・§6を、Owner指示の全文へ置換します。文書版を2とし、対象Releaseと公開時のファイル名`MICROSOFT-COMPONENT-TERMS.txt`は維持します。§1・§2・§3・§5・§7はv3と同じbytesです。§4・§6はLFへ統一した承認英文と比較し、内容を言い換えません。

- **§4**：実際に組み込まれたCovered SDK Codeだけを対象に、技術的制限の回避・逆解析・逆コンパイル・逆アセンブルの制限を設け、適用法の例外を留保します。同じEXE/DLLに含まれるだけで独立した自作MIT・第三者OSSへ拡張しません。Microsoft source限定のExcluded License条件を維持し、製品全体にGPL禁止を課しません。旧A案の「逆解析禁止を追加しない」という選択は今回の承認範囲で更新されます。
- **§6**：無保証に加え、適用法が許す最大限でMicrosoftおよび供給者について、対象SDKコードに関する損害賠償責任の保護を設けます。認められる範囲で第三者受益者とする文面と、強行法・法定権利・執行不能時の留保を含めます。独立した成分やclipss2配布者自身への包括免責に拡張しません。旧A案の「新しい損害賠償制限を設けない」という選択は今回の承認範囲で更新されます。

Microsoft対SDK利用者という別契約の金額上限を下流へ機械的に移さず、今回承認された別の対象限定文面を採用するため、$5等の金額上限は転記・新設しません。全面的な責任制限の文面を採用したこと自体で、SDK §2(a)(ii)の同等保護や条項の有効性が確定したとは扱いません。

## 提示・合意要求・継続義務

既存§5は不変更です。GitHub Releaseで取得前に案内、READMEで使用・再配布前に案内、EXEと同階層に全文を同梱する方式を継承します。README・NOTICEは対象限定とMIT／第三者権利の分離、既存の条件ファイル名を参照しており、今回の§4・§6改訂との矛盾がないためv3のbytesを継承します。

提示、合意要求、有効な合意成立は区別します。匿名ダウンロード・直リンク・第三者再配布の全受領者について、閲覧・合意・条項の法的執行力を証明するものではありません。初回起動同意UI・独自installer・製品再ビルドは追加しません。新しい公開URL・不変permalink・GitHub Release本文の掲載実績は創作せず、公開前の導線確定と実配置を残件として保持します。

配布主体のMicrosoftに対する補償・防御・hold harmless（対象となる弁護士費用を含む）等の継続義務はOwner／配布主体が負います。条件§6はこれをend userへ転嫁せず、MIT免責・NOTICE・技術試験で履行済みにはしません。適用契約上の表示・商標・Windows向け配布・用途・対象source・輸出等の継続義務も旧記録から継承します。

## v4 Packagingの境界

新v4 profile・private生成許可・Reader/writerを追加し、v3の受入済み候補を旧Validatorで確認してから使います。46ファイルのうち45ファイルをv3から完全継承し、条件文書1件だけを差し替えます。18 PE、23第三者原文、MIT LICENSE、Portable marker、README・NOTICEは同一です。Runtime DLL・installer・OS DLL・.lib・PDB・設定・画像・ログ・内部承認記録はZIPへ追加しません。

R4-Dの45ファイルZIP（SHA-256 `5FDCB141E825E7FFADDFFA6EEC16B5BFEBB7CF65C08C690DD98BFA6A83D03B0A`）と、v3の46ファイルZIP（7,967,972 bytes、SHA-256 `B36CEFBDD2D7C00CE0F5FF17278365A977547C2655AB5D5CFDDC3B263025E5B2`）を保全します。旧profile・authorization・Reader/writer・receipt・判断記録を変更しません。

v4は新規のworkspace/build専用領域に限り、既存rootを空でも採用せず、削除・上書きしません。固定hash・証拠・ファイル集合・条文・Git基点が不一致なら停止し、部分出力を保全して受入receiptを発行しません。旧世代へのfallbackはありません。新ZIPのsizeとSHA-256は測定後、外部receiptと実行結果へ記録します。旧Release Notesのv3 hashをv4用に流用しません。

## 実行入口

`tools/package_s1_binary_sdk2_v4.ps1`のModeはPlanOnly、CreatePrivateCandidate、ValidateCandidate、ValidateTree、ValidateZipです。RootとR2EvidenceRoot、R3EvidenceRoot、R4BEvidenceRoot、R4DEvidenceRoot、V3EvidenceRootを指定します。Createは未存在のOutputRoot、各Validateは対応するCandidateRoot／PackageRoot／ZipPathを指定します。PlanOnlyは読み取り専用です。

生成許可はこの指示とPackaging基点、固定profile、追補記録、承認条文hashに限定したローカル整合性契約です。署名・第三者による真正性証明ではありません。commit後の検証は生成時HEADをreceiptから継承し、現在のHEAD相違だけで許可を緩和しません。再生成は基点を含む新しい正規許可を必要とします。

## ゲート・限定した残件

Ownerによる§4・§6英文の正式採用は記録済みです。これはMicrosoftの個別許諾・新しい法務意見・全受領者との有効な合意・適用法上の確実な執行力を意味しません。

**SDK-2はCLEAR IFを維持、G3は未承認、binaryReleaseApproved=false、publicDistributionAllowed=falseです。** 技術受入からG2 CLEARや公開GOを自動発行しません。残る契約判断は、承認文面と§5方式によるSDK §2(a)(ii)の同等保護・合意要求の十分性、適用法上の効力と限界です。条文の採否を再度未決事項に戻すものではありません。

公開時には実在する条件全文の不変参照・Release案内の配置と正しいv4 ZIP identity、継続義務の責任確認、Source公開集合・tag対象・公開操作のOwner承認が必要です。これらは今回の非公開技術受入とは別です。再評価は対象契約・コード・SDK/リンク・配布主体・文面／導線・適用法等の実質的変更、具体的な新証拠に限ります。
