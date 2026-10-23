# v0.1.0 Portable Releaseの準備

最初のS1公開はsource-onlyで、製品バイナリを含まないsnapshotでした。その後R1で実依存InstallとRelease build、R2で18 PEの限定技術受入、R4-Dで45ファイルの非公開候補ZIP受入が完了しています。GitHub Releaseは未公開・公開未承認です。[BUILDING](../BUILDING.md)で構築実績とG2判断の引継ぎを区別します。

対象はWindows x64 Portable、Release /MD、EXE 1本とapp-local DLL 17本、未署名、インストーラなしです。Microsoft VC++ Runtime x64は[Microsoft公式案内](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)から利用者が外部導入し、製品ZIPへRuntime DLL・installer・OS標準DLLを同梱しません。
製品build source revisionは`93545d458da33cef397fb77458336ea06339dcb6`です。その後Phase 3-Dで、Microsoft対象コード限定条件を同梱した46ファイルのv4非公開候補を受け入れました。[Portable README](../packaging/binary/README-portable-s1-release-v3.md)、[Binary NOTICE](../packaging/binary/THIRD-PARTY-NOTICES-S1-BINARY-RELEASE-v3.md)、[v4 Release Notes／本文テンプレート](RELEASE-NOTES-0.1.0-V4.md)を参照してください。これらの文書とPackaging commitは製品の再ビルドを意味しません。
今回の製品targetにはmanaged skcms objectを含まずlibjxl側もSKCMS=OFFですが、保守的な権利情報coverageとして[licenses/skcms/LICENSE](../licenses/skcms/LICENSE)のcopyright・3条件・免責全文を同梱し、Binary NOTICEから対応付けます。build treeの`LICENSE.skcms`だけでは配布同梱の証拠になりません。[確認範囲](SKCMS-LICENSE-REVIEW.md)を超える利用とMS-002等は別の残件です。

製品実装ではEXE隣の既存settings.confを優先し、有効な[Portable marker見本](../packaging/clipss2.portable)を扱います。
markerの内容はASCII clipss2-portable-v1と末尾LF。見本は実設定ではありません。
設定の[記述例](../packaging/settings.conf.example)も実ユーザーデータではありません。
設定保存先・起動modeの詳細はsource実装に従い、未知の既存設定の自動移行・削除を案内しません。

製品の更新時は、先に終了し、設定/必要な画像を利用者自身で保全してください。
削除時も製品と設定・保存画像を分け、必要な個人データを確認してから利用者が処理します。
Microsoft RuntimeをSystem32から抽出・再配布する手順はありません。最低必要Runtime版の保証や、未確認の環境での成功は主張しません。
[外部PC検証表](OTHER-PC-VALIDATION.md)は旧Source-only時点の未実施表として保持します。その後のR3候補に対する別PCの展開・起動・capture・保存成功は限定的なユーザー申告です。OS/Runtime版、capture mode、保存形式、現地hash照合は未記録で、今回の最終ZIP全体の別PC検証には読み替えません。Portable設定の保存・復元やcleanupの全面受入も意味しません。[現在の確認範囲](SUPPORT.md)を参照してください。

## SDK-2の後続設計（第3段階時点の歴史的記録）

以下は実装前の設計記録です。現在の[条件文書v4](../packaging/binary/MICROSOFT-COMPONENT-TERMS-v4.txt)はOwner承認文面を実装・同梱済みですが、SDK-2はCLEAR IF、公開は未承認です。以下の「未実装」等を現在のv4の状態として扱いません。

初回起動同意UI・独自インストーラ・製品再ビルドは採用しません。GitHub Release本文とREADMEで取得前に条件を案内し、Microsoft対象コード限定条件文書（仮称`MICROSOFT-COMPONENT-TERMS.txt`）をEXEと同階層へ同梱する構成を優先候補とします。正式な文面・提示導線・履行方法は確定していません。
対象契約は既存確認済みSDK 10.0.26100.0のEULAID `WIN10SDK.RTM.AUG_2018_en-US`です。§2(a)(ii)の配布要件と§2(a)(iii)の配布制限を対象に、[R4-D記録](R4-D-RELEASE-DECISION.md)の原本識別・既存判断を再利用します。別版の一般文例を適用原本と同一視しません。

条件文書は適用SDK契約の対象組込みコードと受領者・再配布者に限定し、自作MITの全文・権利、別のOSSの条件を維持します。実際の対象コード、表示保持、再配布時の条件伝達・合意要求、配布制限を対応付け、配布者自身の補償・防御等の継続義務は別に記録します。OS DLL本体を静的同梱したと説明したり、MIT/OSS全体へ逆解析禁止・GPL禁止を課したりしません。

条件の提示、同意を求める運用、法的な合意成立は別です。匿名の直リンク・二次配布を含む方式の妥当性と限界を責任者・法務判断で確定し、ダウンロード・実行・文中の「明示同意」という宣言だけで全利用者の同意を認定しません。法務・Owner未承認のAI英文、Microsoft対SDK利用者の責任限度額、包括免責を最終条件として転記しません。文面と方式の採用、実際の反映・受入は後続作業です。

既存R4-D候補は`clipss2-0.1.0-windows-x64-portable.zip`、7,964,729 bytes、45ファイルの非公開候補として不変更で保全します。条件文書を追加する場合は新しいPackaging世代を設け、条件文書1件だけの追加なら閉集合は46ファイルになります。新profile・生成許可・Reader/writerの世代接続、README/NOTICE/Release本文の参照、ZIP entry・展開・hashの受入が必要です。18 PEと既存原文・markerのbytesを維持し、新ZIPのsize/SHA-256を測定します。旧45ファイル契約・anchor・profile・receiptを上書きせず、旧ZIPのhashを新ZIPへ流用しません。今回は新条件文書・新ZIPを生成していません。
