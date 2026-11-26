# clipss2 0.1.3 Windows x64 Portable

ZIP 全体を新しい書込み可能なフォルダーへ展開し、clipss2.exe、隣接の DLL 18 本、clipss2.portable を同じ場所に置いてください。clipss2.exe --help で起動方法を確認できます。通常起動は常駐、--settings は設定画面です。

Microsoft Visual C++ Redistributable x64 を別途導入してください。使用した Build Tools と同じか新しい対応版が必要です。配布物に Runtime DLL やインストーラーは含まれません。 [Microsoft 公式案内](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)

更新前に旧版のプロセスをすべて終了し、必要な設定と画像を保全してください。新旧 Portable 版を併用する場合は別フォルダーに展開し、設定先を分けてください。同一ホスト・同一ユーザーの協調プロセス間で設定の読取りと保存を直列化します。同じ schema の古い状態から全項目を保存すると、別の変更が戻る場合があります。旧版や外部ツールの書込み、別ユーザー・別ホスト間は同期対象外です。

自作部分は [MIT](LICENSE) に従います。[第三者ソフトウェア通知](THIRD-PARTY-NOTICES.md)、[原文の出典](SOURCES.md)、同梱の LICENSE／PATENTS を保持してください。[Microsoft 対象コード限定条件](MICROSOFT-COMPONENT-TERMS.txt) は該当する Microsoft 由来部分に適用されます。ビルドと依存関係は BUILD-IDENTITY.json と SBOM.cdx.json を参照してください。
