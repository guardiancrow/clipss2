# clipss2 0.1.0 — Windows x64 Portable

Window・Region・Monitorを選び、PNG / JPEG / WebP / AVIF / JPEG XLへ保存するWindows向けキャプチャツールです。File / Clipboard / Both出力とPortable設定保存を備えます。機能の実装範囲と、以下の確認済み動作範囲は区別してください。

## 必要環境と配布構成

- Windows x64、D3D11、Windows.Graphics.Captureを利用します。すべてのWindows版・GPU・driverでの動作を保証するものではありません。
- Microsoft Visual C++ v14 Runtime **x64**を外部前提とします。必要な場合は[Microsoft公式案内](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)から別途導入してください。
- Runtime DLL・installer・Windows標準DLLは同梱せず、自動取得・自動導入もしません。System32から回収してコピーしないでください。
- EXE 1本とapp-local DLL 17本、Portable marker、LICENSE、NOTICE、必要なLICENSE/PATENTSを含むZIPです。インストーラはありません。
- この配布物には発行者のコード署名を付していません。Windowsやセキュリティソフトが発行者未確認等の警告を表示する場合があります。
- ZIPのSHA-256はRelease情報と照合してください。hashは同一性確認用であり、コード署名や発行者の認証の代わりにはなりません。

## 使い方

1. ZIP全体を新しい書込み可能な専用フォルダーへ展開します。
2. `clipss2.portable`をEXEの隣に保持します。内容はASCII `clipss2-portable-v1`と末尾LFです。
3. `clipss2.exe --help`でCLIを確認できます。通常起動は常駐mode、`--settings`は設定UIです。
4. 権限のある捕捉対象を選び、画像形式・File/Clipboard/Both・出力先を確認して使用します。
5. Portable設定はEXE隣の`settings.conf`、既定画像先は`captures`です。利用者の設定・画像は配布ZIPに含まれません。
6. 終了・更新時は製品を正常終了し、必要な設定・画像を保全してください。既存データへの上書きや自動移行を前提としません。

## 確認済み動作と制限

独立実測はWindows x64、Windows 11 25H2 build 26200.9711、SDR/fallback経路です。所有HWNDのWindow Capture、772×546 PNGのFile保存、正常終了を確認しています。Runtime実測は14.51.36247.0です。この値を最低必要versionとは断定せず、未実測のOS・Runtime版への保証下限も設定していません。

同じ18 PEを含む以前の非公開候補について、別PCでの展開・EXE起動・キャプチャ・保存の成功がユーザーから報告されています。別PCのOS/Runtime版、capture mode、保存形式、現地hash照合は未記録です。本ZIP全体をその別PCで検証したという主張ではありません。

常駐・設定・選択GUIの全体、Clipboardへの実書込み、Region/Monitor、全codec・全profile、Windows 10、HDR/WCG、複数monitorをまたぐRegion、すべてのGPU/DPI/driver等の組合せは受入済みではありません。実装された機能すべてが実機で確認済みであるとは扱いません。

## Microsoft SDK対象コードの条件

使用・再配布の前に、EXEと同階層の[MICROSOFT-COMPONENT-TERMS.txt](MICROSOFT-COMPONENT-TERMS.txt)を確認してください。配布者は、同文書で限定したCovered SDK Codeの受領者・再配布者に、その成分限定条件への合意を求めます。自作部分のMITと独立した第三者OSSの条件は維持されます。文書の提示、合意要求、有効な合意成立は別であり、ダウンロードや実行だけで明示的合意が成立したとは扱いません。

## ライセンスと権利表示

clipss2の自作部分は[MIT LICENSE](LICENSE)です。第三者成分の権利と原文は[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)および`licenses`以下に分離しています。MITの表示はMicrosoft・第三者コードを一律MITで許諾するものではありません。NOTICEは権利情報の表示であり、記載のない追加許諾やMicrosoftの推奨・承認を表しません。

## 製品識別

Shipping Privacy v2 / S1、Release /MD、製品source revision `93545d458da33cef397fb77458336ea06339dcb6`です。文書・Packaging用commitを製品再ビルドrevisionと混同しないでください。
