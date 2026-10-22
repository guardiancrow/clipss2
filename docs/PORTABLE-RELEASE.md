# Portableと将来Releaseの準備（source-only）

S1はsource-onlyのsource-release契約で、配布ZIP/EXE/DLL/installerや署名済み製品はありません。
依存Install経路は実装済みですが、実install・製品build・runtimeは未実施です。[BUILDING](../BUILDING.md)を参照してください。
将来のbinary配布は、PE集合、Runtime、NOTICE/原文、Packaging/署名/公開判断の別審査が必要です。

製品実装ではEXE隣の既存settings.confを優先し、有効な[Portable marker見本](../packaging/clipss2.portable)を扱います。
markerの内容はASCII clipss2-portable-v1と末尾LF。見本は実設定ではありません。
設定の[記述例](../packaging/settings.conf.example)も実ユーザーデータではありません。
設定保存先・起動modeの詳細はsource実装に従い、未知の既存設定の自動移行・削除を案内しません。

将来受入した製品の更新時は、先に終了し、設定/必要な画像を利用者自身で保全してください。
削除時も製品と設定・保存画像を分け、必要な個人データを確認してから利用者が処理します。
Microsoft RuntimeをSystem32から抽出・再配布する手順はありません。再頒布条件や実version対応は未確定です。
新S1の起動・Portable動作・cleanupは[NOT TESTED](OTHER-PC-VALIDATION.md)であり、旧構成の成功を流用しません。
