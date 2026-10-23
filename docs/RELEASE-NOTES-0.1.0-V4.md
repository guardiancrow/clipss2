<!-- v4 Release本文テンプレート。公開前に条件全文permalinkとSource archive commitの2項目を、実在確認した値へ置換する。生成した本文は追跡対象外へ保存し、commit自身のhashをこのfileへ埋め込まない。 -->
# clipss2 v0.1.0 — Windows x64 Portable

Windows向けスクリーンキャプチャツールです。Windows x64ネイティブのPortable ZIPとして展開して使用します。インストーラはありません。

## バイナリ取得前の条件案内

Before downloading the binary package, review [MICROSOFT-COMPONENT-TERMS.txt]({{MICROSOFT_TERMS_PERMALINK}}). The distributor requires recipients and redistributors of the Covered SDK Code to agree to its component-specific conditions before using or redistributing that code. These conditions preserve the separate MIT and independent third-party licenses. A download, extraction, or execution alone does not establish explicit agreement.

条件全文はZIP内のEXEと同階層にも同梱します。Microsoft SDK対象コードに限定した条件であり、自作部分のMITや独立した第三者OSSを一括して制限するものではありません。

## 主な機能

- Window、単一モニター内のRegion、Monitorの選択。
- PNG / JPEG / WebP / AVIF / JPEG XLへの保存。
- File / Clipboard / Both出力、常駐・設定UI、Portable設定保存。

上記は実装機能です。実機で受入済みの範囲は以下に限定します。

## 必要環境・導入

Windows x64、D3D11、Windows.Graphics.Captureを使用します。Microsoft Visual C++ v14 Runtime **x64**が外部前提です。必要な場合は[Microsoft公式案内](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)から別途導入してください。Runtime DLL・installer・Windows標準DLLは同梱せず、自動導入もしません。

ZIP全体を新しい書込み可能なフォルダーへ展開し、`clipss2.portable`と17本のapp-local DLLをEXEの隣に保持してください。`clipss2.exe --help`でCLIを確認できます。通常起動は常駐mode、`--settings`は設定UIです。Portable設定は`settings.conf`、既定画像保存先は`captures`です。配布ZIPに利用者の設定・画像は含めていません。

## 確認した動作と既知の制限

独立実測はWindows 11 25H2 x64 build 26200.9711、SDR/fallback経路、Runtime 14.51.36247.0です。所有HWNDのWindow Capture、772×546 PNGのFile保存、正常終了を確認しました。限定Regressionは12件成功、失敗0件です。これらのOS・Runtime版を最低必要versionとして保証するものではありません。

同じ18 PEを含む以前の非公開候補について、別PCでの展開・EXE起動・キャプチャ・保存の成功がユーザーから報告されています。別PCのOS/Runtime版、capture mode、保存形式、現地hash照合は未記録です。今回のZIP全体を別PCで検証したという主張ではありません。

常駐・設定・選択GUI全体、Clipboardへの実書込み、Region/Monitor、全codec・全profile、Windows 10、HDR/WCG、すべてのGPU/DPI/driver等の組合せは受入済みではありません。HDR/WCGや複数monitorをまたぐRegionを保証しません。

## 未署名・ダウンロードの確認

この成果物には発行者のコード署名を付していません。Windowsやセキュリティソフトが発行者未確認等の警告を表示する場合があります。

- ファイル名: `clipss2-0.1.0-windows-x64-portable.zip`
- サイズ: **7,968,423 bytes**
- SHA-256:

```text
3803495B4798C6B7D6B30BC0A0E08495FC275DB4E0A576A78CCD60BEBBF0FECC
```

取得したZIPはPowerShellで確認できます。

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath ./clipss2-0.1.0-windows-x64-portable.zip
```

hashは内容の同一性確認用で、コード署名や発行者認証の代わりにはなりません。

## ライセンス・製品識別

自作部分はMIT Licenseです。第三者成分の権利表示は同梱`THIRD-PARTY-NOTICES.md`、必要な原LICENSE/PATENTSは`licenses`以下に分離しています。自作MITをMicrosoft・第三者コード全体への一括許諾として扱いません。Microsoftによる本製品の推奨・承認を示すものではありません。

Shipping Privacy v2 / S1、Release /MD、EXE 1本とapp-local DLL 17本です。製品build source revisionは`93545d458da33cef397fb77458336ea06339dcb6`です。文書・Packaging用commitを製品の再ビルドrevisionとして表示しません。

## Source archiveとの対応

GitHub自動生成のSource archiveは、tag `v0.1.0`の対象commit `{{SOURCE_ARCHIVE_COMMIT}}`のソース・文書・Packaging関連ファイルです。添付するWindows実行用ZIPとは別の配布物であり、Source archiveに受入済みEXE・DLLを同梱したという意味ではありません。

最初の公開mainはSource-onlyのsnapshotでした。今回のtagには後続の文書・Packaging準備も含まれます。製品のbuild入力は上記の固定revisionであり、tag対象commitと異なることは製品の再ビルドを意味しません。
