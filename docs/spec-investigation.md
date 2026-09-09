# 仕様の考慮漏れ・追加調査記録

調査・修正日: 2026-09-06。調査基点: `8434aec`、最終検証対象は本作業の変更を含むワークスペース。本書は調査証跡であり、現行の期待動作は各設計文書を参照する。修正前の不整合と、修正後の保証範囲を区別する。

## 結果

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
