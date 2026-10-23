# clipss2 Portable — Release向け草案

この文書は公開前のレビュー用です。MS-002-Lと下流条件の判断、最終Owner承認は未完了です。公開・第三者配布の許可を表しません。

## 配布構成と環境

- Windows x64向けPortable ZIP。配布先はGitHub Releasesとし、インストーラは作成しません。
- Shipping Privacy v2 / S1、Release /MD。EXE 1本とapp-local DLL 17本です。
- 製品のsource revisionは`93545d458da33cef397fb77458336ea06339dcb6`です。後続の文書・Packaging用commitと区別します。
- WindowsのWGC/D3D11機能とMicrosoft Visual C++ v14 Runtime **x64** が必要です。
- RuntimeのDLL・installerは同梱せず、自動導入もしません。必要な場合は[Microsoft公式案内](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)から利用者が別途導入します。System32から回収してコピーしないでください。
- 初回は未署名Releaseを基本案とし、ZIPのSHA-256をReleaseページに掲載する方針です。hashは同一性確認用であり、署名や発行者の認証を代替しません。Windows等が発行者未確認の警告を表示する場合があります。

## 使い方

1. ZIP全体を新しい書込み可能な専用フォルダーへ展開します。
2. EXE隣の`clipss2.portable`を保持します。内容はASCII `clipss2-portable-v1`と末尾LFです。
3. `clipss2.exe --help`でCLIを確認できます。通常起動は常駐mode、`--settings`は設定UIです。
4. 捕捉対象・画像形式・File/Clipboard/Bothと出力先を確認して使用します。権限のある対象だけを選んでください。
5. Portable設定はEXE隣の`settings.conf`、既定画像先は`captures`です。ZIPには利用者の設定・画像を含めません。
6. 終了・更新時は製品を正常終了し、必要な設定・画像を保全します。既存フォルダーへの上書きや設定の自動移行を前提としません。

## 確認範囲

R2の技術受入はPASS WITH LIMITATIONSです。独立実測はWindows x64、25H2 build 26200.9711、SDR/fallback経路で、所有HWNDのWindow Capture・PNG/File保存・正常終了を確認しています。

R3の非公開ZIP候補について、別PCでの展開・EXE起動・キャプチャ・保存の成功がユーザーから報告されています。別PCのOS/Runtime版、capture mode、保存形式、現地でのhash照合は未記録です。全GUI・全codec・Clipboard・Region/Monitor・HDR・全環境の受入へは拡張しません。

R2のRuntime実測は14.51.36247.0です。この値を必須の最低versionとは断定しません。対応OS・Runtimeの保証下限は最終公開説明で確認する必要があります。

## ライセンスと権利表示

clipss2の自作部分は[MIT LICENSE](LICENSE)です。第三者成分の権利と原文は[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)および`licenses`以下に分離しています。Microsoft・第三者コードを一律MITとして許諾するものではありません。

独自EULA・クリック式同意画面は、必要性が確認されない限り追加しない方針です。このREADMEを読んだことやダウンロードしたことだけで、Microsoftに関する下流条件への有効な同意が成立したとは扱いません。MS-002-Lの許諾対応と適用される下流条件の扱いは未確定で、必要な対応が判明した場合は公開前に判断します。

この草案はMicrosoftによる承認、特許の非侵害、正式Binary Release承認を表しません。
