# clipss2 v0.1.2 — Original application icon candidate

**LOCAL CANDIDATE / OWNER ACCEPTANCE PENDING / NOT PUBLISHED**

## バイナリ取得前の条件案内

Before downloading the binary package, review the full
[MICROSOFT-COMPONENT-TERMS.txt](../packaging/icon-v012/MICROSOFT-COMPONENT-TERMS.txt).
The distributor requires recipients and redistributors of the Covered SDK Code
to agree to its component-specific conditions before use or redistribution.
Independent MIT/OSS rights are preserved. Downloading, extracting or executing
the package alone does not establish explicit agreement.

同じ条件全文をZIP内のEXEと同じ階層にも同梱しています。現時点の相対linkはローカル候補用です。
正式公開を許可された段階で、実在する不変commit permalinkを取得linkより前に配置します。
未公開のv0.1.2 tag・asset URLの到達や公開承認を主張しません。

## 変更と維持事項

- 独自の青い角丸プレートと4隅のキャプチャフレームをEXE、設定UI、通知領域に導入。
- 16/24/32/48/64/128/256pxのアルファ付きアイコン。外部ロゴの流用なし。
- 設定ウィンドウの大小アイコンとDPI変更、常駐から開く設定のタスクバー表示に対応。
- ProductVersion 0.1.2、FileVersion 0.1.2.0、CLI Version 0.1.2。
- Windows x64 Portable、Release /MD、外部VC++ Runtime、既存CLI・設定互換性とcodec機能を維持。
- Brotli 1.2.1、libjxl 0.12.0、その他依存versionを維持。v0.1.1のセキュリティ修正を保全。

v0.1.1は[公開済み](https://github.com/guardiancrow/clipss2/releases/tag/v0.1.1)です。
旧v0.1.0/v0.1.1のtag、ZIP、Source/Binary identityは変更していません。

## ローカルPortable候補

| 項目 | 候補 |
| --- | --- |
| ZIP名 | `clipss2-0.1.2-windows-x64-portable.zip` |
| サイズ | 8,014,600 bytes |
| SHA-256 | `F6F4A14784F7DE40CE1130720FBF84775B481558C0C5E1CBE53FFC36F9F916BE` |
| 構成 | 47 files: EXE 1、DLL 17、第三者原文23、LICENSE、README、完全NOTICE、Portable marker、Microsoft条件、BUILD-IDENTITY |
| Build ID | `clipss2-0.1.2-icon-rc1` |
| 製品build source | `0c6731e15576dace9f5ecacadd5ea9006cca7ae2` |
| 製品build source tree | `e5c23cffbfabead1ecf04d7ae75810c8734f2235` |

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath ./clipss2-0.1.2-windows-x64-portable.zip
```

未署名です。hashは内容照合用で、発行者認証を代替しません。Runtime DLL、installer、
Windows標準DLL、Debug CRT、開発用SVG/PNG、利用者の設定・画像は同梱していません。
ZIP全体を新規の書込み可能なfolderへ展開してください。旧版や既存の設定・画像へ上書きしません。

## 検証と残件

Source contract fixtures 9/9、Release configure/build/RC、CTest 39/39、全18 PEのx64/import、
/MDとDebug CRT不在、7サイズの埋め込みアイコン、VERSIONINFO・manifest、
全47 payload hash、ZIP fixtures 13/13と独立展開を確認しました。
展開EXEの`--help`、200% DPI設定タイトルバー、常駐・通知領域の登録/擬似再登録/終了も確認。

ICON-2でOwnerが承認したデザイン・Explorer（詳細/小/大、100%/200%）は参考記録であり、
この新しいZIPの実機受入は未完了です。[Owner checklist](ICON-3-V012-OWNER-ACCEPTANCE.md)に従い、
Window Capture、JPEG XL可逆/非可逆、Clipboardと他アプリへの貼付け、他形式、設定、
タスクバー/Alt+Tab、通知領域等を新候補で確認します。別PC/テーマ/DPI/Explorer再起動等の
未実施項目をPASSにはしていません。[詳細](ICON-3-V012-VALIDATION.md)を参照してください。

製品build sourceと、その後のPackaging/documentation commitは別です。
最終Source ZIPのcommit・tree・size・SHA-256は自己参照を避けてローカルdelivery記録へ保存します。
Source ZIPとGitHub生成archiveの同一bytesや、独立環境のbitwise再現性を仮定しません。
