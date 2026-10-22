# clipss2 Portable — private validation candidate

**非公開の検証用候補です。第三者へ配布・公開しないでください。**
MS-002と最終Owner承認は未完了です。`binaryReleaseApproved=false`を維持します。
自作部分のMIT LICENSEは、組込みMicrosoftコードや第三者成分を一括してMITで許諾するものではありません。

## この候補

- Source revision: `93545d458da33cef397fb77458336ea06339dcb6`
- Shipping Privacy v2 / S1、`windows-x64-shipping-v2`、Release x64 /MD。
- 製品EXE 1本とapp-local DLL 17本。PDB、設定、画像、ログ、テストEXEは含みません。
- R2はPASS WITH LIMITATIONS。実測はWindows x64、25H2 build 26200.9711、SDR/fallback経路です。
- 別PC、Windows 10、Region/Monitor、常駐・設定・選択UIとClipboardへの実書込みは未受入です。対応OS下限の正式確定は残件です。

## 外部Runtime

WindowsのWGC/D3D11機能と、互換性のあるMicrosoft Visual C++ v14 Redistributable **x64** が必要です。
R2で解決した外部Runtimeはmsvcp140.dll、msvcp140_atomic_wait.dll、vcruntime140.dll、vcruntime140_1.dllで、実測versionは14.51.36247.0でした。
使用toolsetはMSVC 14.44.35207/compiler 19.44.35229.0、SDK 10.0.26100.0です。
RuntimeのDLL・installerは同梱せず、自動取得・自動導入もしません。System32から抽出してコピーしないでください。
必要な場合は[Microsoft公式案内](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)から利用者が別途確認・導入します。別PCでのversion下限検証は未完了です。

## 検証用の使い方

1. 新しい書込み可能な専用フォルダーへ全内容を展開します。既存設定や画像のある場所へ上書きしないでください。
2. `clipss2.portable`をEXEの隣に保ちます。内容はASCII `clipss2-portable-v1`と末尾LFです。
3. `clipss2.exe --help`でCLIを確認できます。通常起動は常駐mode、`--settings`は設定UIです。
4. Window捕捉では自分が所有する非個人情報のテスト対象だけを選びます。PNG/Fileを選択し、専用出力先を確認してから実行してください。
5. Portable modeの設定はEXE隣の`settings.conf`、既定画像先は`captures`です。これらは実行後の利用者データであり、このZIPには含めません。
6. 終了・更新の前に製品を正常終了し、必要な設定・画像を利用者自身で保全します。既存データの自動移行・削除は行いません。

R2の自動Smokeは同一hashの製品workerで所有HWNDを捕捉し、772×546 PNG/File保存・正常終了を確認したものです。上記GUI全体の受入を代替しません。

## 権利表示

自作部分は[LICENSE](LICENSE)、第三者成分と原文の対応は[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)を参照してください。
既知の原LICENSE/PATENTSは`licenses`以下へ同梱しています。Microsoft STL原文だけはCRLFをLFへ統一し、本文は変更していません。他の既存原文のbytesは保持しています。特許の非侵害やMicrosoft再頒布許諾の全面保証を表しません。
この候補には製品署名・公開許可はありません。最終Binary Releaseには権利残件、別PC/手動確認と利用者の明示承認が必要です。
