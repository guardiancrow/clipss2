# SEC-BR2 — v0.1.1 local release candidate

この経路は公開候補のローカル構築用です。push、merge、tag、Release公開を行いません。
SEC-BR1 commit `d5ded1bfc0ee3bd44d0dcb11cd5f0bbccc8c35ed`を親として保全します。

## 入力とversion identity

`CMakeLists.txt`のproject versionを0.1.1へ更新し、SEC-BR2 scopeでEXEの
FileVersion 0.1.1.0 / ProductVersion 0.1.1と`--help`のversion行を生成します。
既存CLIの引数・処理・usage行は維持します。codec機能は変更しません。

この変更と新scope分岐はBR1のraw固定入力2件に影響します。旧ledgerのhashを
書き換えず、`cmake/security/sec-br2/br1-preimages/`の原bytesから歴史的な
BR1 contract/test viewを再構成します。新しいbuild-inputs-v1.jsonは現在のbuild入力を固定します。
旧BR1のPlan/Configureは原commitまたは歴史的view用であり、新versionの承認経路にしません。

既存のS1 raw28入力、BR1 overlay、ledger、receipt/marker、managed source・v2派生を保全。
新構築はSEC-BR1の検証済み16 package・19 installed DLLを明示的に参照し、
17 app-local DLLを新product rootへ配置します。v0.1.0のPEや旧packageを流用しません。
旧manifestのversion-string 0.1.0は歴史的な依存解決metadataで、製品versionは新project/resourceで管理します。

## ローカル手順

固定SEC-BR1 dependenciesとmanaged inputsの準備はSEC-BR1ガイドに従います。
以下はその受入済みtreeが同じcheckoutに存在する場合です。Source ZIPからも
PrepareContractsはGit metadataを必要としませんが、依存ソース取得・構築は別操作です。

新規環境では先にSEC-BR1 commitのソースで依存を構築・受入し、その同じcheckoutの
ignored依存領域を保全したままSEC-BR2ソースへ進みます。これは依存buildの継承で、
v0.1.0製品PEの流用ではありません。本経路をSource ZIP単独での依存初回Installと説明しません。

```powershell
pwsh -NoProfile -File tools/build_security_release_sec_br2.ps1 -Mode PrepareContracts
pwsh -NoProfile -File tools/build_security_release_sec_br2.ps1 -Mode PlanOnly
pwsh -NoProfile -File tools/build_security_release_sec_br2.ps1 -Mode ValidateDependencies
pwsh -NoProfile -File tools/build_security_release_sec_br2.ps1 -Mode VerifyManaged
```

PrepareContractsは既存viewを上書きしません。以降は原OwnerのMSVC x64 developer shellで、
sourceをcommitしてcleanな状態から実行します。

```powershell
pwsh -NoProfile -File tools/build_security_release_sec_br2.ps1 -Mode Configure
cmake --build build/sec-br2/product
pwsh -NoProfile -File tools/build_security_release_sec_br2.ps1 -Mode ValidateProduct
```

新product/execution rootは既存なら空でも拒否します。失敗時に削除・上書き・自動resumeしません。
ログ・一時出力はSEC-BR2専用rootへ限定し、SEC-BR1 installed/cache/証拠は読み取りで使用します。
既存の同一Owner・MSVC 14.44.35207・SDK 10.0.26100.0・Release /MDの資格・リンク判断を継承し、
Microsoft条件文書は承認済みv4の条文本体を保持して対象versionの見出しだけを更新します。
v0.1.0のG3承認や実機受入をv0.1.1へ自動継承しません。

新旧contract testsと利用可能なCTest、package検証の実結果・手動受入項目はSEC-BR2検証記録で区別します。

## 固定候補のPackaging

`packaging/public/profile-sec-br2-v1.json`は今回のbuild source、243 source bindings、
18 PE、23原文、rendered文書を固定した候補profileです。旧v0.1.0 profile/authorizationの
生成許可・PEを流用しません。新moduleは既存のcanonical ZIP形式を47ファイルへ適用し、
余剰entry、重複、traversal、link属性、timestamp/hash不一致を拒否します。

```powershell
pwsh -NoProfile -File tools/package_security_release_sec_br2.ps1 -Mode PlanOnly
pwsh -NoProfile -File tools/package_security_release_sec_br2.ps1 -Mode CreateCandidate
pwsh -NoProfile -File tools/package_security_release_sec_br2.ps1 -Mode ValidateCandidate
```

CreateCandidateは固定 `build/sec-br2/candidate/` が存在すれば空でも拒否します。
payload・ZIP・expanded treeを検証後、最後にacceptance receiptを書きます。途中失敗は保全し、
削除・上書き・自動resumeしません。自己記録のhashは署名・公開承認ではありません。

この固定profileを別buildのhashに合わせて再利用しません。再buildでidentityが変わる場合は
新しいcandidate/profileと検証が必要です。今回の測定値は[検証記録](SEC-BR2-VALIDATION.md)、
手動受入と次phaseは[checklist](SEC-BR2-OWNER-ACCEPTANCE.md)を参照してください。
