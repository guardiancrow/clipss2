# S1 構築入口と検証ガイド

対象はShipping Privacy v2 / MSVC x64 / Ninja single-config / Releaseのみです。
Debug、通常OFF、v1、Visual Studio/Ninja Multi-Configはこの公開経路で拒否します。
旧Source-only snapshot時点では実Install/build未実施でした。その後R1で実依存Install・ValidateInstalled・Shipping configure・製品target `clipss2`のRelease buildに成功し、R2で18 PEを限定受入、R4-Dで45ファイルの非公開候補ZIPを受け入れました。
製品build source revisionは`93545d458da33cef397fb77458336ea06339dcb6`です。文書・Packaging用の後続commitは製品再ビルドrevisionではありません。その後Phase 3-Dで同じ18 PEを含む46ファイルのv4非公開候補を受け入れました。Windows x64 Portable、Release /MD、外部VC++ Runtime x64、未署名で、GitHub Releaseの公開は未承認です。[Portable README](packaging/binary/README-portable-s1-release-v3.md)、[Binary NOTICE](packaging/binary/THIRD-PARTY-NOTICES-S1-BINARY-RELEASE-v3.md)、[v4 Release Notes／本文テンプレート](docs/RELEASE-NOTES-0.1.0-V4.md)を参照してください。

想定する環境はWindows x64、Visual Studio 2022/Build ToolsのMSVC C++23とWindows SDK、CMake 3.28以降、Ninja、Git、PowerShell 7.2以降です。
今回のS1構築で使用したSDKは10.0.26100.0、MSVC toolsetは14.44.35207/compiler 19.44.35229.0です。使用資格は下記の既存判断を継承します。SDKの下流条件の履行と公開承認は別の残件です。

## G2判断の引継ぎ（2026-10-23）

記録IDは`clipss2-g2-inheritance-2026-10-23`です。承認根拠は、Owner提供の外部引継ぎメモ`clipss2-g2-handoff-2026-10-23.md`と、同日の「第3段階再開：固定契約の保護とG2判断の引継ぎ」というOwner指示です。Skillと参照資料v1.3.0に従います。本項はプロジェクト内の引継ぎ記録で、新たな法務意見でも、メモそのものを正式承認記録とみなすものでもありません。

判断・承認責任者はOwner／開発者本人です。MSVC-1は法務相談を経た解釈をOwnerが採用したという引継ぎに基づきます。相談担当者の氏名、相談原記録の保全先、条文対応の詳細は本項へ提供されていないため創作せず、Owner側の原記録との対応付けを残します。同条件の採用済み判断を再審査するための要求ではありません。

| 論点 | 継承する判断と適用範囲 | 未履行・適用限界 |
| --- | --- | --- |
| G1 | 技術確認済み。R1/R2の証拠を継承し再解析しない | 全構成・全環境の保証へ拡張しない |
| MSVC-1 | 標準Release /MDのCRT startupについて採用済み合理的契約解釈をINTERPRETED-APPROVEDとして引き継ぎ、通常監査を終了 | Microsoftの個別明示許諾ではない。異なる静的コード・リンク構成へ自動拡張しない |
| TOOLS-1 | 本人確認済みCommunity資格と、同じ本人・用途のBuild Tools補助利用はCLEAR | 第三者CI、別組織・共有ホストへ自動拡張しない |
| SDK-1 | SDK 10.0.26100.0の指定パスに対する既存条件付き判断を継承 | SDK-2の履行完了を意味しない |
| SDK-2 | CLEAR IF。Release本文・READMEの事前案内とEXE同階層のZIP内条件文書、起動時同意UIなしを優先候補とする | 文面の採用、提示・合意要求方式の責任者判断、配布者の継続義務、実物への反映・受入が未了 |
| G3 | 公開未承認 | G2の判断継承や技術受入から公開GOを自動発行しない |

射程はclipss2の個人開発、自作部分MIT、当該18 PE、標準Release /MD、Runtime非同梱、GitHub ReleasesでのPortable ZIP配布準備です。通常のcommitやAI/スレッド変更で再起票しません。契約・利用主体・MSVC/SDK・リンク構成・Microsoft由来同梱物・配布経路・下流条件の実質的変更、重要な新証拠がある場合だけ差分を確認します。

[R4-D記録](docs/R4-D-RELEASE-DECISION.md)と旧profileのMS-002-L留保は当時の固定記録として維持します。今回の引継ぎで旧profile・Reader・公開承認フラグを書き換えません。G2全体の履行完了・G3の公開承認は宣言しません。

## 固定入力と構築手順

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

以下は資格確認済みの呼出者が、新規所有領域で再構築する場合の手順です。R0 Remediation時点では未実行、R1では実行・受入済みです。同じ本人・用途の確認済み資格判断を継承し、既存のR4-D候補のために再実行しません。

```powershell
pwsh -NoProfile -File tools/build_shipping_dependencies_s1.ps1 -Mode Install -Profile v2 -Scope s1-v1 -Privacy ON -Configuration Release -Architecture x64 -BuildToolsEligibilityConfirmed
```

Installは原4 checkoutのpin/clean/Git tree/1481 tupleと派生1481件を再検証し、新規の専用vcpkg checkoutを公式URIから固定baselineで取得します。
registry由来13件は固定checkoutのport treeに照合します。overlay由来のhighway・libjxl・libyuvは、固定28入力のraw hashとoverlayのfile/directory閉集合、選択されたport定義のname/version/port-versionを照合します。libjxlは固定overlayの0.12.0を使用し、registryの別versionとのtree差異は拒否理由にしません。
[vcpkg公式の選択順](https://learn.microsoft.com/en-us/vcpkg/concepts/overlay-ports)はCLI、config、環境変数、registryです。S1ではCLIとconfigを同じ固定overlay rootに限定し、子processの継承overlay環境を除外します。追加portやroot直下の単一port定義も固定閉集合に含まれないため拒否します。
固定checkout内のbootstrapでvcpkgを取得後、元のmanifest・v2 overlay・専用target/host tripletで実installします。Install直前と受入時にも固定入力を再検証します。
継承されたvcpkg/Git/CMakeの上書き環境を除き、binary/asset cacheをclear、downloads/buildtrees/packages/registry cache/一時領域を新規専用rootへ限定します。旧生成物をコピーする入口はありません。
CLIのroot指定は[vcpkg公式オプション](https://learn.microsoft.com/en-us/vcpkg/commands/common-options)に従います。当該S1の実bootstrap/tool/SDK接続はR1で受け入れました。別の環境・版の成功を保証するものではありません。

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

[preset](CMakePresets.json)のwindows-x64-shipping-v2は上記の実受入後にconfigureする入口です。R1で上記製品revisionの実configureとRelease buildを受け入れました。
専用rootはbuild/shipping-vcpkg-s1-v2、build/shipping-dependencies-s1-v2、build/vcpkg_installed-s1-v2、build/managed-jpegli-s1-v2、build/windows-x64-s1-v2です。
旧f3、旧v1/v2、通常Debug/Releaseや他のinstalled/cacheをS1へ混用しません。

[NOTICE検査](docs/SOURCE-S1-NOTICES.md)は独立したread-only操作です。
source-release宣言の構造受入と、R1/R2の実Install/build・限定runtime受入は別の証拠です。独立再ビルドのbitwise再現性や一般的なABI互換性は未実証です。正式source承認は特定snapshotに限り、別のOwner admissionと公開許可に従います。
固定28入力と旧v2 NOTICE metadataのbytesは維持します。skcmsの権利情報は新v3 NOTICE世代のhash連鎖とPublicationView集合へ再固定しました。新しい正式公開時には全source集合の再固定とOwner判断が必要であり、旧snapshotの承認を引き継ぎません。
raw hash固定された[Privacy README](cmake/privacy/README.md)の未実施記述は旧snapshot時点の歴史的説明として保持します。旧PublicationViewは295ファイル、第2段階HEADのtag archive対象は323ファイルです。今回の文書4件はpathを変えず更新しますが、そのbytesを含む新しい公開集合へのOwner承認は別途必要です。B区分13資料は再現性と履歴を維持するため公開する案を第一候補とし、未承認の公開操作や旧snapshot承認の継承には読み替えません。

実installやcompilerを使わない検証:

```powershell
pwsh -NoProfile -File tests/s1_dependency_install_test.ps1 -EvidenceRoot "$PWD/build/r0-remediation-tests"
pwsh -NoProfile -File tests/s1_source_contracts_test.ps1 -EvidenceRoot "$PWD/build/r0-remediation-tests/origin"
```

これらは新規の隔離fixture領域だけを書き込み、削除しません。Installの外部command成功/失敗とmanaged実bytes検証は明示的なtest doubleであり、実取得・bootstrap・installの受入ではありません。
既存`tests/s1_source_contract_test.ps1 -ContractOnly`は、固定入力と必要なtool/CMake fileをコピーした隔離`-Root`に対して使います。Git metadataや既存installed treeがあるRootは拒否します。実checkout・派生source・CMake接続はこのmodeでは検査しません。
