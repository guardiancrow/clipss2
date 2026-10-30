# SEC-BR1 ローカル検証記録

検証日：2026-10-30（日本時間）。Brotli 1.2.1への更新とローカル検証はPASS。
v0.1.1公開、push、tag作成、Release/公開ZIPの変更は実施していない。
実装・再構築手順と到達可能性の評価は[構築ガイド](SEC-BR1-BROTLI.md)を参照。

## 作業場所とGit

開始workspaceはremoteなしの別checkoutで、working treeはcleanだった。
指定のShipping Privacy契約が存在せず、公開repoとの履歴分離を確認した。
既存checkoutへ公開remoteを接続せず、ignored `build/sec-br1-public/` に
`https://github.com/guardiancrow/clipss2.git` を新規cloneした。

公開repoの最新mainをGitHub APIと `git ls-remote` で確認し、clone後の
`origin/main` と一致した `e73552453ddb643036f7b0e95d9582738be6876c` から
`fix/brotli-cve-2026-111497` を新規作成した。同名remote branchは存在しなかった。
以降のsource変更はこのbranchだけ。local main/origin/mainは基点のままである。
内部repoのtracked files、branch、HEAD、remote設定を変更していない。

この報告書を含むローカルcommitのHEADは最終返信と `git rev-parse HEAD` で確認する。
既存OwnerのGit identityを用い、公開送信や新たなOwner承認を行わない。

## 方式・影響範囲

既存baselineを維持し、公式vcpkgのBrotli 1.2.1 portを専用overlayへ原bytesで導入。
旧overlayの閉集合・旧28入力を壊さないよう、専用manifest/configとSEC-BR1契約を追加した。
libjxl 0.12.0、他15 packageのversion/port tree、host tools、v2 patch、tripletは不変。
新規installed/cache/derived/product領域で全依存を再構築し、過去のbinaryは再利用していない。

| 固定対象 | identity |
| --- | --- |
| baseline | `04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4` |
| Brotli 1.2.1 official port tree | `0702c3d1505dae90ff22bcbdc68921c74bb8651f` |
| upstream revision | `db24a98aed38a16de78cc05ca15cdd8dc2a0d92d` |
| 新source inputs SHA-256 | `EF0EB35F031CE4424408709D0871C2A72F0FBF109A08C7295F911F40B4FFFE32` |
| 旧S1 source inputs SHA-256（不変） | `80BD9B28EF236F29E31BC12B1D6C16120C9F7E60AC0ACF4F861691C6DF5C64AB` |
| 取得Brotli archive SHA-256 | `98D35D11A78DC453AEBB7D10C3748F3F73FF78C8267C0DC697B8631F67BF7E8F` |

取得archiveは公式portのSHA-512とも一致。新18入力のsize/SHA-256、旧28入力、
official portの5 blob、package status/list/payload、receipt/markerを検査した。
新marker/receiptは旧S1とは別名で、source input hashを結合する。

## 変更ファイル

既存3件を変更、新規18件を追加。旧契約ファイルの一括改名・version置換はない。

| ファイル | 内容 |
| --- | --- |
| `BUILDING.md` | SEC-BR1入口と旧契約の歴史的説明を追加 |
| `CMakeLists.txt` | SEC-BR1だけのBrotli実DLLテストtarget |
| `cmake/ShippingPrivacy.cmake` | 新scopeへの明示分岐。旧S1経路は維持 |
| `cmake/security/sec-br1/.gitattributes` | 新契約bytesの維持 |
| `cmake/security/sec-br1/ShippingPrivacy.cmake` | MSVC x64 Release、専用path/overlay/installed/managed検証 |
| `cmake/security/sec-br1/source-inputs-v1.json` | package・raw入力・19 installed/17 product DLL・専用rootの固定 |
| `cmake/security/sec-br1/vcpkg.json` | 旧manifestと同bytesの専用manifest |
| `cmake/security/sec-br1/vcpkg-configuration.json` | 新Brotli overlay、旧v2 overlayの順序固定 |
| `vcpkg-overlays/security-sec-br1/.gitattributes` | imported portのraw bytes維持 |
| `vcpkg-overlays/security-sec-br1/brotli/vcpkg.json` | 公式1.2.1 definition |
| `vcpkg-overlays/security-sec-br1/brotli/portfile.cmake` | 公式source取得・checksum・構築定義 |
| `vcpkg-overlays/security-sec-br1/brotli/install.patch` | 公式patchを無変更で導入 |
| `vcpkg-overlays/security-sec-br1/brotli/pkgconfig.patch` | 公式patchを無変更で導入 |
| `vcpkg-overlays/security-sec-br1/brotli/usage` | 公式CMake target案内 |
| `tools/ShippingProfileSecBR1.ps1` | source・構築・receipt・DLL集合検証。旧関数は上書きしない |
| `tools/build_shipping_dependencies_sec_br1.ps1` | Plan/Derive/Install/Validate/Configure入口 |
| `tests/sec_br1_contract_test.ps1` | 隔離fixtureによる22契約テスト |
| `tests/brotli_sec_br1_test.cpp` | 1.2.1実API、round trip、不正header拒否 |
| `THIRD-PARTY-NOTICES-SEC-BR1.md` | 1.2.1と公式portの帰属差分。原LICENSE維持 |
| `docs/SEC-BR1-BROTLI.md` | 固定方式、到達可能性、構築手順、制限 |
| `docs/SEC-BR1-VALIDATION.md` | 本記録 |

## 実行結果

環境：既存OwnerのMSVC 14.44.35207/compiler 19.44.35229.0、SDK 10.0.26100.0、
Ninja single-config、Release /MD。資格は既存 `clipss2-g2-inheritance-2026-10-23`
の同一Owner・用途の判断を継承。Microsoft関連の新規の変更トリガーなし。
旧SDK-2/公開承認の適用範囲を新成果物へ自動拡張していない。

| 検証 | 結果 |
| --- | --- |
| 新契約のoffline synthetic tests | PASS 22/22 |
| 旧 `s1_dependency_install_test.ps1` | PASS 71/71 |
| 旧raw28入力、旧manifest/config/presets/overlayの照合 | PASS |
| 原4 checkout / 1481 tuples・新規v2派生1481件 | PASS |
| 固定vcpkg取得/bootstrap、16 package実ビルド | PASS（新領域で再構築） |
| ValidateInstalled・receipt/marker・19 installed DLL | PASS |
| SEC-BR1 configure、製品と選択test target 11件のbuild | PASS（222 build steps） |
| `libjxl_smoke` | PASS（0.12.0 encoder/decoder/runner） |
| `brotli_sec_br1` | PASS（1.2.1 encoder/decoder DLL、round trip、不正header） |
| `jxl_codec` | PASS（可逆pixel一致、入力拒否等） |
| `capture_image_output` | PASS（JPEG XL/AVIFの非可逆3 profile、保存・検証等） |
| `png_codec`, `webp_codec`, `avif_codec` | PASS |
| `jpegli_backend`, `jpeg_backend_policy` | PASS |
| `privacy_diagnostics` | PASS |
| 上記CTest合計 | PASS 10/10、FAIL 0、SKIP 0、5.44秒 |
| ValidateProduct | PASS（17 DLLすべて新installedと同bytes） |
| PE構成・import検査 | PASS（EXE+17 DLLはx64、Debug CRT importなし） |
| Brotli license bytes | PASS（upstream 1.2.1、既存原文、installedで同一） |
| CVE exploit PoC / fuzz campaign | SKIP（未実施） |
| 全CTest・実GUI/capture/Clipboard publication・別PC/OS/HDR全組合せ | SKIP（未実施） |

PE importは `clipss2.exe → jxl.dll → brotlidec.dll / brotlienc.dll → brotlicommon.dll`
を確認。新runtime testは製品と同じapp-local DLLを使用する。
一般的ABI保証・独立環境のbitwise再現性・署名済みprovenanceの検証とは区別する。

## 失敗・修正・証拠保全

最初の依存build自体は成功したが、新検査がinstalledと配布の集合を混同して17本を要求し、
acceptanceでFAILした。既存WebP portの補助2 DLLを含むinstalledは19本、製品配置は17本だった。
初回は成功marker/receiptなし。初稿input hash
`2D27A39403CDC371AF4E9B8885D23BD9345C36A1F10E2E70AA989895CA15689F`
とsource snapshot、出力、logsを `build/sec-br1/failure-snapshot/` 等へ保全した。
集合を分離し、新しい固定root `build/sec-br1-r1/` でcache/binaryを流用せず再構築した。

configureの前段では複数cmake候補の配列連結による起動失敗を修正し、単一toolを選択した。
続くconfigureはsandbox userと取得sourceの所有者相違によるGit拒否でFAIL。
未ビルドのcacheを `build/sec-br1-r1/failed-configure-sandbox/` へ上書きなしで保全し、
移動前後のcache SHA-256一致と全pathのworkspace内・非reparseを確認した。
同じOwnerで再実行してconfigure/build/testsはPASS。Gitの全体設定は変更していない。
fixture初回の旧marker拒否テストでは、正しく拒否されたエラー名と期待値の相違を修正した。
派生生成中の早期検査も、完了後に原source/派生bytesを検証し直した。
これらの初期FAILを最終PASSへ合算・隠蔽せず、失敗出力を残している。

ローカル証拠（build配下、commit対象外）：

| ファイル | SHA-256 |
| --- | --- |
| `build/sec-br1-r1/installed/share/clipss2-sec-br1-installed-v1.json` | `56116DE94EAC39F8B5D7179629ABE0E78C1C2A205542257688268F0239CC688D` |
| `build/sec-br1-r1/installed/share/clipss2-sec-br1-install-receipt-v1.json` | `0D84E144C82AA74208CF57B93271D1C33EF0724643E24C947E21338EFD617D35` |
| `build/sec-br1-r1/native-tests.xml` | `756D83AB4621E8A9E7E52C188E0D66D0F48141460933505D129281B8A24272B3` |
| `build/sec-br1-r1/pe-inventory.json` | `79354448084F754B5476CD7ABE5B00F0EDDF825C2365CFFE6129422E1AF0902C` |

stdout/stderrは `build/sec-br1-r1/dependencies/logs/`、新旧fixture結果は
`build/sec-br1-tests/` と `build/sec-br1-legacy-tests/` に保全。
新しいfixture runごとに別rootを作り、既存結果を上書きしていない。

## 文書・実装の時点差と残件

- `BUILDING.md` の過去の未公開/条件未履行記述、raw固定の `cmake/privacy/README.md`
  の未実証記述と、`README.md` のv0.1.0公開済み状態には時点差がある。
  旧記録を最新承認へ書き換えず、BUILDINGの冒頭と新ガイドで歴史的契約と新経路を区別した。
- 旧baselineの `ports/brotli/portfile.cmake` に `# v1.1.0` の古いコメントがあるが、
  実REFはVERSION展開で、同portのvcpkg.jsonと旧ledgerが1.2.0を選択する。
  旧baselineを変更せず、1.2.1の公式portを原bytesで別途固定した。
- `CMakePresets.json` と旧NOTICE/Packaging profileの1.2.0固定は歴史的identityとして維持。
  新経路と差分NOTICEを明示し、旧profileを新binaryへ流用しない。
  公式patch内の末尾空白を含むcontextもGit blob identityを優先して保持した。
  raw JSONのCRLFもbyte固定のまま維持し、自作差分の空白検査ではcr-at-eolを指定した。

調査は本差分と関連資料に限定し、全履歴・全資料の網羅監査は行っていない。
通常の製品JPEG XL経路から脆弱Brotli処理への到達は今回のsource調査では確認しなかったが、
悪用不能を証明していない。v0.1.0が脆弱componentを配布している事実と区別する。
既存v0.1.0 tag、GitHub Release、ZIP、そのhash/Source/Binary identity、Owner記録、
原LICENSE/PATENTS・旧検証証拠は変更していない。

次phaseでは、レビュー後のsource identity固定、新binary/package profileと完全なNOTICE、
binary privacy・配布導線・必要な実機検証、新成果物に対するOwner承認を行う。
既存ダウンロードは本ローカル修正では置換されず、正式公開は別phaseである。
