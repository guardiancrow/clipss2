# ICON-3 v0.1.2 candidate validation

判定: **READY FOR OWNER ACCEPTANCE**。正式公開・新ZIPのOwner実機受入は未完了。

## Source / Build / Packaging

| 項目 | identity |
| --- | --- |
| ICON-2親commit | `7dc253259f62b136ef58d1bd0655da06247ec0c7` |
| 製品build source commit | `0c6731e15576dace9f5ecacadd5ea9006cca7ae2` |
| 製品build source tree | `e5c23cffbfabead1ecf04d7ae75810c8734f2235` |
| Build ID | `clipss2-0.1.2-icon-rc1` |
| Source inputs SHA-256（224 bindings） | `B7EEE6B375C281B6BE4084E613C257194FD90E94AF2125B1DED131F801188097` |
| Packaging profile SHA-256 | `787E3DAA741DA1D7FB1475345C1D98F4B03448D1BCB29BF01E498AB93F4F1996` |
| EXE SHA-256 | `6D56315C3798AEEF3CEB9C3AEC52D3E195E142035DB5BA8D0E8372770C9A22F4` |
| ZIP size | 8,014,600 bytes |
| ZIP SHA-256 | `F6F4A14784F7DE40CE1130720FBF84775B481558C0C5E1CBE53FFC36F9F916BE` |

後続Packaging/documentation commitは製品build revisionとは別。最終Source ZIPは
その最終commitから生成して全Git blobへ照合し、commit/tree/size/hashをローカルdeliveryへ記録する。
既存ICON-2 commitの改変、mainへのcommit、push、PR、merge、tag、Release操作は行わない。

## 新契約と依存

`icon-v012` は新しいRelease候補scope。旧 `icon-local` の受入を公開用に読み替えていない。
224 source bindingsはC++、headers、SVG/ICO、RC、manifest、CMake、admission codeを含む。
製品build時はcleanな不変commitと全bindingを照合。Packaging時にも元commitのtree/input blob、
source閉集合、build receipt、全18 PEと入力を照合する。署名済みprovenanceの代替とは扱わない。

依存は元のSEC-BR1/BR2 checkoutの固定inputs・installed全内容・receipt/markerとmanaged sourceを
読み取り検証して使用。旧製品ZIP/EXEから流用していない。

| 固定依存記録 | SHA-256 |
| --- | --- |
| SEC-BR1 source inputs | `EF0EB35F031CE4424408709D0871C2A72F0FBF109A08C7295F911F40B4FFFE32` |
| installed receipt | `0D84E144C82AA74208CF57B93271D1C33EF0724643E24C947E21338EFD617D35` |
| installed marker | `56116DE94EAC39F8B5D7179629ABE0E78C1C2A205542257688268F0239CC688D` |

Brotli 1.2.1、libjxl 0.12.0、他packageも既存16 packageと同一。再install・更新なし。
旧S1/SEC-BR1/SEC-BR2固定入力・source binding・receipt・marker・Packaging profile・
既存検証証拠、公開済みv0.1.0/v0.1.1のtag/assetsは変更しない。

## 自動検証

| 項目 | 結果 |
| --- | --- |
| Source改変拒否fixtures | 9/9 PASS |
| Release configure/build/RC | PASS、新しい専用root |
| CTest | 39/39 PASS、9.89秒 |
| Release /MD compile flags | 155 project compile commandsで確認 |
| 全18 PE | AMD64、import確認、Debug CRTなし、検査した個人pathなし |
| DLL | 全17 DLLが固定installed原bytesと一致 |
| icon resource | RT_GROUP_ICON 101、7 RGBA PNGと採用ICOが同一bytes |
| version | ProductVersion 0.1.2、FileVersion 0.1.2.0、CLI 0.1.2 |
| manifest | PerMonitorV2 / PerMonitor、true/pm、detached console policy保持 |
| payload / ZIP / 展開tree | 全47ファイルの集合・size・SHA-256一致 |
| ZIP/profile fixtures | 13/13 PASS |
| 別cwdで展開EXEの`--help` | PASS |

CTestには既存codec、設定、Clipboard image、tray、console lifecycle、privacy diagnostics、
新アイコンの全サイズ/owned fallback/96→192→144→96 DPI置換、Brotli実DLL round tripを含む。
実キャプチャや他アプリへのClipboard貼付けを実施したという意味ではない。

ZIP fixturesは正常系、欠落、余剰、重複、traversal、予約名、内容改変、過大長、timestamp、
symlink metadata、末尾余剰byte、公開承認偽装、run-root traversalを検査した。
既存の純粋なcanonical ZIP/tree primitivesのみ再利用し、旧BR2 profile admissionは使わない。
最初のZIP fixture試行はテスト側のbyte[]束縛で停止したため修正し、新しい証拠rootで再実行。
候補ZIP自体は変更していない。失敗証拠もローカルに保持した。

## 表示・動作とOwner報告の区別

ICON-2について今回Ownerが報告した受入は、デザイン・識別性とExplorerの詳細/小/大表示を
実機100%/200% DPIで確認し問題なし、という範囲。未報告の操作をPASSに追加していない。

新v0.1.2展開EXEでは単独/常駐所有Settingsを実機192 DPIで起動。
small=32px / big=64px、WS_EX_APPWINDOWと可視状態をAPIで確認し、タイトルバー画像を目視確認。
通知領域の登録・削除、rect取得、擬似TaskbarCreated 2回と設定通知後の継続、exit code 0を確認。
設定を保存せず、試験プロセスのみ終了。旧設定、テーマ/DPI、システムicon cacheは変更していない。

この候補のExplorer、タスクバー/Alt+Tab、通知領域の見た目、実機100% DPI、テーマ比較、
実capture、JPEG XL実保存/再表示、Clipboardと貼付け、別PC、実Explorer再起動等はOwner受入待ち。
[チェックリスト](ICON-3-V012-OWNER-ACCEPTANCE.md)はすべてPENDINGとしている。

## NOTICE・Microsoft条件・Privacy・文書

47ファイルの内訳は18 PE、23第三者原文、6文書/marker。SVG/PNGや開発ツールは同梱しない。
全23 LICENSE/PATENTSは既存固定原bytes。完全NOTICEにBrotli 1.2.1、依存全成分、Microsoftと
独自アイコンMITの由来を保持。Microsoft条件はv0.1.1の承認済み本文を維持し、製品見出しだけ更新。
同じMSVC 14.44.35207、SDK 10.0.26100.0、Release /MDとRuntime外部導入の既存判断を継承し、
新しい配布承認や全受領者の合意成立を主張しない。

検査範囲は今回のSource差分、新規素材の既存hash/metadata、README/Packaging/NOTICE、
新ZIPの全ファイル、EXE/DLL、commit author/committer。既存の全Git履歴は再監査していない。
不要なユーザー絶対path、資格情報、認証URL、画像metadataや内部証拠の新規混入は検査範囲で未検出。
author/committerはICON-2と同じ既存設定を使用し、設定や署名方針を変更していない。
ローカル証拠・画像・ログ・失敗成果物はignored build配下のみ。

README日英両版の「v0.1.1未公開」という現在状態の不一致を修正し、公開済みv0.1.1と
未公開v0.1.2の区別、ダウンロード先、セキュリティ履歴とアイコン追加を反映。
BUILDINGには新scopeへの入口を追加し、旧公開/歴史的scopeのRelease限定・固定root・clean前提は
保持した。過去のPENDING記録や固定契約の書換えで解消していない。

正式公開前の残件は新ZIPのOwner受入、必要な再検証、Source/Binary/ZIP identityの最終承認、
取得前条件案内とZIP内本文の一致・不変URLの実配置、公開操作の個別承認。
