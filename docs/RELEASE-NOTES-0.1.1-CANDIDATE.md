# clipss2 v0.1.1 — Security maintenance release candidate

**LOCAL CANDIDATE / OWNER ACCEPTANCE PENDING / NOT PUBLISHED**
この文書は正式公開前の本文案です。push・merge・tag・GitHub Release公開は行っていません。

## バイナリ取得前の条件案内

Before downloading the binary package, review the full
[MICROSOFT-COMPONENT-TERMS.txt](../packaging/sec-br2/MICROSOFT-COMPONENT-TERMS.txt).
The distributor requires recipients and redistributors of the Covered SDK Code
to agree to its component-specific conditions before using or redistributing
that code. These conditions preserve the separate MIT and independent
third-party licenses. A download, extraction, or execution alone does not
establish explicit agreement.

条件全文はZIP内のEXEと同階層にも同梱しています。上の相対linkはローカル候補レビュー用です。
正式公開時には、push済みの実在する不変commit permalinkへ結合し、取得linkより前に提示します。
現時点で未公開URLの到達確認や新たなOwner公開承認を主張しません。

## 更新内容

Brotliを1.2.0から1.2.1へ更新し、CVE-2026-111497に対応するセキュリティ保守更新です。
通常の製品経路から悪用可能であるとの実証ではありません。脆弱componentの配布を修正する目的です。
libjxl 0.12.0とその他package version、Shipping Privacy、既存codec機能とCLI引数は維持します。
EXEのversion resourceと`--help`のversion表示を0.1.1へ整合させています。

## 構成と導入

Windows x64 Portable、MSVC Release /MD、EXE 1本とapp-local DLL 17本です。
Microsoft VC++ Runtime x64は外部導入、Runtime DLL・installer・OS DLL・Debug CRTは同梱しません。
未署名です。必要なRuntimeは[Microsoft公式案内](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)を参照してください。

ZIP全体を新しい書込み可能なfolderへ展開し、Portable markerとDLLを保持してください。
使用・再配布前に同梱README、完全NOTICE、Microsoft対象コード条件文書を確認してください。
既存の設定・画像・旧v0.1.0 ZIPを上書きしません。

| 項目 | ローカル候補 |
| --- | --- |
| ファイル名 | `clipss2-0.1.1-windows-x64-portable.zip` |
| サイズ | 7,970,478 bytes |
| SHA-256 | `A13DF0E714A061C367548DD5296C3C2D1A7CBCE81E2030FE2BB8C9AD1DE02169` |
| 内容 | 47ファイル：18 PE、23第三者原文、LICENSE、README、完全NOTICE、Portable marker、Microsoft条件文書、BUILD-IDENTITY |
| Build ID | `clipss2-0.1.1-sec-br2-rc1` |
| 製品build source | `6c4f48f8908746a7b06ab046ab715a51cbab86f8` |

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath ./clipss2-0.1.1-windows-x64-portable.zip
```

hashは内容照合用であり、コード署名・発行者認証ではありません。
Source配布候補はこのbuild commitと後続Packaging資料を含む最終ローカルcommitから別ZIPで生成します。
最終Source ZIP identityは外部delivery記録と最終返信に記載し、Packaging commitをEXEの再build revisionにしません。

## 検証範囲と公開前残件

新旧contract tests、CTest 38件、EXE version、18 PEのx64/import/hash・privacy、ZIP検証、
展開したEXEの別cwdからの`--help`はPASSです。詳細は[SEC-BR2検証記録](SEC-BR2-VALIDATION.md)。

常駐・設定・選択GUI、実capture、Clipboard実書込み、別PC/OS/GPU/DPI/HDR等は新binaryのOwner受入項目です。
旧v0.1.0の実機受入・公開承認を継承していません。[Owner checklist](SEC-BR2-OWNER-ACCEPTANCE.md)に従って確認し、
新しいSource/Binary/ZIP identityに対する承認と公開導線の実配置を別phaseで行います。
