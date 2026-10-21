# 操作の説明（実装仕様）

これはソース内の製品機能の説明で、S1製品のruntime受入やダウンロード案内ではありません。
本候補に実行ファイルはなく、実build/installは[構築状況](../BUILDING.md)のとおり未完了です。

将来自分の責任で構築・受入した製品は、無引数でトレイ常駐し、起動だけでは撮影しない設計です。
トレイまたはCtrl+Shift+F11でWindowを選びます。CLIは--capture-window / --capture-region / --capture-monitor / --settingsです。
Regionは単一モニター内、Monitorは1台で、仮想desktop全体を一括撮影する機能ではありません。

選択中はEnterで確定、Escで取消。Fで形式、OでFile/Clipboard/Both、Cでカーソル、Pで対応形式のprofile、EでJPEG encoderを選ぶ実装です。
PNG、JPEG、WebP Lossless、AVIF/JXL Lossless/Lossyを含み、8-bit SDR/sRGBを対象とします。JPEGは透過なし、HDR/WCGは対象外です。
ClipboardはPNG/DIBV5、Bothはファイル保存成功後にClipboardを更新する設計です。
全codec/profile/異常系のS1実機検証は未実施です。

設定とPortableの扱いは[Portable準備](PORTABLE-RELEASE.md)、OS/API条件は[SUPPORT](SUPPORT.md)、権利文書は[S1 NOTICE](../THIRD-PARTY-NOTICES-S1.md)を参照してください。
個人の画面内容、window title、設定全文、pathを公開issueへ貼らないでください。
