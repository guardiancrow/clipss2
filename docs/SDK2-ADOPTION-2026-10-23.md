# SDK-2：第3稿A案の文面選択と非公開候補実装

記録ID：`clipss2-sdk2-adoption-2026-10-23`。判断責任者：Owner／開発者本人。根拠は本日の「Phase 3-C：SDK-2採用判断記録と新Packaging実装」というOwner指示です。外部法務意見・相談原本・Microsoft回答を新しく取得した記録ではありません。

## 適用範囲と継承

対象はclipss2 v0.1.0 Windows x64 Portable、製品build source revision `93545d458da33cef397fb77458336ea06339dcb6`の受入済み18 PE、Release /MD、VC++ Runtime外部導入です。今回のPackaging基点は`f8cf793b6dfbda951e051b90a2147d5c3a3103e3`です。新しい文書commitを製品の再ビルドrevisionにしません。

Windows SDK 10.0.26100.0、適用EULA `WIN10SDK.RTM.AUG_2018_en-US`、保全済み原文SHA-256 `0F4A26AC9DC50066F8A1BFEAAF3F092D1B9E4791DF5487B9F3C31E7C3DC4D7F5`とR4-Dまでの証拠を再利用します。SDK-1はum/x64・ucrt/x64の指定入力の確認であり、全memberの組込みを推定しません。

[BUILDINGのG2引継ぎ](../BUILDING.md)の記録ID`clipss2-g2-inheritance-2026-10-23`と外部引継ぎメモ`clipss2-g2-handoff-2026-10-23.md`に従います。G1確認済み、MSVC-1 INTERPRETED-APPROVED、TOOLS-1 CLEAR、SDK-1の既存条件付き判断を再審査せず継承します。Microsoftの個別明示許諾に読み替えません。参照基準はmicrosoft-licensing Skillおよび参照資料v1.3.0です。

## 今回Ownerが選択した文面

第3稿A案の7節を採用候補ファイルへ実装します。本文§1・§2・§3・§5・§7、§4の一般的権利保護・Microsoft source限定Excluded License条件、§6の無保証を維持します。候補用の版と対象Releaseを表示し、レビュー専用見出しのみ除きます。

- **§4**：逆解析禁止は追加しません。SDK §7の開発者側制限をSDK §2(a)(ii)の下流条件にそのまま移す根拠とせず、独立したMIT・OSSへの包括禁止を避けるというOwner選択です。法定例外・対象範囲を無視した禁止を設けません。逆解析制限を省くことが同等保護に十分かの最終契約判断は未了です。
- **§5**：GitHub Release本文で取得前に案内、ZIP内READMEで使用・再配布前に案内、EXE同階層へ条件全文を同梱します。再配布者には文書・表示の伝達と下流配布者／外部利用者への合意要求を求めます。現在の選択は候補方式の選択で、有効な合意成立の保証ではありません。
- **§6 A案**：Microsoft対象コードの無保証と強行法上の権利を維持し、新たな損害賠償制限・金額上限を設けません。SDK §14・§15の開発者対Microsoftの条件を受領者へ機械的に転記しない選択です。§15相当の責任保護を追加しない構成が§2(a)(ii)に足りるかは判断責任者による最終判断待ちです。

条件全文は[Microsoft対象コード限定条件](../packaging/binary/MICROSOFT-COMPONENT-TERMS.txt)です。SDK対象コードに限定し、MSVC startup、STL、外部Runtime、import先OS実装と独立したMIT／OSSを対象に加えません。

## 条件対応と未履行の限定事項

| 適用条項 | 今回実装する対応 | なお未確認の事項 |
| --- | --- | --- |
| SDK §2(a)(ii)：主要機能付加、指定.libのlink成果物 | 既存キャプチャ製品とSDK-1の指定入力判断を継承。.lib自体は同梱しない | 未確認memberの組込みを新たに主張しない |
| 同項：配布者・外部end userに少なくとも同等に保護する条件への合意を要求 | 条件§2・§5、Release案内・README・同梱全文 | 文面全体（特に逆解析制限／損害賠償制限の不採用）による同等保護と、この提示・合意要求方式の契約上の十分性の最終判断 |
| 同項：copyright表示、配布主体の補償・防御 | 既存表示・原文を維持。下記継続義務を記録 | 補償責任をNOTICEや利用者への転嫁で消滅させない |
| SDK §2(a)(iii) | 条件§3・§4・§5でWindows向け、権利表示、商標・推奨誤認、悪意ある用途、Microsoft source限定のExcluded License条件を扱う | 独立OSS全体に適用範囲を拡張しない。実際の継続履行は配布者の責任 |
| SDK §7 | 本候補に独自の逆解析禁止は設けない | 同等保護との関係の判断待ち。SDK利用者自身への適用契約を免除しない |
| SDK §14・§15 | 条件§6の無保証、強行法上の留保。新しい損害賠償免責・$5等の上限なし | §2(a)(ii)との関係を含む文面の最終適合判断待ち |

匿名ダウンロード・asset直リンクはReleaseページを経由しない場合があります。ZIPのREADME・全文は取得後に提示できますが、提示・合意要求と有効な合意成立は別です。第三者の再配布では元のページ表示を強制できず、§5による条件伝達・合意要求義務の連鎖を設けても全受領者の履行・合意を実証したことにはなりません。初回起動同意UI・独自installerは追加せず、製品は再ビルドしません。

## 配布主体自身の継続義務

適用契約のMicrosoftに対する補償・防御（弁護士費用を含む対象範囲）等はOwner／配布主体が負います。これを利用者へ転嫁したり、MITの免責・条件§6・NOTICEで代替したりしません。表示・商標・Windows向け配布・悪意ある用途の禁止・対象Microsoft sourceの制約、適用契約の輸出関連条件等を継続して履行します。条件伝達・合意要求方式の運用も配布主体の責任です。

## Packagingと公開ゲート

R4-Dの45ファイル契約・ZIP・receiptは不変更です。新v3 profileと新private生成許可で、43ファイルを完全継承し、README・NOTICEを差し替え、条件全文1件を加えた46ファイルを新規隔離先に生成します。18 PEと23第三者原文・MIT原文・Portable markerは不変更です。新たなZIP size・SHA-256は実測後、別世代のRelease Notesと受入receiptに記録します。

新Release Notesの条件全文permalinkと最終公開対象commitは、まだ存在しないため未確定です。公開前に実在する不変参照と正しいZIP hashへ対応付けます。Release本文は未掲載であり、現在確認できるのは文案とZIP内の配置です。

Ownerの文面選択は**法的適合性の最終判断ではありません**。SDK-2は**CLEAR IF**、G2全体のCLEARは宣言せず、G3は公開未承認です。`binaryReleaseApproved=false`、`publicDistributionAllowed=false`を維持します。Microsoftの新たな許諾、全受領者の合意、法務の承認を捏造しません。Ownerの残存リスク受容は必要な権利・契約条件を免除しません。

残件は、上記同等保護・合意要求方式の最終判断、公開時の実在する条件参照と案内の配置、継続義務の責任確認、最終ZIP／Source公開集合／Release操作のOwner承認です。非公開Packagingの技術的受入とは独立して扱います。

## 再評価トリガー

適用契約・法令・重要な一次資料、利用主体、SDK／MSVC・リンク構成の適用範囲逸脱、Microsoft対象コードやRuntime同梱物、条件文面・提示経路・合意方式、第三者再配布方式の実質的変更または具体的な不適合証拠です。通常commit、AIやスレッドの変更、同一18 PEの再確認だけでは終了済み監査を再開しません。
