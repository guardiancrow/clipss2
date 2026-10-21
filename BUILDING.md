# S1 構築入口と検証ガイド

対象はShipping Privacy v2 / MSVC x64 / Ninja single-config / Releaseのみです。
Debug、通常OFF、v1、Visual Studio/Ninja Multi-Configはこの公開経路で拒否します。
この文書は実行可能な一括build手順の完成を宣言しません。

想定する環境はWindows x64、Visual Studio 2022/Build ToolsのMSVC C++23とWindows SDK、CMake 3.28以降、Ninja、Git、PowerShell 7.2以降です。
過去の別構成の参照環境はSDK 10.0.26100.0、MSVC toolset 14.44.35207/compiler 19.44.35229.0でした。新S1のtoolchain受入実績ではありません。
SDK/Build Toolsの取得・利用条件、将来のMicrosoft再頒布条件は別途確認が必要です。

固定baselineは[vcpkg manifest](vcpkg.json)の04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4。
[新config](vcpkg-configuration.json)はv2 overlayだけを選択します。公式libjxl 0.12.0由来のportとHighway/libyuvの変更は[帰属](cmake/privacy/ATTRIBUTION.md)を参照してください。
16 packageと実28入力は[source-inputs](cmake/privacy/source-inputs-s1-v1.json)に固定されています。

現在実行可能なread-only source plan:

```powershell
pwsh -NoProfile -File tools/build_shipping_dependencies_s1.ps1 -Mode PlanOnly -Profile v2 -Scope s1-v1 -Privacy ON -Configuration Release -Architecture x64
```

これはvcpkg、compiler、configure、installを起動せず、output directoryも作成しません。
既存outputはPRESENT_UNADMITTEDとして扱い、その所有権・ABI・再利用を承認しません。
**Mode=InstallはInstallNotImplementedで停止します。** 依存一式を作る機能はまだ提供しません。

managed sourceの固定4 checkoutは[中立origin](cmake/privacy/managed-origin-s1-v1.json)に記載します。
[取得script](tools/prepare_jpegli.ps1)は明示的なGit/network取得を行う別操作です。実行前に取得条件を確認し、既存source/cacheを保全してください。
原sourceが自分のbuild/managed-jpegli-srcにある場合の検証:

```powershell
pwsh -NoProfile -File tools/derive_shipping_privacy_s1.ps1 -Mode VerifyOnly -Profile v2 -Scope s1-v1 -Privacy ON -Configuration Release -Architecture x64
```

新規所有rootで明示Deriveすればv2 source transformationを行います。出力はbuild/managed-jpegli-s1-v2で、既存rootは上書きしません。
検査された1481原tupleとv2 postimageは保持しますが、これは製品buildではありません。

[preset](CMakePresets.json)のwindows-x64-shipping-v2は将来の構築入口です。
vcpkg専用checkout、実installed tree/新marker、派生sourceの受入が必要で、このsource cutから実configure/buildは未実施です。
marker fixtureの成功をinstall済みとして利用しないでください。偽markerを作るbootstrap手順は提供しません。
専用rootはbuild/shipping-vcpkg-s1-v2、build/shipping-dependencies-s1-v2、build/vcpkg_installed-s1-v2、build/managed-jpegli-s1-v2、build/windows-x64-s1-v2です。
旧f3、旧v1/v2、通常Debug/Releaseや他のinstalled/cacheをS1へ混用しません。

[NOTICE検査](docs/SOURCE-S1-NOTICES.md)は独立したread-only操作です。
source-release宣言の構造受入は実Install/build・ABI/runtimeの受入ではありません。これらは未実施/未実証です。正式source承認は特定snapshotに限り、別のOwner admissionと公開許可に従います。
