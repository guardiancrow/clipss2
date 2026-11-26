# clipss2 v0.1.3

この版は Windows x64 向けのセキュリティ保守更新です。JPEG XL、PNG、zlib、TurboJPEG、libspng、AOM、libyuv を承認済み構成に更新し、libwebp 1.6.0 に上流の修正を移植しました。Brotli 1.2.1 を継続して使用します。詳細は [構築手順](BUILD-V013.md) と [第三者ソフトウェア通知](../packaging/v013/THIRD-PARTY-NOTICES.md) を参照してください。

- JPEG XL の非可逆保存でも、完全不透明な画像を含め、既存の alpha チャンネル出力契約を維持します。
- 同じ Windows ホスト・同じユーザーで、新しい同期規約に従うプロセスの設定読取りと保存を直列化します。同じ schema では、最後に成功した全項目の保存を採用します。古いスナップショットで別の変更が戻る場合があります。旧版、外部ツール、別ユーザー・別ホストの書込みは同期対象外です。 [設定 I/O 同期規約](SETTINGS-IO-SYNCHRONIZATION.md)
- 非可逆 AVIF balanced の保存時間は、既存の比較環境で旧公開版より Full HD で約 3.1%、1921×1081 で約 4.2%、4K で約 3.7% 長くなりました。これらは特定の画像と環境での測定値です。セキュリティ修正を優先した構成を採用しています。

更新前に旧版の常駐や設定画面を終了し、設定と画像を保全してください。新旧の Portable 版を併用する場合は、別々のフォルダーへ展開し、設定の保存先を分けてください。

外部の Microsoft Visual C++ Redistributable x64 が必要です。OS と Runtime の確認範囲は [動作環境](SUPPORT.md) を参照してください。
