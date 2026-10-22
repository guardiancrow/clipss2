# S1 Shipping Privacy source契約

S1はPrivacy ON / v2 / s1-v1 / MSVC x64 / Ninja / Releaseのみです。
[中立origin](managed-origin-s1-v1.json)と[original tuple TSV](managed-origin-s1-v1.tsv)は旧derived値を持ちません。
[v2 spec](managed-diagnostics-v2.json)と[v2 manifest](managed-sources-v2.tsv)は3段階の診断/変更notice postimageを固定します。
[依存diagnostics](dependency-diagnostics-v2.json)と[port policy](DependencyPorts-v2.cmake)も元のv2 identityを保持します。
[source-inputs](source-inputs-s1-v1.json)は16 packageと実28入力を固定します。旧v1契約へのfallbackはありません。

原source4 checkout、9 gitlinks、1481 tupleと、限定v2派生1481件の対応は検査済みです。
派生ソース・依存本体・compiler/SDK/Runtime・PRIVATE証拠はこの公開source集合に同梱しません。
固定skcmsのS1 MSVC x64対象は[限定権利確認](../../docs/SKCMS-LICENSE-REVIEW.md)によりBSD-3-Clauseと確認しました。Microsoft tool/runtime terms/版対応の未確定事項は維持します。
PlanOnlyはread-onlyです。Install経路は実装済みですが、実install/configure/build/ABI/runtimeは未実施です。

[帰属](ATTRIBUTION.md)、[S1 NOTICE](../../THIRD-PARTY-NOTICES-S1.md)、[Readerとscope](../../docs/SOURCE-S1-NOTICES.md)を参照してください。
PDB、個人設定、ログ、build/cache、旧契約・旧binaryは公開scope外です。
