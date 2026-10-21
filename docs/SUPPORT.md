# S1の対象環境と未実測範囲

対象sourceはWindows x64、D3D11、Windows.Graphics.Captureを用います。
cursor制御APIのWindows 10 version 2004/build19041以降は検証下限候補であり、全環境での動作保証ではありません。
DisplayInformation interopはWindows 11 build22621以降を前提とし、古い環境では限定したDisplayConfig/DXGI fallbackを含みます。
Windows 11 24H2/build26100以降のConsole Allocation Policyを、Windows 10でも同等と推定しません。
これらは既存sourceのAPI設計上の境界で、今回OS実機検証を追加した結果ではありません。

新S1はMSVC x64 / Ninja single-config / Release / Privacy ON / profile v2のみを公開支援経路とします。
現在のCMake統合は静的・script mode/CLI接続の受入までで、実configure・依存install・ABI・runtimeは未実施です。
旧構成にあったWindows 10の限定USER-REPORTED成功や別PC成功をS1へ転用しません。

captureは実capability・target・color・topology等を確認し、不明/不整合を拒否する設計です。
HDR/WCG、複数monitorをまたぐRegion、全driver/GPU/DPI/アクセシビリティ/codec組合せの保証はありません。
[外部PC検証表](OTHER-PC-VALIDATION.md)は新S1についてNOT TESTEDです。
[利用仕様](USAGE.md)と[構築制限](../BUILDING.md)を確認してください。
