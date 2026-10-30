# v0.1.1 candidate — Owner受入checklist

判定対象は[検証記録](SEC-BR2-VALIDATION.md)の新Source/Binary/ZIP identityです。
以下は手動受入待ちであり、チェック済み・公開承認済みとは扱いません。

1. [Release本文案](RELEASE-NOTES-0.1.1-CANDIDATE.md)のZIP size/SHA-256を確認し、
   新しい書込み可能な専用folderへ展開する。旧v0.1.0配布物・設定・画像は別途保全する。
2. 取得前の条件案内、ZIP内README・完全NOTICE・LICENSE・Microsoft対象コード条件文書を読む。
   承認済み条文本体は維持している。新成果物への適用と公開導線を確認し、旧G3を流用しない。
3. EXE 1本、DLL 17本、`clipss2.portable`を同階層に保持する。
   `--help`のVersion 0.1.1とCLIを確認し、Runtime不足をSystem32等からの無断コピーで補わない。
4. 常駐起動、tray・終了、設定・選択UIを確認する。既存の常駐instance・hotkeyと競合させない。
5. Ownerが権限を持つ非機密の対象windowを用い、必要なWindow/Region/Monitorのcaptureと保存を確認する。
   PNG、JPEG、WebP、AVIF、JPEG XL、必要な可逆/非可逆profileを選ぶ。新binaryの実機結果として記録する。
6. Clipboardの内容を保全したうえで、必要ならFile/Clipboard/Bothと失敗時の振舞いを確認する。
   自動テストのmemory serializationをClipboard実publicationの成功へ読み替えない。
7. 設定保存・再起動後の読み戻し、EXE隣の`settings.conf`と既定`captures`の使用を確認する。
   Portable運用で意図しない利用者領域へ設定が作られないか確認する。配布ZIPへ利用者データを戻さない。
8. 必要に応じて空白・日本語を含む展開先、別PC/OS/Runtime、DPI/GPU/HDR等を確認する。
   未確認の組合せを動作保証へ含めない。未署名の扱いも確認する。
9. 結果は日時・OS/Runtime・対象mode/format・candidate hashと結合し、非機密の検証記録として保全する。
   画像や利用者の設定・private logsを公開sourceへ混入させない。
10. 公開phaseでsource差分、Source archive commit、ZIP identityと条件全文の実在permalinkを最終照合する。
    merge/push/tag/Releaseの各操作は別途明示的に承認する。修正が入れば影響するbuild・package・検証を再固定する。

Microsoftの同一Owner・toolchain・SDK・Release /MDに関する既存判断を再起票するchecklistではありません。
新しい候補の技術受入、提示導線、新Source/Binary identityに対するG3は別の判断です。
