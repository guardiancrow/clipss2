# S1 Binary候補のPackaging契約

この契約はR2受入済みrevision `93545d458da33cef397fb77458336ea06339dcb6`の18 PEだけを対象にします。
旧Source v2/v3 catalog/anchor/承認状態は不変更です。Source Readerの検査成功をBinary承認に変換しません。
新規Binary関連fileは旧Source PublicationViewの固定集合外です。将来のsource公開集合の再固定・Owner判断は別操作です。

## MS-002の限定確認（2026-10-23）

- 使用環境はMSVC 14.44.35207/compiler 19.44.35229.0、SDK 10.0.26100.0、Release /MDです。R2の4外部VC++ RuntimeはSystem32の14.51.36247.0でした。
- [Microsoft CRT資料](https://learn.microsoft.com/en-us/cpp/c-runtime-library/crt-library-features?view=msvc-170)は、/MDでもmsvcrt.libのnative CRT初期化/終了コードが静的にリンクされることを示します。
- [2022 REDIST一覧](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution)とinstalled Redist.txtを照合しました。installed一覧は公式一覧への参照です。今回のmsvcrt.lib等の組込み部分に対する許諾を、この一覧だけで確定できません。
- [Build Tools条項（2024-03）](https://visualstudio.microsoft.com/license-terms/vs2022-ga-diagnosticbuildtools/)は既存MS-003判断と同じ資料を参照しました。今回の条項から、全組込みCRTコードを公開再頒布可能とする新しい根拠は得ていません。MS-003自体を再審査したものではありません。
- installed SDK 10.0.26100.0のsdk_license.rtfはDistributable Codeの指定と、対象.libをlinkerへ通した結果の配布条件を含みます。これは任意のMSVC .libを対象へ指定する条項ではありません。
- installed vector/yvals_core.hのSPDX表示はApache-2.0 WITH LLVM-exceptionです。Microsoft STL vs-2022-17.14の原LICENSEを追加保存しますが、この条件を非STLのCRT startup/SDK部分へ拡張しません。

**MS-002は未解決のままです。** Microsoftの正式回答または適用版を特定した判断により、実際のCRT起動コード・SDK組込み部分と許諾対象の対応、配布上の条件を確認する必要があります。
MS-001のRuntime非同梱、MS-003とskcms/Highwayの既存判断は維持します。技術的Packaging成功によってクローズしません。
ローカルで自己使用する非公開検証候補だけを生成し、第三者への引渡し・公開・配布を許可しません。外部配布時の未解決事項を回避する手順ではありません。

## 独立Binary profile

`packaging/public/profile-s1-binary-private-v1.json`をBinary Readerのcode-pinned SHA-256で固定します。
hashはローカル整合性契約であり、署名、Owner承認、法的許諾の証明ではありません。
profileは18 PE、同梱文書・原LICENSE/PATENTSのsize/hash、成分対応、固定28入力、旧Source資料とR2証拠のraw identityを持ちます。
新規STL LICENSEだけはGitの既存LF属性に従い、取得原文のCRLFをLFへ正規化します。profileには取得URI/revision、取得時のsize/SHA-256と変換方法も固定します。Readerは同梱LF原文からCRLFを復元し、取得原文のhashへ一致することを検証します。本文の変更、既存原文の再生成、Git属性やsafecrlfの変更はありません。
R2証拠は独立したEvidenceRoot引数で指定します。個人絶対path、監査ログ、R2画像をpackageへコピーしません。
`binaryReleaseApproved=false`、`publicDistributionAllowed=false`、未解決MS-002と最終Owner判断を固定し、CLIから上書きする入口はありません。

## 実行手順

```powershell
pwsh -NoProfile -File tools/package_s1_binary.ps1 -Mode PlanOnly -EvidenceRoot <R2証拠の絶対path>
pwsh -NoProfile -File tools/package_s1_binary.ps1 -Mode CreatePrivateCandidate -EvidenceRoot <R2証拠の絶対path> -OutputRoot <workspace/build配下の未存在path>
pwsh -NoProfile -File tools/package_s1_binary.ps1 -Mode ValidateTree -EvidenceRoot <R2証拠の絶対path> -PackageRoot <展開先の絶対path>
```

PlanOnlyはread-onlyです。Createは全入力の受入後、新規rootだけにpayload・ZIP・新規展開先・検証receiptを作ります。
既存root、reparse、path traversal、case alias、余分なfile/directory、改変PE/原文、重複ZIP entry、ZIP metadataのsymlink等を拒否します。
ZIP entryはprofileの閉集合と完全一致し、entryの内容hashを展開前に照合します。FileMode.CreateNewで展開し、既存内容へ上書きしません。
失敗時は途中出力を保全し、受入receiptを発行しません。ZIP全体と全展開fileのsize/SHA-256を外部receiptへ記録します。自己参照hashを作るためのmanifest書換えはありません。

同梱README/NOTICEと原文は候補の正確な集合として確定しますが、Microsoftの未解決条件を含めた正式Binary NOTICE/公開承認の完了とは扱いません。
R4ではMS-002、R2のGUI/別PC等の制限、最終Owner承認、署名方針を確認します。署名等でPEが変わる場合は旧R2 identityをそのまま再利用できません。
