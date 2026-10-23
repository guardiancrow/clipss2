# R4-B: 非公開Release候補生成

## 許可の範囲と固定契約

OwnerのR4-B依頼により、新規隔離領域での候補生成と受入だけが許可されています。製品source revisionは`93545d458da33cef397fb77458336ea06339dcb6`、今回のPackaging HEADは`73c593690b46b018975f3b914da97ab334ad0e35`です。新writer/CLIの未commit bytesは候補receiptに相対path/size/SHA-256として記録します。Packaging commitを製品再ビルドrevisionとは表示しません。

`packaging/public/authorization-s1-r4b-private-v1.json`は、この明示指示を記録した独立のcode-pinned契約です。対象は固定R4-A profileと今回のPackaging HEAD、許可操作は非公開候補生成・検証です。署名されたOwner認証や法的許諾の証明ではありません。

R4-A profileの`r4BGenerationAuthorized=false`は設計時点の記録として不変更です。新Readerはそのfalseも検証し、別契約の`r4BGenerationAuthorized=true`を明示的に組み合わせます。`binaryReleaseApproved=false`、`publicDistributionAllowed=false`、第三者配布・GitHub Release・署名・法的承認のfalseは両者を通じて維持します。フラグを無視する、旧profileへfallbackする、既存profileを再固定する経路はありません。

MS-001/003、MS-002-T、SDKおよび第三者成分の既存判断を継承します。MS-002-LとMicrosoft下流条件はNEEDS CLARIFICATIONです。技術的受入、候補生成許可、公開許可を混同せず、ライセンス再監査やMicrosoft個別照会を本工程の必須操作にしません。

## 実装境界

新`packaging/S1BinaryReleasePackage.psm1`が、R4-Aのread-only Readerに接続します。R4-A ReaderとR3 moduleの既知のraw hashを検証してから読み込みます。R3から再利用するのはZIP entry/size/hash/metadata検査、safe path、CreateNew書込み、新規directory所有権の汎用primitiveです。既存のreader/writer、code-pinned SHA、HEAD一致検査、公開Source契約は変更しません。

新writerはR4-Aの45ファイル閉集合、固定入力、R2/R3 evidence bindingに加え、R2 identityの18 PEを明示照合します。43ファイルはR3と同じbytes、README/NOTICEの2件だけがR4-A版です。auth、owner、receipt、監査資料はZIPへ同梱しません。

生成時は現在のHEAD/branch/repository rootが承認記録と一致することを要求します。実装のレビュー用変更は未commitで記録できます。後日のread-only受入は現HEADを新たな生成許可へ読み替えず、固定auth・profile・receipt・実装identity・実ファイルを照合します。別HEADでの再生成には、その条件に対応した正規の許可契約が必要です。旧HEADへresetしたり旧契約を緩めたりしません。

## 手順

PowerShell 7.2以降で、既存R2/R3証拠の絶対pathを指定します。以下の変数は呼出者が設定します。

```powershell
$r2Evidence = '<既存R2証拠の絶対path>'
$r3Evidence = '<既存R3証拠の絶対path>'
pwsh -NoProfile -File tools/package_s1_binary_release.ps1 -Mode PlanOnly -R2EvidenceRoot $r2Evidence -R3EvidenceRoot $r3Evidence
```

PlanOnlyはread-onlyで、outputを作りません。生成はworkspace/build配下の未存在の一意containerを選び、既存のreparse/未知データとの衝突がないことを確認してから行います。

```powershell
$container = Join-Path $PWD ('build/r4-b-release-private-' + [guid]::NewGuid().ToString('N'))
if (Test-Path -LiteralPath $container) { throw 'Existing container' }
New-Item -ItemType Directory -Path $container -ErrorAction Stop
$candidate = Join-Path $container 'candidate'
pwsh -NoProfile -File tools/package_s1_binary_release.ps1 -Mode CreatePrivateCandidate -R2EvidenceRoot $r2Evidence -R3EvidenceRoot $r3Evidence -OutputRoot $candidate
pwsh -NoProfile -File tools/package_s1_binary_release.ps1 -Mode ValidateCandidate -R2EvidenceRoot $r2Evidence -R3EvidenceRoot $r3Evidence -CandidateRoot $candidate
```

outputは新規leafだけを所有し、既存rootは空でも拒否します。containerは`build/r4-b-release-private-`または隔離テスト用`build/r4-b-package-tests-`の接頭辞に限定します。既存物の削除、上書き、自動resume・自動昇格は行いません。失敗時はpartialを保全し、成功receiptを発行しません。

`payload`を閉集合・hashで確認後、固定日時/属性でZIPをCreateNewし、R3由来のcanonical ZIP検査で全entryを照合してから新規`expanded`へ展開します。展開内容と入力を再確認した後だけ`acceptance.json`を最後にCreateNewします。owner単体・ZIP単体・partialは受入済みとしません。

ValidateCandidateは外側のowner/receipt/ZIP/payload/expandedの閉集合も確認し、receiptの生成許可・公開false、revision、実装hash、全file hash、ZIP hash、inventory hashを実物と照合します。ValidateTree/ValidateZipは個別の内容確認用で、公開承認ではありません。

## 限定検証と次工程

```powershell
pwsh -NoProfile -File tests/s1_binary_release_package_test.ps1 -R2EvidenceRoot $r2Evidence -R3EvidenceRoot $r3Evidence -TestRoot (Join-Path $PWD ('build/r4-b-package-tests-' + [guid]::NewGuid().ToString('N')))
```

隔離fixtureで公開許可との混同、証拠差し替え、旧profile fallback、余分な設定/PDB/LIB/画像/ログ/PRIVATE資料、改変原文/PE/文書、ZIP entry改変、owner/receiptの偽装、partial受入を拒否することを確認します。R2 Regression・実キャプチャ・PE全解析・再ビルドは行いません。

R4-Cのレビューへ引き継ぐのは非公開候補、ZIP size/SHA-256、45ファイルinventory、生成許可記録、受入receiptと継続残件です。MS-002-L、下流条件、対応環境の表現、未署名方針の最終判断と具体的成果物へのOwner公開許可は継続します。R3の別PC成功報告を新README/NOTICEや新ZIP全体の現地hash検証へ拡張しません。
