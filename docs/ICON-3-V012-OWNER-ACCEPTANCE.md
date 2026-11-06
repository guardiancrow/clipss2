# v0.1.2 Portable candidate — Owner acceptance

状態: **PENDING**。ICON-2のOwner報告や旧公開版の受入をこのZIPへ継承していません。

- 対象: `clipss2-0.1.2-windows-x64-portable.zip`
- Size: 8,014,600 bytes
- SHA-256: `F6F4A14784F7DE40CE1130720FBF84775B481558C0C5E1CBE53FFC36F9F916BE`
- Build: `clipss2-0.1.2-icon-rc1`
- Build source: `0c6731e15576dace9f5ecacadd5ea9006cca7ae2`

ZIP全体を新しい書込み可能な専用folderへ展開してください。既存の設定・画像は別に保全し、
旧版への上書きやシステムアイコンキャッシュの削除は行いません。
開始前に`--help`のVersion 0.1.2と、ZIP hashを確認します。

各項目についてPASS / FAIL / SKIP、Windows版、DPI、テーマ、必要な所見を記録してください。
利用者の画像、個人の設定や非公開スクリーンショットを公開ソースへ追加しないでください。

| 確認項目 | 状態 | 所見 |
| --- | --- | --- |
| Portable展開、通常起動、正常終了 | PENDING | |
| Explorer詳細/小/大アイコン、実機100% DPI | PENDING | |
| Explorer詳細/小/大アイコン、実機200% DPI | PENDING | |
| 設定UIの単独起動と常駐からの起動 | PENDING | |
| タスクバー/Alt+Tabのアイコン・表示・切替 | PENDING | |
| 通知領域のアイコン・メニュー・終了 | PENDING | |
| 既存設定の読込みと別の試験用設定の保存 | PENDING | |
| 所有内容のWindow CaptureとPNG保存 | PENDING | |
| JPEG XL可逆保存・再表示 | PENDING | |
| JPEG XL非可逆保存・再表示 | PENDING | |
| Clipboard書込み・他アプリへの貼付け | PENDING | |
| JPEG/WebP/AVIFの簡易保存・再表示 | PENDING | |
| ライト/ダークテーマ比較 | PENDING | |
| DPI/monitor間移動、再表示 | PENDING | |
| 実際のExplorer再起動後の回復（可能なら） | PENDING | |
| 別PCでの展開・起動・capture（可能なら） | PENDING | |

Region/Monitor、HDR、未列挙のcodec/profile組合せや全OS/GPUの保証ではありません。
Codexの200%設定タイトルバーや擬似TaskbarCreated確認はAPI/限定表示の証拠であり、
この表のOwnerによる実操作を済ませたという意味ではありません。

候補受入後も、Source/Binary/ZIP identityに対する最終確認、必要な公開条件の実配置、
Ownerによるpush/PR/merge/tag/Releaseの明示的な公開操作承認を別に行います。
