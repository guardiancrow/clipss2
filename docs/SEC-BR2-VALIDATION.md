# SEC-BR2 — v0.1.1公開候補のローカル検証

検証日：2026-10-30（日本時間）。**Owner受入確認へ進める：YES。正式公開承認：NO。**
Source・Binary・Packaging・完全NOTICE・条件案内とローカル検証を準備した。
push、PR、main merge、tag、GitHub Release作成、旧asset変更は実施していない。

## 1. 開始状態とSEC-BR1レビュー

指定checkout `build/sec-br1-public/` のGit root、origin
`https://github.com/guardiancrow/clipss2.git`、branch
`fix/brotli-cve-2026-111497`、HEAD
`d5ded1bfc0ee3bd44d0dcb11cd5f0bbccc8c35ed` が一致し、開始working treeはcleanだった。
修復・checkout切替・親内部checkoutの変更はしていない。

SEC-BR1のcommit差分は報告どおり21ファイル、親は
`e73552453ddb643036f7b0e95d9582738be6876c`。以下を原commitと実証拠で確認した。

| SEC-BR1差分 | レビュー結果 |
| --- | --- |
| `BUILDING.md` | 新入口・歴史的契約の説明。過去承認を変更していない |
| `CMakeLists.txt` | Brotli runtime testのscope限定追加のみ |
| `cmake/ShippingPrivacy.cmake` | BR1への明示分岐。旧S1を破壊しない |
| `THIRD-PARTY-NOTICES-SEC-BR1.md` | 差分NOTICEとして正確。完全配布NOTICEではない |
| `cmake/security/sec-br1/.gitattributes` | raw bytes固定 |
| `cmake/security/sec-br1/ShippingPrivacy.cmake` | MSVC x64 Release・専用rootと入力検証 |
| `cmake/security/sec-br1/source-inputs-v1.json` | EF0EB…FE32、package 16、installed 19 / product 17を確認 |
| `cmake/security/sec-br1/vcpkg.json` | 旧manifestと同bytes、baseline維持 |
| `cmake/security/sec-br1/vcpkg-configuration.json` | 新overlay優先、旧v2 overlay併用 |
| `vcpkg-overlays/security-sec-br1/.gitattributes` | port原bytes固定 |
| 同 `brotli/vcpkg.json` | 1.2.1、既存host tools、MIT |
| 同 `brotli/portfile.cmake` | official source/checksumとtree identity一致 |
| 同 `brotli/install.patch` | official blobと一致。新しい独自patchなし |
| 同 `brotli/pkgconfig.patch` | official blobと一致 |
| 同 `brotli/usage` | actual imported CMake targetと整合 |
| `tools/ShippingProfileSecBR1.ps1` | fresh root・closed set・status/receipt・DLL照合を確認 |
| `tools/build_shipping_dependencies_sec_br1.ps1` | mode選択と副作用境界を確認 |
| `tests/sec_br1_contract_test.ps1` | synthetic 22件。実ABI/実buildを証明するものではない |
| `tests/brotli_sec_br1_test.cpp` | actual version/round trip。不正header試験はCVE PoCではない |
| `docs/SEC-BR1-BROTLI.md` | 依存・到達性の制限を記載。一般ABI保証なし |
| `docs/SEC-BR1-VALIDATION.md` | receipt/marker/JUnit/PE inventoryの実hashと一致 |

実ValidateProductもPASS。18 PEの初期privacy走査で見つかった `/home/` は
`src/platform/windows/error_diagnostics.cpp` の6-byte検出sentinelで、利用者の実パスではなかった。
重大な矛盾・採用阻害事項は検出しなかった。自己記録receiptを署名済みprovenanceとは扱わない。
レビュー結果はignored `build/sec-br2/br1-review.json` に保全した。

## 2. 新しいSource / Binary identity

| 項目 | identity |
| --- | --- |
| 製品build source commit | `6c4f48f8908746a7b06ab046ab715a51cbab86f8` |
| 製品build source tree | `011853611d980769ff6ad9814b03c5c81de5a17a` |
| Build ID | `clipss2-0.1.1-sec-br2-rc1` |
| 新build inputs SHA-256 | `5CC40FDA714E820DDFD6A14527E68FA3A2A448E60AB7748EEEFE6A6202396560` |
| Package profile SHA-256 | `D40B66640F03E6ADFD492E9DE26991AC139D482719ABDC7FE2AC917C91107217` |
| 依存BR1 input SHA-256（不変） | `EF0EB35F031CE4424408709D0871C2A72F0FBF109A08C7295F911F40B4FFFE32` |
| 依存receipt SHA-256（不変） | `0D84E144C82AA74208CF57B93271D1C33EF0724643E24C947E21338EFD617D35` |
| EXE SHA-256（803,840 bytes） | `4E4966E3E90EE0C8DD8BD117906FB1BE0DE2F799EC9B3707D4BCE3332781BB83` |

EXEは新sourceから新規product rootへbuildし、FileVersion 0.1.1.0 / ProductVersion 0.1.1、
`--help`のVersion 0.1.1を確認した。DLLは本phaseで再buildせず、明示的に許可されたSEC-BR1の
固定入力・受入済みdependenciesを検証して使用した。v0.1.0 ZIP/EXE/DLLから取得していない。
243 compiled-source bindingsと18 PE inventoryを新build receiptへ結合し、Packaging時にも再照合した。

後続Packaging commitはEXEの再build revisionではない。最終Source ZIPはその最終ローカルcommitから
別に生成し、全entryをGit blobへ照合する。Source ZIPの最終commit・size・SHA-256は、自己参照を避けて
ignored `build/sec-br2/delivery.json` と最終返信へ記録する。

## 3. 変更ファイルと固定契約への影響

本phaseは27ファイルを変更・追加した。依存versionやcodec機能を一括更新していない。

| ファイル | 理由 |
| --- | --- |
| `README.md`, `README.en.md`, `BUILDING.md` | 未公開v0.1.1候補の入口を追加。公開済みv0.1.0情報は保持 |
| `CMakeLists.txt`, `src/main.cpp`, `src/platform/windows/product_version.rc.in` | project/resource/helpのversion整合、CLI引数と機能は維持 |
| `cmake/ShippingPrivacy.cmake` | SEC-BR2への明示分岐 |
| `cmake/security/sec-br2/.gitattributes`, `ShippingPrivacy.cmake`, `build-inputs-v1.json` | 新version・product root・依存継承の別契約 |
| `cmake/security/sec-br2/br1-preimages/CMakeLists.txt`, `cmake/ShippingPrivacy.cmake`, `README.md` | BR1が固定した2入力の原bytesと再構成の説明 |
| `tools/ShippingProfileSecBR2.ps1`, `tools/build_security_release_sec_br2.ps1` | 歴史的view・現source・受入dependencies・新productの検証/構築 |
| `tests/sec_br2_contract_test.ps1` | 新旧契約境界、tamper/extra port/overwrite拒否 |
| `packaging/sec-br2/MICROSOFT-COMPONENT-TERMS.txt`, `README.md.in`, `THIRD-PARTY-NOTICES.md.in` | 新versionの条件文書、README、完全NOTICE |
| `packaging/public/profile-sec-br2-v1.json`, `packaging/SecBR2Package.psm1` | 47ファイルの新固定profileとcandidate reader/writer |
| `tools/package_security_release_sec_br2.ps1`, `tests/sec_br2_package_test.ps1` | Packaging入口、canonical ZIP/安全境界のfixture |
| `docs/SEC-BR2-BUILD.md`, `RELEASE-NOTES-0.1.1-CANDIDATE.md`, `SEC-BR2-OWNER-ACCEPTANCE.md`, `SEC-BR2-VALIDATION.md` | 再構築境界、条件の事前案内、実結果、手動受入 |

BR1がhash固定するCMake入力2件を新version用に変更したため、BR1のledgerを現sourceへ再hashしなかった。
原commitの2 preimageと不変入力から歴史的contract/test viewを構成し、BR1テストをそこで実行した。
旧S1 raw28入力、BR1 ledger/overlay/receipt/marker、v0.1.0 profile/条件文書/原LICENSEは不変。
新規環境ではSEC-BR1 sourceで依存を構築してから同じcheckoutでSEC-BR2へ進む。
Source ZIP単独での依存初回Installや独立環境のbitwise再現性を実証したとは主張しない。

## 4. Brotliと構築・テスト

Brotli 1.2.1 official port treeは `0702c3d1505dae90ff22bcbdc68921c74bb8651f`、baselineは
`04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4`。libjxl 0.12.0と他15 packageは維持した。
新productの全17 DLLはBR1 installed receiptと同bytes。actual runtime testはdecoder/encoder version
`0x01002001`を確認し、PE importはEXE → jxl → brotlidec/brotlienc → brotlicommonを確認した。

| DLL | SHA-256 |
| --- | --- |
| brotlicommon.dll | `31A88D04504896E82A035B03F5ECA60DEE97789707BEC4ECFA1D2BC223340643` |
| brotlidec.dll | `72C3B73E4BFAF02BD09A770E4382C5CDF7764FAFC735118E7D18B6B785810502` |
| brotlienc.dll | `D82A81EFA007A8C28D1CD067E446BFC1A60310694934E5F45636DD42AC9EDF3C` |

環境：Windows x64、OS API description `Microsoft Windows 10.0.26200`、MSVC 14.44.35207 /
compiler 19.44.35229.0、SDK 10.0.26100.0、Ninja single-config、Release /MD。
外部の4 VC Runtime DLL実測は14.51.36247.0。これらを最低OS/Runtime版とは断定しない。

| 実施項目 | 結果・範囲 |
| --- | --- |
| SEC-BR1 21ファイルと実証拠のレビュー | PASS、重大事項なし |
| 新BR2 contract fixtures | PASS 8/8 |
| 歴史的BR1 contract fixtures | PASS 22/22 |
| 旧S1 dependency contract fixtures | PASS 71/71 |
| 固定dependencies・原4 checkout/1481 tuples・v2派生 | PASS、既存証拠を再検証。依存再Installは実施しない |
| SEC-BR2 configure / full Release build | PASS、新product root |
| 全登録CTest | PASS 38/38、FAIL 0、SKIP 0、JUnit time 9秒 |
| Brotli実version / libjxl / JPEG XL可逆・非可逆3 profile | PASS、実DLL・保存/検証処理を含む |
| PNG / JPEG / WebP / AVIFとその他native model tests | PASS、上記38件に含む |
| ZIP/filesystem fixtures | PASS 11/11、duplicate/traversal/link/date/hash/extra/overwrite等 |
| 新EXE version、18 PE x64/type/import/hash | PASS、Debug CRT importなし |
| Package create / validate / 再Create拒否 | PASS、再Create前後のZIP hash不変 |
| ZIP展開47ファイルとhash、Portable marker | PASS |
| 展開EXEの別cwdからの`--help` | PASS、Version 0.1.1、exit 0、展開app directory不変 |
| Source差分・47 payload files・18 PEのprivacy | 検査範囲内で問題を検出せず |
| 常駐/設定/選択GUI、実capture、Clipboard publication、別PC/OS/HDR等 | SKIP、Owner手動受入待ち |
| CVE exploit PoC / fuzz campaign / bitwise独立再build | SKIP、未実施 |

CTestのhidden console/所有windowと失敗注入はnative fixtureの実行であり、実画面captureや
Clipboard publicationの受入ではない。テスト用EXE・audit fixtureを候補ZIPへ含めていない。

初期fixture実装では、未作成Rootのpath解決、module import後のglobal helper可視性、
OrderedDictionaryのMeasure-Object上限計算でFAILした。fixture準備と新readerの明示的な
64-bit size加算へ修正し、別run rootで再検証した。旧readerは変更していない。
初期FAILと新結果は別UUIDの証拠として保全し、未実施をPASSへ置き換えていない。

## 5. ZIP・LICENSE・条件案内

| 項目 | 候補 |
| --- | --- |
| ZIP | `build/sec-br2/candidate/clipss2-0.1.1-windows-x64-portable.zip` |
| サイズ | 7,970,478 bytes |
| SHA-256 | `A13DF0E714A061C367548DD5296C3C2D1A7CBCE81E2030FE2BB8C9AD1DE02169` |
| 内容 | EXE 1 + DLL 17 + 第三者原文23 + LICENSE/README/完全NOTICE/marker/条件文書/BUILD-IDENTITY = 47 |
| 固定ZIP timestamp | 2026-10-30 00:00:00 +09:00 |
| 公開状態 | LOCAL CANDIDATE / OWNER PENDING、公開許可flagsはfalse |

v0.1.0から継承したのは23点の原LICENSE/PATENTS・製品MIT・Portable marker等の原文で、旧PEではない。
Brotliの1084-byte MIT原文は1.2.1 upstream・旧原文・installed・ZIPで同一、SHA-256は
`3D180008E36922A4E8DAEC11C34C7AF264FED5962D07924AEA928C38E8663C94`。
新しい完全NOTICEは全component表、IJG credit、Apache/CC0/PATENTSと変更告知を保持している。

Microsoft条件文書はv4の対象version見出しだけを0.1.1へ変更し、§1〜§7の本文は完全に同一。
同じOwner・MSVC/SDK・Release /MD・外部Runtime方針の既存資格/リンク判断を継承した。
新たなMicrosoftコード/Runtime同梱・リンク方式・条文の実質的な変更トリガーはない。
旧SDK-2/G3や全受領者の同意成立を新候補へ自動継承していない。
本文案の取得前案内、ZIP内条件全文、対象SDK code限定と独立MIT/OSSの分離を確認した。
正式Releaseの条件permalink・取得link・実配置と新identityへのOwner G3は次phaseの残件。

## 6. Privacy・保全・次phase

新規/変更source、全47 payload bytesのUTF-8/UTF-16、18 PEのASCII/UTF-16、PDB basenameと
importを検査した。個人絶対path・host・資格情報patternの新規混入は検出しなかった。
必要なcopyrightや第三者の帰属をPIIとして削除していない。source/ZIPにbuild/cache/log/
PDB/.lib/実設定/画像/内部監査本文を追加していない。profileは必要な証拠hashと相対pathだけを参照する。
パターン走査・差分レビューの範囲の結果であり、漏えい不存在や全Git履歴の網羅監査を保証しない。

`CMakeLists.txt`と歴史的manifestのversion差、旧preset/NOTICEの固定version、
`README.md`の公開済みv0.1.0と未公開v0.1.1の時点差は新ガイド/候補表示で区別した。
旧`BUILDING.md`・`cmake/privacy/README.md`や承認記録の歴史的記述を新結果で上書きしていない。

SEC-BR1 commit、旧source input/port tree/検証証拠、Owner判断、v0.1.0 Source/Binary identity、
tag・GitHub Release・assetを保全した。全生成先は本checkoutの新規build領域で、削除・上書きなし。
親内部checkout、local main/origin/mainを変更していない。

証拠はignored `build/sec-br2/` の `br1-review.json`、`configure-identity.json`、
`build-acceptance.json`、`public-build-identity.json`、`native-tests.xml`、`ctest-summary.json`、
`candidate-audit.json`、`tests/` と候補owner/acceptanceに保全。build receipt SHA-256は
`E603FDE44A2A7529DE5371898B38F27A6192B02119F585ECD74621B281B3B71C`、
JUnit SHA-256は `A4459F72A649174D54BF0C9C3ADCD9242EB52250C29E929718832107F144C469`。

次phaseは[Owner受入](SEC-BR2-OWNER-ACCEPTANCE.md)、新identityの承認、公開Source集合/条件permalinkの
最終照合、明示承認後のpush・merge・tag・正式Releaseである。実機受入未実施のまま正式公開GOにはしない。
今回の最終ローカルHEAD・clean状態とSource archive identityは最終返信で報告する。
