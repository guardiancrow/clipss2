# S1 構築入口と検証ガイド

対象はShipping Privacy v2 / MSVC x64 / Ninja single-config / Releaseのみです。
Debug、通常OFF、v1、Visual Studio/Ninja Multi-Configはこの公開経路で拒否します。
Install経路は実装済みですが、実依存install・Shipping configure・製品buildの受入は未実施です。

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
PlanOnlyのPASSは実source checkout、派生bytes、既存outputの所有権や再利用を承認しません。
固定skcmsの[権利確認](docs/SKCMS-LICENSE-REVIEW.md)はv3 source-development NOTICEへ反映済みです。MS-003確認を含むInstall経路の手順は本文書を参照してください。

managed sourceの固定4 checkoutは[中立origin](cmake/privacy/managed-origin-s1-v1.json)に記載します。
[取得script](tools/prepare_jpegli.ps1)は明示的なGit/network取得を行う別操作です。実行前に取得条件を確認し、既存source/cacheを保全してください。
原sourceが自分のbuild/managed-jpegli-srcにある場合の検証:

```powershell
pwsh -NoProfile -File tools/derive_shipping_privacy_s1.ps1 -Mode VerifyOnly -Profile v2 -Scope s1-v1 -Privacy ON -Configuration Release -Architecture x64
```

新規所有rootで明示Deriveすればv2 source transformationを行います。出力はbuild/managed-jpegli-s1-v2で、既存rootは上書きしません。
検査された1481原tupleとv2 postimageは保持しますが、これは製品buildではありません。

原sourceが未取得の場合の明示取得と、原source受入後の派生生成は別操作です。既存rootがないことと取得条件を確認した後だけ実行します。通常configureは取得しません。

```powershell
pwsh -NoProfile -File tools/prepare_jpegli.ps1
pwsh -NoProfile -File tools/derive_shipping_privacy_s1.ps1 -Mode Derive -Profile v2 -Scope s1-v1 -Privacy ON -Configuration Release -Architecture x64
```

[Install入口](tools/build_shipping_dependencies_s1.ps1)は、上記の原sourceと派生sourceが準備・検証された後に実行します。
**MS-003のBuild Tools使用資格確認が先です。** `-BuildToolsEligibilityConfirmed`は呼出者による確認済みの表明であり、scriptによる使用権の審査・付与ではありません。未確認ではこの引数を付けず、Installを実行しないでください。

以下は確認完了後の将来実行用です。R0 Remediationでは実行していません。

```powershell
pwsh -NoProfile -File tools/build_shipping_dependencies_s1.ps1 -Mode Install -Profile v2 -Scope s1-v1 -Privacy ON -Configuration Release -Architecture x64 -BuildToolsEligibilityConfirmed
```

Installは原4 checkoutのpin/clean/Git tree/1481 tupleと派生1481件を再検証し、新規の専用vcpkg checkoutを公式URIから固定baselineで取得します。
registry由来13件は固定checkoutのport treeに照合します。overlay由来のhighway・libjxl・libyuvは、固定28入力のraw hashとoverlayのfile/directory閉集合、選択されたport定義のname/version/port-versionを照合します。libjxlは固定overlayの0.12.0を使用し、registryの別versionとのtree差異は拒否理由にしません。
[vcpkg公式の選択順](https://learn.microsoft.com/en-us/vcpkg/concepts/overlay-ports)はCLI、config、環境変数、registryです。S1ではCLIとconfigを同じ固定overlay rootに限定し、子processの継承overlay環境を除外します。追加portやroot直下の単一port定義も固定閉集合に含まれないため拒否します。
固定checkout内のbootstrapでvcpkgを取得後、元のmanifest・v2 overlay・専用target/host tripletで実installします。Install直前と受入時にも固定入力を再検証します。
継承されたvcpkg/Git/CMakeの上書き環境を除き、binary/asset cacheをclear、downloads/buildtrees/packages/registry cache/一時領域を新規専用rootへ限定します。旧生成物をコピーする入口はありません。
CLIのroot指定は[vcpkg公式オプション](https://learn.microsoft.com/en-us/vcpkg/commands/common-options)に従います。実bootstrap/tool/SDKとの接続はまだ実証していません。

依存領域にはUNADMITTEDの所有記録を作ります。既存vcpkg/依存/installed/製品rootは空でも再利用せず、読み取り検査後に拒否します。
原sourceと派生sourceは既存の固定内容検証に合格したものを使います。それ以外の既存rootに対するresume、削除、上書き、自動修復は行いません。途中失敗時は原因のstageを報告し、partial outputとlogsを保全します。再試行には別途その領域の確認が必要です。

失敗済みrootがある場合、別名のInstall rootへの切替は固定output policyに反するため行いません。再試行前に、稼働中processがないこと、対象がworkspace配下の正規pathでreparseでないこと、ownerのsourceInputsSha256・UNADMITTED/reuseAllowed=false、vcpkgの固定HEAD/clean状態、installedの内容とmarker/receiptの有無を読み取り確認します。不明な所有物や整合性の不一致があれば停止します。
確認後の退避は別途明示的に実施する操作です。新規に所有する一意の保全先をworkspaceのbuild配下に作り、既存の3 root（shipping-vcpkg-s1-v2、shipping-dependencies-s1-v2、vcpkg_installed-s1-v2）を、全移動元・移動先の絶対pathがworkspace内であることと宛先未存在を確認してから、PowerShellのLiteralPath指定で上書きせず移動します。owner、logs、未受入payloadを丸ごと保持し、成功済みとは扱いません。3つの固定rootがすべて不在であることを確認するまでInstallを開始しません。
managed-jpegli-srcとmanaged-jpegli-s1-v2は退避対象に含めず、上記VerifyOnlyでidentityが一致すればそのまま使用します。再取得・再Deriveは不要です。この手順は自動退避や既存rootのresumeを許可するものではありません。

vcpkg終了code=0だけでは受入しません。固定入力・managed bytes・vcpkg source/toolを再確認し、16 packageのstatus/version/triplet、package listと実payloadを検査します。
その後、全installed fileの相対path/size/SHA-256をreceiptへ記録・照合します。markerは依存領域にCreateNewで完全に書き終えた後、最後に`share/clipss2-source-profile-s1-v1.json`へ上書き禁止のatomic moveで公開します。
configure側のValidateInstalledもmarkerに加えてstatus/payload/receiptを要求し、余剰fileや内容改変を拒否します。
marker/receiptは自己記録の整合性確認です。ABI互換性、信頼済みbinary identity、署名、権利承認の証明ではありません。fixtureの成功を実install済みに読み替えないでください。

```powershell
pwsh -NoProfile -File tools/build_shipping_dependencies_s1.ps1 -Mode ValidateInstalled -Profile v2 -Scope s1-v1 -Privacy ON -Configuration Release -Architecture x64 -InstalledDir "$PWD/build/vcpkg_installed-s1-v2"
```

[preset](CMakePresets.json)のwindows-x64-shipping-v2は上記の実受入後にconfigureする入口です。このsource cutから実configure/buildは未実施です。
専用rootはbuild/shipping-vcpkg-s1-v2、build/shipping-dependencies-s1-v2、build/vcpkg_installed-s1-v2、build/managed-jpegli-s1-v2、build/windows-x64-s1-v2です。
旧f3、旧v1/v2、通常Debug/Releaseや他のinstalled/cacheをS1へ混用しません。

[NOTICE検査](docs/SOURCE-S1-NOTICES.md)は独立したread-only操作です。
source-release宣言の構造受入は実Install/build・ABI/runtimeの受入ではありません。これらは未実施/未実証です。正式source承認は特定snapshotに限り、別のOwner admissionと公開許可に従います。
固定28入力と旧v2 NOTICE metadataのbytesは維持します。skcmsの権利情報は新v3 NOTICE世代のhash連鎖とPublicationView集合へ再固定しました。新しい正式公開時には全source集合の再固定とOwner判断が必要であり、旧snapshotの承認を引き継ぎません。

実installやcompilerを使わない検証:

```powershell
pwsh -NoProfile -File tests/s1_dependency_install_test.ps1 -EvidenceRoot "$PWD/build/r0-remediation-tests"
pwsh -NoProfile -File tests/s1_source_contracts_test.ps1 -EvidenceRoot "$PWD/build/r0-remediation-tests/origin"
```

これらは新規の隔離fixture領域だけを書き込み、削除しません。Installの外部command成功/失敗とmanaged実bytes検証は明示的なtest doubleであり、実取得・bootstrap・installの受入ではありません。
既存`tests/s1_source_contract_test.ps1 -ContractOnly`は、固定入力と必要なtool/CMake fileをコピーした隔離`-Root`に対して使います。Git metadataや既存installed treeがあるRootは拒否します。実checkout・派生source・CMake接続はこのmodeでは検査しません。
