# Windows x64 の動作環境

v0.1.3 の候補は Windows x64 Portable、Release /MD です。外部の Microsoft Visual C++ Redistributable x64 は、使用した Visual Studio Build Tools と同じか新しい対応版を使用してください。最小の Runtime 版は実行検証で確定していません。 [Microsoft 公式案内](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)

Windows 10 2004／build 19041 は API に基づく検証下限の候補です。旧 OS の実機受入は未実施です。設定同期で使う CNG SHA-256 の API は Windows 10 にあります。同じホストとユーザーでも、別セッション・権限の違い、NTFS 以外の実媒体での設定同期は実地未検証です。 [BCryptHash の要件](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcrypthash)、[設定 I/O 同期規約](SETTINGS-IO-SYNCHRONIZATION.md)

## 過去の検証記録

対象sourceはWindows x64、D3D11、Windows.Graphics.Captureを用います。
cursor制御APIのWindows 10 version 2004/build19041以降は検証下限候補であり、全環境での動作保証ではありません。
DisplayInformation interopはWindows 11 build22621以降を前提とし、古い環境では限定したDisplayConfig/DXGI fallbackを含みます。
Windows 11 24H2/build26100以降のConsole Allocation Policyを、Windows 10でも同等と推定しません。
これらは既存sourceのAPI設計上の境界で、最低対応OSの実測保証ではありません。今回の文書整備では実機検証を追加していません。

新S1はMSVC x64 / Ninja single-config / Release / Privacy ON / profile v2のみを公開支援経路とします。
Source-only snapshot時点の検証は静的・script mode/CLI・隔離fixtureまででした。その後R1で実依存Install・ValidateInstalled・Shipping configure・製品Release buildに成功し、R2で18 PEを限定受入しました。fixtureの成功と実製品の受入は別の証拠です。製品build source revisionは`93545d458da33cef397fb77458336ea06339dcb6`です。
旧構成にあったWindows 10の限定USER-REPORTED成功や別PC成功をS1へ転用しません。

R2の独立実測はWindows 11 25H2 x64 build 26200.9711、SDR/fallback経路、Runtime 14.51.36247.0です。所有HWNDのWindow Capture、772×546 PNGのFile保存、正常終了を確認しました。これを最低必要OS/Runtime版や全環境の保証にはしません。
現在のv4非公開候補はWindows x64 Portable、Release /MD、外部VC++ Runtime x64、未署名、インストーラなしで、同じ18 PEを維持しています。必要環境と導入案内は[Portable README](../packaging/binary/README-portable-s1-release-v3.md)、権利表示は[Binary NOTICE](../packaging/binary/THIRD-PARTY-NOTICES-S1-BINARY-RELEASE-v3.md)を参照してください。非公開候補の技術受入はGitHub公開の承認ではありません。

captureは実capability・target・color・topology等を確認し、不明/不整合を拒否する設計です。
HDR/WCG、複数monitorをまたぐRegion、全driver/GPU/DPI/アクセシビリティ/codec組合せの保証はありません。
[外部PC検証表](OTHER-PC-VALIDATION.md)は旧Source-only時点の未実施表として保持します。その後、同じ18 PEを含むR3候補の別PCでの展開・起動・capture・保存成功がユーザーから申告されています。OS/Runtime版、capture mode、保存形式、現地hash照合は未記録です。R4-D最終ZIP全体の別PC検証ではなく、全GUI・Clipboard実書込み・Region/Monitor・全codec/profile・Portable設定復元・cleanup等の全面受入にも拡張しません。
[利用仕様](USAGE.md)と[構築制限](../BUILDING.md)を確認してください。
