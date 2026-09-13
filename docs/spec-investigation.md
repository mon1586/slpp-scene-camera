# 仕様の考慮漏れ・追加調査記録

## 2026-09-14 タグregexを一覧全体への照合へ変更（03:13）

ユーザーのsittingあり・standingなしという条件に対応するため、個別タグのany-matchを廃止し、取得順のタグを半角スペース1個で連結した文字列へregex_searchする仕様へ変更した。名前欄とのAND、空欄の制限なし、情報不明の除外は維持。既存のタグregexの`^`と`$`は一覧全体を指すようになるため、単語タグの存在確認には`\bsitting\b`などを使う。手動回帰テストも更新した。

build.cmd成功、4スイート全通過。肯定・否定先読みの組み合わせ、除外タグの前後順、空タグ一覧と情報不明の違いを追加確認した。実機での新しい照合結果は未確認。MO2側DLL/PDBを更新しdistとのhash一致を確認。DLL SHA256: `91E2E0E25C7BA1173EE93403FFF7B2E13673DF1F282219D77BDE4937FED99219`。旧ファイルは`build/tag-list-filter-backup-20260914-031307`へ退避した。

## 2026-09-14 アニメーションフィルタ本実装とPoC撤去（02:58）

承認されたレース対策に沿って、通知受信時の番号更新、取得待ち、最新要求の確定をAnimationUpdateCoordinatorへ実装した。AnimationChangeは取得可能の根拠にせず、AnimationStart/StageStartでIDを取得する。名前・タグと候補を同じ更新として公開し、公開・通常選択・プリセット保存の公開をSelectionBoundaryで順序付ける。VM呼び出し・LOS・regex・ディスク書き込みはこの排他区間に含めない。受信番号は通常の受信ログより先に更新する。

エディタに名前・タグのregex欄、入力エラー、現在情報表示を追加。保存形式はversion 5の任意項目として拡張し、初期選択・通常A/Dへ反映した。取得不能・最後の対象通知から5秒経過では情報不明として条件なし候補だけを使用する。取得PoCのCapture/Probe、専用計測テスト、MoveSceneの観測専用ログと定期観測を削除し、既存の調査記録は保持した。

build.cmd成功、4テストスイート全通過。新規テストは旧応答・逆順応答・受信済み未配送通知・部分応答・期限切れ・同一key再開始・取得例外・regex照合と保存を確認する。SceneCamera結合テストでは実際のLOS評価途中に通知または削除を差し込み、古い候補の公開を防ぐこと、取得中の移動が保留を解除しないことも確認した。実機のUI/キー変更から新IDを取得できるかは未確認であり、自動テストから保証しない。

ゲーム停止を確認し、MO2側DLL/PDBを更新してdistとのSHA256一致を確認した。DLL: `E30C8B48A849B667D9702848B7E963FF887E1E57C66FE2CC025B8E06207D78FF`。PDB: `3E5D7A80F801553558227F6C23E198A199EA643E16DB84E61A2C12FF52CF3F28`。旧DLL/PDBは`build/animation-filter-before-install-20260914-025819`へ退避済み。ユーザーのプリセットファイルは上書きしていない。

実機受入では名前regexに現在名の一部、タグregexに現在のタグを設定し、UI変更・キー変更・通常ステージ進行・移動・連続変更で現在情報とA/D候補を確認する。`Animation filter committed`ログは確定したID・名前・タグ数を記録する。旧PoCの反復計測ログは出力しない。

## 2026-09-14 UI変更のログでイベント網羅性の不足を確認（02:21）

ユーザーがUIから変更したと報告した実行のSSC/P+ログを `build/animation-metadata-v3-ui-20260914-022142` に保存した。SSC SHA256: `08E939E0E74C710FED86195530C8E4C0D90411935DA0E17065E2BBC6BA7C1BE6`。P+ SHA256: `45487B77792D13D7A1E5F2199A8EFEAA88DB2286C9E3F4225153C6A8B381E9B3`。

開始時のIDは `21zryfmm`、表示名は `Billyy Handjob 1 Kneeling Side`。Starting/Startの2報告だけで、AnimationChange callbackは0件、接尾辞なしStageStartは7件だった。SSCのerror/criticalは0件。

P+ログでは02:21:34.782に `B_HJ1_A1_S1`、02:21:37.533に `B_AKneelFF_A1_S1`、02:21:40.553に `B_Beh2_A1_S1` が実際に受理されている。後二つは同じアニメーションの単なるS2/S3進行ではなく、異なるアニメーションのS1である。その直前のSSCログにはそれぞれ02:21:37.522、02:21:40.543のStageStartがある一方、AnimationChangeがない。現行PoCはStageStartを取得の契機にしないため、開始後の新しいIDは記録していない。

結論: 前回の変更キーの結果だけではUI経路まで保証できず、今回その不足を実ログで確認した。開始・AnimationChange・移動完了だけで現在情報を更新する仕様では、UI変更後も古いmetadataが残る。次の候補はStageStartでもIDを取得し、保持IDとの差がある場合にmetadataを更新するイベント駆動方式。StageStartはステージ進行にも届くので、アニメーション変更と同一視しない。

配置済み `sslThreadModel.psc:1249` はStageStart通知後に `AdvanceScene` を呼ぶ。このためStageStartという名前だけで同期完了とは解釈せず、同イベントを契機としたID取得のタイミング確認が必要。今回のUI操作がPSCの検索fallbackかnative UI側の別経路かはログだけでは確定していない。P+ログの `Missing interface, scaling disabled` は別件として保全した。実装・ビルド・再配置は行わず、結果記録のみ。

## 2026-09-14 タイミングPoC v3のゲーム内ログ（02:15〜02:16）

`build/animation-metadata-v3-20260914-021612` にSSCログ、P+の `SexLabUtil.log`、抽出JSON、集計CSVを保全した。SSCログSHA256: `13296BD9ACC6B172195C183BABDDA7185818F22056BBFD4FA045AA10DF212F1A`。P+ログSHA256: `7C7FCA514C71741881E0EB046E762ED04E1C3BE722142CFCBD8A925BB9E0160B`。

- Starting 2件、Start 2件、Change 17件、ActorsRelocated 2件の計23通知を観測し、すべてに最初のID要求と成功応答が残った。ID取得は計122回、9種類のIDで全回ok。各通知内で最初と後続のIDが異なるケースは0件。
- 初回要求はSSCの通知投入から3.3893〜3.8677ms後。実行済みID要求の予定からの遅れは最大3.874ms、要求から応答の遅れは最大21.1399ms。これは今回の測定範囲で、イベント自体の遅れやP+内部の代入時刻を表すものではない。
- StartingをStartが、連続Changeを後続Changeが置き換えても、先行通知の最初の取得を保持した。同一main update内での置き換えをこのログだけから再現済みとはしない。その条件は自動テストで検証している。
- 名前・タグを要求した20観測は両方ともok（計40応答）。eventId 9/10はすべての要求済みIDがokだが、連続通知が続いてmetadataの後取得開始前に5秒上限へ達したためreason=timeout。VMのID取得失敗ではない。eventId 25は移動後約878msでシーンが終了し、6回のID取得のみを残して名前・タグは未要求。名前・タグの網羅取得とIDタイミング取得の範囲を区別する。
- 02:15:28の変更でIDが `djmq34yw` から `4v7hmy0m` へ変わり、変更通知の初回から後続まで `4v7hmy0m` を返した。02:15:41の移動後も同じIDを保持した。2回目のシーンの連続変更も、各通知内では取得IDが一貫していた。
- 変更通知を伴わない02:15:47〜52、02:15:58、02:16:04〜05、02:16:10のStageStartはP+ログで同じアニメーションのS2/S3/S4/S5への進行と照合できた。この箇所はアニメーション全体の変更通知の欠落と判断しない。
- SSCのerror/critical、request/window/report容量超過は0件。P+ログには02:15:41.918と02:16:10.582の移動時に `ReplaceCenterRef is not on valid thread` があり、別途02:15:20.524に `Missing interface, scaling disabled` がある。これらはSSCの取得失敗ではなく、原因をこのPoCの有無へ帰属する根拠もない。両ログに保全した。

結論: 今回の受信イベントに対するID取得タイミングでは、イベント起点の一回取得に追跡を追加する必要を示す結果はなかった。これはイベント駆動案を支持する実測であり、全経路の通知網羅や競合不在の証明ではない。選択画面からの変更を実施したかはユーザーへ照会中。ロード・新規ゲーム・NPC別シーン・容量上限は今回の実ゲームログでの検証済みとしない。実装変更・ビルド・再配置は行わず、結果記録のみ。

## 2026-09-14 タイミングPoCレビューとv3の修正

レビュー基点はHEAD `0fe3c90f5c8da04b394e7ddcae5866570a97f765` とv2の作業ツリー。要件・正確性・回帰/API・検証品質の4観点で独立レビューを実施した。修正前の対象は `build/animation-metadata-v2-review-snapshot` に保存した。発見をこの世代で固定し、ユーザーの「改善して」に従って以下の指摘を修正した。元レビューにmust-fixはなく、観測範囲の改善と制限の明示が対象である。

| 固定した指摘 | 修正・処置 | 解消確認 |
| --- | --- | --- |
| COR-01: 同一main updateの連続通知で先の最初の取得が消える | 通知処理内で最初のID要求を出し、通知ごとの窓に要求済み応答を保持する。後続通知は追加要求のみ停止する | 同一batchの2通知と逆順応答のテスト、および元レビュアーの解消確認で閉鎖 |
| REG-01: 名前・タグや先のID応答待ちが次のID要求を遅らせる | ID予定要求を応答待ちから分離。名前・タグは観測IDに対する後取得として分離する | 全ID応答を保留した8要求、旧metadata遅延中の次イベントのテストと解消確認で閉鎖 |
| REQ-1 / VER-01: 操作経路と通知有無の確認が任意 | 変更キー・選択画面、操作順、画面上の変更前後の名前、通知がない操作の記録を必須化する | 検証品質レビュアーが元の不足の解消を確認。実操作の結果はまだ未取得 |
| REG-02: 追加の同期ログが初回取得を遅らせる | PoCの要求前ログを削除し、詳細報告はIDの予定要求が終わってから出す | 回帰レビュアーが軽減を確認。既存ログやVM負荷の影響は残ると仕様に明記 |

`AnimationMetadataCapture` をVM依存のreaderから分離し、同じcapture処理に制御可能な時刻・遅延応答を渡してテストする。追加テストは同一batch、逆順応答、応答が一件も返らない間の予定要求、遅いmetadata、load後の遅延旧応答、終了・別scene・移動完了、通知過多と報告上限、正常な空タグを扱う。16通知・128未解放callback・64保留報告で制限し、上限による欠落をログへ明示する。VM callbackの実行やP+の通知網羅性をシミュレータで確認したものではない。

指摘解消のレビューは元の発生条件と修正起因のmust-fix退行だけを対象とした。正確性・回帰・検証品質の各レビュアーが閉鎖を確認し、新しいmust-fix退行はなかった。API経由の非同期取得とprivate mailboxの方針は維持する。

`build.cmd`成功。追加テストを含む既存4テスト群、x64/SKSE exports/依存DLL検査が成功。Skyrim未起動を確認しMO2へDLL/PDBを配置、distと両ファイルのSHA256一致を確認した。旧版と直前ログは `build/animation-metadata-timing-v3-backup-20260914-021326` に保全済み。DLL SHA256: `DAD1AF229032566796B8163C759E4B887B33A612DBDA3B046D92A908A5276EB0`。ログ識別子は `AnimationMetadata timing v3`、要求・応答時刻は報告の出力時刻とは別に記録する。v3の実ゲーム確認は未実施。

## 2026-09-14 アニメーション情報のタイミング検証PoC v2

v1のゲーム内ログを `build/animation-metadata-v1-20260914-014647.log` に保全した。5種類、全14観測で名前・タグのstatusがok、取得前後のIDがsame_idだった。ただし01:46:29.033の変更通知は直前と同じIDを返しており、この観測は次の変更で打ち切られた。「変更通知1回の取得だけで必ず新しい値が得られる」という結論は出さない。

配置済み `sslThreadModel.psc:1156` の `ResetScene` は同期中・settle期間中に切り替えを見送る、または要求を保留する経路を持つ。`sslThreadController.psc:235` はこの処理の前に `AnimationChange` を送る。`:1534` 付近の `OnAnimationSynchronized` にある `AnimationStart` は初回だけで、各変更後に必ず送られる完了通知にはできない。PSCを確認した結果であり、PEX/DLL内部の実測とは区別する。

v2は既存の開始・変更にActorsRelocatedを追加し、各通知のSubmitEvent時刻と通番を観測へ引き継ぐ。時刻の基準はSSCのイベント投入時点であり、P+内部の送信命令・ID代入時刻ではない。各VM要求とcallback到着の時刻を記録する。観測予定は投入から0/25/50/100/250/500/1000/2000ms、実際の要求時刻を別途記録し、main updateで直列に実施する。観測処理開始から5秒で打ち切る。取得時刻は要求〜callbackの区間内にあると解釈する。

ログ識別子は `AnimationMetadata timing v2`。phase=queued/observing/sample/closedとeventIdで対応付ける。中断時も未完了sampleと理由を出し、previousObservedID・firstID・lastIDで前後を比較できる。後続通知のない間だけ最大8観測する診断であり、製品版ストア・regex・フィルタ・常時ポーリングは追加していない。

最終 `build.cmd`、既存4テスト群、x64/SKSE exports/依存DLL検査が成功。新しい時刻ログのゲーム内検証は未実施。Skyrim未起動を確認してMO2へDLL/PDBを配置し、distと両方のSHA256が一致した。旧版は `build/animation-metadata-timing-v2-backup-20260914-015804` に退避済み。DLL SHA256: `711C692AB83F2DC3F86FA001D2E4589CE14D1B4722868033E7540E273019BAF5`。確認手順は[PoC仕様](animation-metadata-poc.md)に記載。

## 2026-09-14 アニメーション名・タグ取得PoC

期待動作とゲーム内確認手順は[アニメーション情報取得PoC](animation-metadata-poc.md)を参照する。

取得元は配置済み `F:\Games\BottleRim\mods\SexLab Framework PPLUS\Source\Scripts` で確認した。`sslThreadModel.psc:14` に `String Function GetActiveScene() native`、`SexlabRegistry.psc:90` に `String Function GetSceneName(String asID) native global`、同`:105` に `String[] Function GetSceneTags(String asID) native global` がある。`sslThreadController` は `sslThreadModel` を継承する。`GetSceneTags` は全stageのタグを合わせたsceneタグであり、`sslThreadModel.GetTags` が返す候補集合の共通タグとは異なる。同梱PSCの確認であり、実行中PEX/DLLとの一致や実呼び出し成功はまだ確認していない。

`sslThreadController.psc:235–236` の変更操作は `AnimationChange` 通知後に `ResetScene` を呼ぶ。このため通知時点を新しいIDの確定境界とせず、時間をずらした3回の観測を行う。観測ごとに `GetActiveScene` → 表示名・タグ → `GetActiveScene` の非同期VM呼び出しを行い、前後のIDの一致も記録する。`same_id` は前後2回の一致だけを意味し、途中の切り替えが絶対になかった保証ではない。

`AnimationMetadataProbe` は既存のmain updateで通知・結果・期限を処理する。VM callbackは専用mailboxに値をコピーするだけとし、カメラやゲームオブジェクトへアクセスしない。後続通知・終了・lifecycle世代変更で古いmailboxを観測対象から外す。5秒で未完了の観測を打ち切るが、VMへ渡した呼び出し自体の取消を保証しない。遅延callbackは破棄済みmailboxへ書くだけで、次の観測へ混ざらない。プリセットや公開runtimeインターフェースは変更していない。

検証: `build.cmd`成功、既存4テスト群成功、x64/SKSE exports/依存DLL検査成功。これは既存処理の回帰確認であり、新しいVM呼び出しの実ゲーム検証ではない。初回ビルドの引数テンプレートエラーは、文字列を値として渡す修正で解消した。

Skyrim未起動を確認し、MO2の `Sexlab Scene Camera` へDLL/PDBを配置してdistとのSHA256一致を確認した。旧ファイルは `build/animation-metadata-probe-v1-backup-20260914-014114` に退避済み。DLL SHA256: `83026CC9BD4B6AC558F74A217E6DC9D721E05A8EDD267A7364380DE4B9F1C096`。ログの識別子は `AnimationMetadata probe v1`。次はプレイヤー参加シーンの開始・アニメーション変更のゲーム内ログで取得可否を判断する。regexエディタとフィルタは未実装。

調査・修正日: 2026-09-06。調査基点: `8434aec`、最終検証対象は本作業の変更を含むワークスペース。本書は調査証跡であり、現行の期待動作は各設計文書を参照する。修正前の不整合と、修正後の保証範囲を区別する。

## 2026-09-11 Move Scene: 検出条件の調査（本修正前）

ローカルの `F:\Games\BottleRim\mods\SexLab Framework PPLUS` を調査した。`meta.ini` のinstallationFileは `SexLab Framework PPLUS - V2.18.1.7z`。MO2の選択プロファイルは `pg13_npcface` で、このModは有効。これは配布アーカイブ名と配置済みソースの確認であり、実行中PEXとソースの一致や実ゲームでの通知順序はまだ確認していない。

- `Source/Scripts/sslThreadController.psc:239` の `MoveScene` は開始通知を送らない。非対応シーンは冒頭で終了し、説明画面での取消もActorのpause/unlock前に終了する。
- 移動に入るとActorをPausedへ移し、プレイヤーをunlockする。`SexLabUtil.SetActorMovement(PlayerRef, 1)` の後、1秒待機し、キー再押下または最大60回の0.5秒待機で移動を終える。その後movementをlockし、位置が停止するまで待ってからActorをunpauseし、`CenterOnObject(PlayerRef)` を呼ぶ。
- `SexLabUtil.psc:175` の `SetActorMovement` は、非VRプレイヤーのunlockでAI駆動を解除し、移動入力を許可する。lockではAI駆動を有効化し、移動入力を禁止する。この入力フラグはゲーム全体の状態であり、Move Scene固有の識別子ではない。
- `sslThreadModel.psc` の `CenterOnObject` は再配置成功時にだけ `ActorsRelocated` を送る。再配置失敗では、ユーザー選択によるシーン終了または取消があり、取消時にはこの通知を送らない。
- 同ファイルの `SetupThreadEvent` は `SendModEvent(HookEvent, thread_id)` を使う。`thread_id` はstrArg。Papyrus用の `ModEvent` とネイティブ `ModCallbackEvent` を混同しない。
- SSCは `ActorsRelocated` を受理せず、従来の未知通知ログも名前にAnimationを含むものだけを記録する。したがって既存ログだけでは移動開始・終了の時系列を判断できない。

診断ビルド `MoveScene probe v1` は、SexLab.esm定義Questからの全ModCallbackEventを採否変更なしで記録する。既存メイン更新でプレイヤー入力許可、カメラ状態、pause、SSCのローカル所有フラグを変化時だけ記録し、その時点のworldFOVを添える。SmoothCam APIによる実所有者の照会は追加しない。プレイヤー参加開始通知から観測し、終了後の返却も含めてロード・新規ゲームまで継続する。プローブの失敗はシーン状態へ反映しない。

次の判断材料は [ゲーム内調査手順](move-scene-test.md) のログ。入力許可の変化だけでMove Sceneと断定したり、lock直後を再同期完了とみなしたりしない。根拠が不足すれば、対象シーンのActor状態などの観測を追加して再調査する。本修正、自動復帰、受入確認、memo削除は未実施。

2026-09-11検証: `build.cmd` 成功。既存4テスト群がすべて成功し、x64/SKSE exports/依存DLL検査も成功。これは既存手続きの回帰検証であり、診断の実ゲームでの発火やMove Sceneの検出成功を保証しない。Skyrim未起動を確認してMO2の `Sexlab Scene Camera` へ診断DLL/PDBを配置し、両ファイルのSHA256がdistと一致することを確認した。DLL SHA256: `B338CB0810FBFF041DA81CA3B015A3EF83E513BA948D792DAB974C9661E97142`。配置前のDLL/PDBと既存ログは `build/move-scene-probe-v1-backup-20260911` に退避済み。ゲーム内調査結果待ち。

## 2026-09-11 Move Scene: 実測結果と返却・復帰の実装

ユーザーが診断版で長押しと時間切れの両方を実施した。ログは `build/move-scene-probe-v1-20260911-2238.log` に保全した。

| 区間 | 移動許可 | 再制限 | ActorsRelocated |
| --- | --- | --- | --- |
| 短い区間（長押しに対応すると推定） | 22:37:32.919 | 22:37:36.765 | 22:37:37.474 |
| 長い区間（時間切れに対応すると推定） | 22:37:41.169 | 22:38:20.243 | 22:38:20.952 |

両区間ともcamera=9、looking=1のまま、SSCの所有フラグが1で残り、移動中にA/Dによるプリセット切り替えが発生した。再制限から再配置通知までは両方とも約0.709秒。長い区間は実時間約39秒であり、スクリプト上の待機回数を正確な実時間30秒のタイマーとして扱えない。最終的なAnimationEndingでカメラ解放とIDLEを確認した。

本修正はMove Scene専用の推定器ではなく、有効なプレイヤー参加シーンで移動が許可されたら制御を譲る規則を採用した。`ISceneSource::CollectControlState` は移動許可とpauseを返し、`MainUpdateDispatcher` が通知処理後に `IRuntimeClient::ProcessMainUpdate` を呼ぶ。SceneCameraは中断中にscene keyと開始時刻を保持し、外部所有権を返却、入力要求とpreviewを無効化する。復帰条件が成立するまではカメラUpdateで構図を評価・反映しない。

再制限・非ポーズ状態の1秒継続を復帰の暫定条件とし、同じシーンのActorsRelocatedまたはAnimationChangeで待機を更新する。同期完了の保証はなく、修正版の実ゲームで姿勢と復帰タイミングを確認する。取消でActorsRelocatedがない場合も状態に基づいて再開できる。対象外カメラ状態など既存のreset条件は変更しない。復帰時の旧anchor、旧preview、候補選択を捨てて現在地から評価し直す。

`build.cmd` 成功。4テスト群とDLL検査が成功。追加テストは短い／長い移動、中断中のA/D停止、移動先anchor、通知なしの再制限、連続移動、返却失敗・スレッド不一致相当の再試行、入力取得不能、pause、Editor無効化、身体入力待ち、取得拒否時の再試行抑制、Debug、終了/resetと遅延通知、別scene key、TDMの開始直後の取消、30分watchdogを対象とする。実際のスレッド・フックやSLPPの取消UIを模擬したテストではない。

修正版DLL SHA256: `E3A9F220AF8AEBF4733D4A0F4FE7F068EEDA39CD6269AE5E217B57DB460FC34D`。起動ログの識別子は `MoveScene control v1 enabled`。生通知・状態変化のprobeログは受入確認用に維持する。ゲーム内受入が未完了のためmemoは保持する。

Skyrim未起動を確認し、MO2のSSCへ修正版DLL/PDBを配置した。両ファイルのハッシュがdistと一致。前の診断版は `build/move-scene-control-v1-backup-20260911` に退避済み。

## 2026-09-11 Move Scene: 修正版のゲーム内確認（23:15〜23:22）

ユーザーから「問題なさそう」との報告。修正版のログは `build/move-scene-control-v1-20260911-2322.log` に保全した。起動識別子 `MoveScene control v1 enabled` を確認。

- 23:15:29.181に移動許可で中断し、同時刻にSmoothCamの返却が成功。中断から復帰までプリセットcutと構図反映はともに0回。
- 23:15:38.694に移動が再制限され、23:15:39.407に再配置通知を処理。23:15:40.411に新しい身体中心から構図を作り、SmoothCamの再取得と反映が成功した。
- その後の5回のシーン終了でも、終了直前の移動許可時に返却し、AnimationEndingでIDLEとなった。これらは通常終了時の入力解放と整合し、追加のMove Scene実施回数には数えない。
- error/criticalは0件。warningは既存の接尾辞付きAnimation通知を無視する記録のみ。
- 移動→再配置→復帰は1区間のみ（約11.23秒）。修正版の時間切れ経路をこのログだけで検証済みとはしない。診断版での時間切れ確認と区別し、実施有無をユーザーへ確認中。

確認範囲は実際の移動操作に対するユーザー報告、移動時の返却と復帰、移動中の構図変更停止、通常のシーン終了後の解放。ロード中断、取消UI、Debug／Editorとの移動競合はこのログからは確認できない。これらの状態遷移に対する自動テスト結果と、ゲーム内確認結果を混同しない。

## 2026-09-11 Move Scene: 修正版の時間切れ確認（23:25〜23:29）

追加ログは `build/move-scene-control-v1-20260911-2329.log` に保全した。23:25:17.149に移動許可と同時にSmoothCamの返却が成功し、23:25:56.206の再制限まで約39.06秒間、所有フラグは0。移動中のプリセットcutと構図反映は0回だった。

23:25:56.918にActorsRelocatedを処理し、23:25:57.922に復帰の再評価へ進んだ。新しい身体中心を取得できたが、`No valid camera preset is available; SmoothCam remains in control` となり、この時点では再取得しなかった。これは候補がない場合の既存仕様に沿った結果であり、「時間切れ後の自動再取得成功」とは記録しない。

23:26:34.500にEditorを開いた後、23:26:34.504に再取得が成功した。23:28:23.079のDebug ONで返却、23:29:03.211のDebug OFFで再取得も成功。これらはMove Scene中のDebug／Editor競合を再現したものではない。最後は23:29:38.967に返却、23:29:39.130にIDLEとなり、error/criticalは0件。

前回ログと合わせて、移動中の返却維持、短い移動後の自動復帰、時間切れ後の再評価と候補なし時の通常カメラ維持、通常終了後の解放を確認した。取消UI・移動中ロード・移動中のDebug／Editor競合については引き続き実ゲーム未確認。コードの追加変更は不要と判断し、この回は結果記録のみを更新した。

## Move Sceneコミット前レビュー

比較基点は `f19b7e456b1259fd615c5c672ca50b1d655006b2`。変更済み作業ツリーと新規Move Scene文書を同一の対象として固定し、要求・正確性・回帰/API適合・検証品質の4観点で独立レビューを実施した。修正前の変更ファイルは `build/move-scene-review-original` に保存した。

指摘を1件に確定した。**COR-1（must-fix）**: SSCが所有中と記録しているカメラを別Modが取得した直後、カメラUpdateより先に移動許可を検出すると、一時返却が `kNoOwnership` を返す。この結果を正常な返却と同一視してシーンを保持し、再制限後に古いシーンから取得を試みる。既存の「所有権喪失では対象シーンを破棄する」規則に違反する。根拠はReleaseCameraとSmoothCamCameraControl::Releaseの経路であり、競合Modを使った実ゲーム再現ではない。

方式評価は現行方式を維持。SLPPの移動開始通知がないため、明示的に移動を優先する仕様には入力許可の観測が適合する。既存のメイン更新・SmoothCam APIを使用し、別フックやSLPPスクリプトの変更は不要。その他の観点から確定した欠陥はなかった。実ゲーム未確認事項は [Move Sceneテスト](move-scene-test.md) に残す。

**COR-1修正・閉鎖確認済み**: 記録上の所有権を持ってReleaseへ入った場合の `kNoOwnership` は、シーン終了とreset要求へ進める。正常返却後に所有権がないケースにはこの分岐を適用しない。回帰テストは元の発生順序、遅延再配置通知で再開しないこと、正常返却との区別、他所有者への重複返却防止、新しい開始で再取得できることを検証する。指摘した独立レビュアーが元の問題の解消と共有ReleaseCamera呼出元への修正起因の退行がないことを確認した。

最終の `build.cmd`、4テスト群、x64/SKSE exports/依存DLL検査は成功。DLL SHA256は `2A86EAD8B10CAFE0DD727C29CD1331A8DCE9818C0C47BF6C02457742508619C7`。競合Modによる所有権喪失の実機再現は未実施であり、閉鎖根拠はソース・自動テスト・ビルド。未解決のmust-fix指摘はない。ユーザーの指示に従いmemo.mdは削除し、残るゲーム内検証の限界は文書に維持する。

## 既存の調査結果

G01〜G12について、現行動作と保証できない範囲を各設計文書へ反映した。旧説明を注記で現行仕様に残す方式はやめ、FILEの旧手順・失敗表、RT・HOOKの旧mailbox方式と全camera state対応の説明を削除した。

- G10の数値上限チェックを修正し、文字列優先・数値fallback・丸め・上下限を含む21ケースを追加した。既存の受理ルールは維持した。
- 追加でG13〜G15を修正した。終了・resetの解放失敗後の更新継続、処理済みresetの遅延再適用、旧シーンのpreview要求残留を止め、Flow回帰テストを追加した。
- G02の同一key再利用後の旧通知識別、G03の新Editorに届く過去の画面終了識別は、現状の保証に含めない。
- G04の30分上限は現行動作を明記した。時間上限や対応機能を変更する判断は今回追加していない。
- `build.cmd`でDLLビルド・4テスト群・x64/SKSE export/依存DLL検査が成功。ゲーム内確認は未実施。

「整合済み」は文書の期待結果と現行コードの契約を揃えた意味であり、実機保証や将来の保証拡張まで完了した意味ではない。

## G01: 開始・変更通知の採否 — 文書整理完了

根拠: [SceneSession.h](../src/SceneSession.h)の`Prepare`・`Activate`、[SceneCamera.cpp](../src/SceneCamera.cpp)の`Prepare`・`OnAnimationStarting`・`OnAnimationStart`・`OnAnimationChange`・終了処理。

- 対象なしのStartは準備を補い受理する。
- 準備中の同一keyのStartingは参加者を更新する。active中の開始通知は無視する。
- 別keyの開始は既存シーンを置換しない。Changeはactiveかつ同一keyだけ受理する。
- 初期アンカー待ちでもStartを受理した後はactiveなので、Changeで初回評価期限を延期する。
- 準備中の同一keyのStartでプレイヤーが不参加になっていれば、準備を取り消す。
- EndingとEndはそれぞれ単独で一致する対象を終了できる。

[シーンカメラ手続き](scene-camera-procedure.md)へ採否表を追加した。同一keyの別シーン再利用はG02として分離した。

既存確認: [SceneSessionTests.cpp](../tests/SceneSessionTests.cpp)の`overlapping scene cannot replace Preparing`、`wrong scene key cannot activate`、`overlapping scene cannot replace Active`。StartingなしStartは[SceneCameraFlowTests.cpp](../tests/SceneCameraFlowTests.cpp)の初期起動で使用している。重複Startや全状態のイベント組み合わせを網羅するテストがあるとは主張しない。

## G02: 同じscene keyの再利用 — 制限を明記

根拠: [SceneKey.h](../src/runtime/SceneKey.h)、[CameraHook.cpp](../src/runtime/CameraHook.cpp)の`InvalidatePendingEvents`・`SubmitEvent`、[PluginRuntime.cpp](../src/runtime/PluginRuntime.cpp)の`QueueLifecycleReset`。

keyはQuestのForm IDとthread IDだけで、個々のシーン開始を識別する番号はない。保留通知の世代を変えるのはゲームのロード・新規ゲーム境界であり、通常のシーン終了・次シーン開始では変わらない。同一keyを再利用した新シーンに旧シーンのEndが届けば、現状の一致判定は終了対象として扱う。

リポジトリにP+の送信側スクリプトは含まれていない。同一keyの再利用と遅延通知が実際に成立する順序、および送信側の保証は今回確認できていない。ロード前にSSCが受理済みの保留通知を無効化することと、ロード後に初めて届く古い送信元通知を識別することも別である。

RT・PROC・ROADの保証範囲を一致しないkeyとロード前の保留通知に揃え、同一key再利用後の旧終了は識別保証外と明記した。ここを改善する場合は送信側契約の確認と機能変更が必要であり、今回対応済みとはしていない。

## G03: Editor中の通知 — 整合済み・識別上の制限あり

根拠: [SceneCamera.cpp](../src/SceneCamera.cpp)の各イベント処理と`Update`、[PresetEditorMenu.cpp](../src/ui/PresetEditorMenu.cpp)の`OnMenuEvent`・`OpenEditor`・`CloseEditor`、[PresetPreviewService.cpp](../src/runtime/PresetPreviewService.cpp)の`ClearPreview`。

- シーンイベントの受理にEditorの状態は使われず、遅延した一致Endは編集中でも復帰を開始する。
- Changeを受けると編集状態に関係なく再評価が予約される。Editorの更新も継続するため、期限に達すると全候補の評価へ入る経路がある。
- 連続したCloseは、終了済みpreviewを再度変更しない。これは既存テストで確認されている。
- 一方、新たなEditorを開いた後に古い画面終了通知が到来した場合、通知を編集開始の世代で識別する契約はない。Menuの終了通知はその時点のEditorを閉じる。
- Debug設定はDashboard、ReloadはEditorにあるため、通常UIからの操作と遅延・競合した通知を分ける必要がある。

通常進行は停止するが、同一keyの外部Changeは期限後の評価、終了は復帰を優先する契約へPROC・UI・CRUDを揃えた。画面全体のCloseは現在のEditorを閉じる。新Editor開始後に届いた過去の画面終了だけを除外する保証はないと明記した。実際の送信元でこの順序が生じるかは未検証。

## G04: watchdog — 現行上限を明記

根拠: [SceneCamera.cpp](../src/SceneCamera.cpp)の`kMaximumSceneDuration`・`activeSince_`・`Update`、[CameraHook.cpp](../src/runtime/CameraHook.cpp)の更新可否判定。

現状はStart受理から**実時間30分を超えた時**にシーンを終了する。30分ちょうどでは終了しない。Changeや身体入力の正常取得で開始時刻を更新せず、終了通知が欠けたことを調べるものでもない。

- 初期入力待ちも30分に含む。
- Startingだけの準備状態はactiveではなく、このwatchdogの対象ではない。
- 通常のポーズでは判定する更新が止まるが、実時間の経過は止まらず、再開後に判定される。
- Editor・Debug復帰ではポーズ中にも更新され、30分を超えるとその更新で終了しうる。

PROCへ現行の30分上限と適用範囲を明記し、UIのEditor維持の例外にも反映した。これは上限の妥当性を実機で評価した結果ではない。ポーズ除外や準備専用期限を導入する場合は別の仕様変更になる。既存テストにはこの30分境界の直接検証がない。

## G05: ポーズ中の更新 — 文書整理完了

根拠: [CameraHook.cpp](../src/runtime/CameraHook.cpp)のポーズ判定、[SceneCamera.cpp](../src/SceneCamera.cpp)の`AllowsUpdateWhilePaused`、[SceneCameraFlowTests.cpp](../tests/SceneCameraFlowTests.cpp)のポーズ・Debug復帰ケース。

通常更新は止めるが、preview中、Debug設定変更の処理中、Debug復帰再試行中は更新を許可している。G13修正で終了・reset要求が残っている場合もポーズ中の更新を許可し、解放を再試行する。その場合の追従時間は0で、定期評価の時計を進めない。Debug切り替えのための候補評価とpreview評価は定期評価とは別である。

Runtime、hook、シーン手続きの記述をこの区分へ揃えた。既存テストはポーズ中の解放・復帰、追従位置維持、定常Debug中の停止を確認する。実際のゲームから更新される境界の妥当性はG12として分離した。

## G06: 失敗後の復旧 — 整合済み

根拠: [SceneCamera.cpp](../src/SceneCamera.cpp)の`Update`・`ApplyRequestedTransform`・`ApplyTransform`・`ReleaseCamera`・`Restore`。

| 条件 | 現実装で確認した処理 |
| --- | --- |
| 初回の身体入力・向きが無効 | 初回評価を保留。取得しない |
| 取得済み後の身体入力が無効 | 直前のアンカーで構図を維持。A/Dを止める |
| 候補の姿勢・LOSが無効 | 候補を使用不能にする。現在の選択は可視性だけでは解除しない |
| 通常の選択構図の生成が失敗 | シーンを破棄し復帰 |
| 編集値からの構図生成が失敗 | エラーを返す。通常の構図生成失敗と同じシーン破棄は行わない |
| カメラ取得の事前確認・取得要求が失敗 | 取得せずエラーを表示。active scene自体は維持 |
| カメラ表示失敗・所有権喪失 | 復帰とシーン破棄。A/D・Debug OFFだけでは消えたシーンは再開しない |
| 解放に失敗 | 成功したことにせず、復帰を保留し再度resetを要求 |

取得失敗後にはactiveが残るため、変更通知による評価、別候補への手動切り替え、Debug OFF、preview要求などで再度取得経路に入れる場合がある。対して、所有権喪失でシーンを破棄した後は改めて有効な開始通知が必要になる。

初期FILEの「SmoothCam取得失敗でそのシーンの処理を終了」という旧表を削除し、失敗の種類・activeと選択の扱い・再開条件をPROCの表へ集約した。通常の取得拒否にもDebug復帰と同じ再試行を適用するといった新しい動作は追加していない。

追加調査で、終了の解放失敗後もactiveが残り更新を継続する不備を発見した。G13で修正し、終了・resetに伴う解放失敗は復帰待ちとして評価・反映を停止する。シーンを維持するDebug ONの解放再試行とは区別する。

## G07: 編集対象消失後の復帰 — 文書整理完了

根拠: UI・VISの既存仕様、[SceneCamera.cpp](../src/SceneCamera.cpp)の`EvaluateVisibility`、[SceneCameraFlowTests.cpp](../tests/SceneCameraFlowTests.cpp)の`R1-02`ケース。

編集対象を引き継げなければ編集前の選択を維持し、そのIDもなければ通常カメラへ戻す。可視性の回復を理由に別候補を自動選択しない。未保存新規からの復帰、編集前IDの削除、編集前に選択がない場合の3経路が既存テストで確認される。CRUDの復帰先表現を揃えた。

## G08: 初期段階の説明 — 文書整理完了

現在のUI・VIS・FILEの保存仕様、[PresetEditorMenu.cpp](../src/ui/PresetEditorMenu.cpp)のFOV設定、[SceneCamera.cpp](../src/SceneCamera.cpp)の初期選択・A/D、`build.cmd`と配布規則を照合した。

CRUDのFOV編集対象外を修正し、可視性診断の担当文書を明確にした。FILEの初期実行手順・対象外一覧・旧失敗表は削除し、現行の参照先へ置き換えた。ROADの開発段階と現在の契約を区別し、同梱read-onlyデータや旧mailboxの説明も更新した。

## G09: 時間境界と予約 — 文書整理完了

根拠: [SceneCamera.cpp](../src/SceneCamera.cpp)の`Update`、[SceneCameraFlowTests.cpp](../tests/SceneCameraFlowTests.cpp)の`R1-01`・`R1-01-C1`。

- 復帰間隔は0.5秒以上、期限はOFF処理から2秒未満。2秒ちょうどで期限切れを先に判定する。
- 同じOFF状態を繰り返しても新しい切り替えとして観測せず、試行予算を更新しない。
- 欠落中は試行回数を消費しないが、期限は進む。古いEditor終了要求も予算を迂回しない。
- 更新時に確認したON・Editor開始・期限切れは取得試行より先に処理される。終了・resetで要求を破棄した後は再開しない。
- Change待ちの間は定期評価・A/Dを止める。期限ちょうどで変更後評価が実行可能になり、入力が欠落すれば保留する。変更後評価を行った更新では定期評価を重ねず、その後の0.5秒を数え直す。

DBG・PROC・VISへ反映した。既存テストは499msと500ms、3試行、2秒一致、入力欠落、編集・ON・resetによる取消を確認する。異なる通知元の並行到来順序を一律に保証したものではなく、G03・G12で示した範囲の実機検証は残る。

## G10: thread ID — 境界欠陥を修正・21ケース成功

根拠: [SexLabPSceneSource.cpp](../src/runtime/SexLabPSceneSource.cpp)の入力変換とsender検証、修正後の[SceneInstanceID.h](../src/runtime/SceneInstanceID.h)。

| 入力 | 現実装 |
| --- | --- |
| 有効なint32整数文字列 | 文字列を優先し、numArgは参照しない |
| 空文字または不正な文字列 | numArgへfallback。不正文字列だけでは拒否しない |
| 数値が非有限・範囲判定で範囲外 | 拒否 |
| 有限の小数 | 最も近い整数へ丸め、ちょうど半分は0から遠い側へ丸める |
| 不正sender | IDの採否に関係なく拒否 |

修正前の上限比較には欠陥があった。`int32`最大値2147483647を`float`へ変換すると2147483648になり、範囲外のnumArg=2147483648を上限比較で拒否できなかった。PowerShellの同じ単精度変換と比較で確認した結果は次のとおり。

```text
Int32Maximum: 2147483647
MaximumConvertedToFloat: 2147483648
OutOfRangeInput: 2147483648
RejectedByCurrentUpperBoundComparison: false
```

修正後は入力値と整数上限を正確に比較し、丸め後の範囲も確認してから整数へ変換する。RTへ既存の文字列優先・小数丸め・不正文字列fallbackを明文化し、21ケースで互換性と境界を確認した。数値2147483648は拒否し、文字列2147483647は受け付ける。修正前の整数変換がゲーム内でどの値を返したかは確認していない。テストは製品が使う同じ変換関数を呼ぶが、実際のSKSEイベントやsender検証まで模擬してはいない。

## G11: Reloadと選択ID — 現在の操作範囲で文書整理完了

根拠: [PresetEditorMenu.cpp](../src/ui/PresetEditorMenu.cpp)の`RenderSection`・`RenderEditor`・`ReloadPresets`・`SelectFirstPreset`、[PresetRepository.cpp](../src/runtime/PresetRepository.cpp)の`Reload`。

現在のReloadボタンはEditor内だけに存在する。Dashboardにはなく、Debug中はEditorを開始できない。外部ファイルの自動監視もないため、「通常表示またはDebug中に利用者が直接Reloadする」という前提は現在の操作経路には当てはまらない。

EditorからReloadすると、成功時は新しい保存順の先頭を選びpreviewする。元の選択IDが残っていても先頭へ選び直し、空一覧なら最後の有効なpreviewを維持して新規作成を可能にする。失敗時は一覧と編集内容を維持する。この範囲をUI仕様へ明記した。

内部の保存一覧だけを通常表示中に置き換えるテスト入力や、将来DashboardへReloadを追加する場合までの契約には広げない。通常表示中に同一IDの構図が変われば最新値を使用する処理と、Debug表示のID保持処理は確認したが、利用者向けの直接Reload操作とは区別する。

## G12: 通知境界とcamera state — 文書整合済み・実機確認待ち

根拠: [CameraHook.cpp](../src/runtime/CameraHook.cpp)の`SubmitEvent`・`QueueReset`・`InstallOnce`・`Thunk`・`CameraStateSink`、RTとHOOK。

| 調査開始時の旧設計 | 現行の実装 |
| --- | --- |
| 固定長mailboxへ蓄積し、camera updateで通知を排出 | イベントをSKSE taskへ渡し、そのtaskから直接シーン手続きを呼ぶ。srcに固定長mailboxは見当たらない |
| mailbox満杯時に通知を破棄してreset | 当該上限・満杯判定の経路はない |
| 通常の例外・resetの外部変更は次のcamera updateまで遅延 | reset専用taskから保留要求を処理する経路、イベントtask例外からEmergencyResetを呼ぶ経路がある。G14で処理済みresetの再適用を防止 |
| cameraStatesの各実体を対象にする | ThirdPersonとAnimatedのみを対象にする |
| camera eventを監視した再hookを行わない | 再hookは行わないがcamera event自体は監視し、対象外stateへの遷移でresetを要求する |

上表は修正前の不整合の記録である。RTとHOOKを現行の通知受理・通常更新・復帰の契約へ書き直した。再hookしない契約を維持し、camera eventによる対象外状態の検出はそれとは別に記載した。固定長mailboxや全camera state対応を現行の要求として残していない。

実機で残る確認: SmoothCamを含む更新順序、通知側での復帰、対象外stateへの移行。FlowテストはSceneCameraへ直接通知を渡すため、この実接続の検証を代替しない。G14でresetの消費方法は修正したが、hook連携方式やサポートstate自体は変更していない。

## G13: 終了の解放失敗 — 修正・自動検証済み

根拠: [SceneCamera.cpp](../src/SceneCamera.cpp)の`Restore`・`ProcessPendingReset`・`StopSceneWork`・`EmergencyReset`、[SceneCameraFlowTests.cpp](../tests/SceneCameraFlowTests.cpp)のT35/T37。

修正前は`Restore`が解放失敗で早期returnし、activeと予約を残していた。更新冒頭のreset再試行が失敗してもその後の更新が続き、終了対象のアンカー取得・評価・構図反映へ進めた。またreset要求だけではポーズ中の更新が許可されなかった。

修正後は解放前に復帰待ちへ入り、評価・A/D・Debug復帰・previewを取り消して編集をロックする。reset処理は解放未完了を呼び出し元へ返し、通知処理と更新はそこで停止する。ポーズ中も保留resetの解放を進める。緊急解放失敗にも同じ取消を適用する。Debug ONの解放失敗は従来どおりシーンを維持し、切り替えを再試行する。

自動検証: End、直接reset、緊急復帰の3経路 × 解放失敗・実行スレッド不一致の2結果、計6通り。解放失敗が続く間のChange・別keyのStart・A/D・更新で、アンカー取得数、ray数、取得数、構図反映数が増えないことを確認した。ポーズ中の再試行成功後に待ち状態が消え、改めて受けた開始が動くことも確認した。実際のSmoothCam APIのエラーやゲームスレッドはテスト用制御に置き換えている。

## G14: 処理済みresetの遅延実行 — 修正・自動検証済み

根拠: [CameraHook.cpp](../src/runtime/CameraHook.cpp)の`QueueReset`、[IRuntimeClient.h](../src/runtime/IRuntimeClient.h)と[SceneCamera.cpp](../src/SceneCamera.cpp)の`ProcessPendingReset`、FlowテストのT36。

修正前はreset要求を別のイベント処理や更新が消費した後でも、専用taskが無条件に`Reset`を呼んだ。この間に新シーンを開始すると、古いreset taskが新シーンを消せた。

専用taskは保留要求だけを消費する処理へ変更した。既に消費されていれば何もせず、その後の新しいreset要求があれば処理する。Flowテストで、Aのresetを開始イベント側で消費してBを開始した後、同じRuntime interfaceへ遅延・重複の呼び出しを行い、Bの所有権・状態・解放回数が変わらないことを確認した。その後の新resetはBを終了できる。SKSE task queue自体の順序・実行環境を模擬するテストではない。

## G15: 旧シーンのpreview残留 — 修正・自動検証済み

根拠: [SceneCamera.cpp](../src/SceneCamera.cpp)の終了・消去処理、[PresetPreviewService.cpp](../src/runtime/PresetPreviewService.cpp)の`InvalidatePreviewSession`、FlowテストのT35/T37。

修正前はシーン内部のpreview参照だけを消し、共有サービスにpreview値やEditor終了後の復帰要求が残った。そのため次のシーンが旧下書きや旧復帰先を使う経路があった。

終了・resetでpreview sessionと共有要求を無効化する。UI内の下書き自体は変更しない。6通りの終了失敗ケースの後に新シーンを開始し、旧下書きの角度ではなく保存済み構図を使うことを確認した。また、削除済みの旧プリセットを復帰先とするEditor終了要求を未処理でresetし、新シーンの初期選択を妨げないことを確認した。ゲーム内Editorの再操作と並行通知の実接続確認は残る。G03の過去の画面終了通知を識別できない制限は、この共有要求の取消とは別である。

## 検証証跡と未実施範囲

実行コマンド: `build.cmd`。構成: RelWithDebInfo。対象ランタイムのゲーム起動は行っていない。

| テスト名 | 結果 | 今回参照した範囲 |
| --- | --- | --- |
| SexlabSceneCamera.SceneSession | 成功 | 重なったシーンの拒否、preview終了の重複、保存・読込、追加したID変換21ケース |
| SexlabSceneCamera.PresetSettingsSpec | 成功 | 設定値・Editor利用条件・hotkey保存 |
| SexlabSceneCamera.VisibilityEvaluation | 成功 | 可視条件・候補選択 |
| SexlabSceneCamera.SceneCameraFlow | 成功 | 初期入力、ポーズ、Debug復帰期限、編集終了の復帰、追加T35〜T37の終了失敗・reset遅延・preview残留 |

ビルド後のDLL検査も成功。SHA-256: `9cfed288d8c6e07ec1009ca3e54d71cb2d8204922482e3682e4c305c1d858675`。

対応する考慮漏れケースの例はT01、T06〜T07、T10〜T12、T14〜T16、T20〜T21、T28〜T29。これは既存テストとの部分対応であり、各T行の全分岐を実行したという宣言ではない。とくにT02〜T05の全通知順序、T24のEditor遅延通知、T30〜T31の実接続、T32の30分境界、T33の全失敗経路、T34の期限重複、G10のSKSEイベント受信側、G12の実行境界は検証完了に数えない。

## 2026-09-10 R1/R2: メイン更新での通知と返却

`1413196`の走査で、通常のSKSE taskはメインスレッド限定ではなく、対応カメラ更新が停止すると返却の再試行機会を失う経路（R1）、参加者収集のスレッド保証との不一致（R2）を確認した。上記G14の専用SKSE taskは今回廃止した。

`CameraHook`はゲームのメイン更新から`MainUpdateDispatcher`を実行する。通知元はmutexで保護した保留領域へ処理を渡すだけとし、参加者収集・既存の通知判定・シーン手続きはメイン更新で行う。ロード世代の確認は収集前と通知直前に維持する。処理中に追加された通知は次回に回す。各メイン更新で`ProcessPendingReset`も実行するため、対象外カメラ状態・ポーズ中の返却は対応カメラUpdateに依存しない。通常の構図反映とTDM解除開始は従来のカメラUpdateに残す。

接続位置の一次資料はTrue Directional Movementの`MainUpdateHook`。v2.2.7（`ed6b033cf07febf47e0dd563f44f7b5416f934e1`）の`src/Hooks.h`、`src/Hooks.cpp`は、Address Library ID 35565/36564、SE +0x748 / AE +0xC26、引数・戻り値なしの呼出を使用する。追加調査時の同リポジトリ`src/PCH.h`と`src/Hooks.h`は1.7.99以降を+0xC38へ切り替えており、その分岐も反映した。E8 CALLを確認し、現在の呼出先を先に公開してから接続する。他Modの既存呼出先も維持する。命令形式が合わない場合はシーン通知を登録せず、誤った位置へ書き込まない。

自動検証は本物のworkerからの投稿、メイン側での収集・通知順序、処理中投稿の次回送り、カメラUpdateを停止した後のSmoothCam相当の返却失敗→再試行→完了、同条件でのTDM一時禁止返却を対象とする。これはdispatcherとシーン手続きの検証であり、ゲームのフック呼出を模擬したものではない。

実ゲームで残る確認は、新しいメイン更新ログとAPI/cameraのthread ID一致、対象外カメラ状態に留まった場合の返却、ポーズ中の終了、解除要求直後の終了・ロード。TDMのメイン更新入口自体にカメラ状態による制限はなく、pause判定はその呼出先の内部にあるが、すべてのmenu状態でゲーム側の入口が継続することまでは自動テストでは保証しない。
