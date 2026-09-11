# clearance非依存プリセットCRUD 要件・設計

状態: **実装済み／ビルド・unit test済み／ゲーム内確認待ち**

## 目的

ユーザーがゲーム内でカメラ構図を作り、プリセットとして一覧、作成、更新、削除、再読込できるようにする。編集した数値は保存操作を待たずにカメラへ反映し、構図を見ながら調整できることを必須とする。

本書はプリセットの作成・編集・保存を扱う。壁や遮蔽の状態は編集用previewと保存の可否に影響させない。現在は可視性診断と通常表示の候補選択も導入済みであり、その条件は[`clearance-design.md`](clearance-design.md)、画面の操作は[`preset-settings-spec.md`](preset-settings-spec.md)に従う。

## 対象範囲

対象に含めるもの:

- 保存済みプリセットの一覧表示と選択
- active sceneで現在適用中のプリセットを示す常設操作
- 新規作成、既存プリセットの更新、削除
- 保存ファイルからの再読込
- 現在のカメラ位置からのプリセット作成
- 数値操作に追従するリアルタイムプレビュー
- 保存、取消、未保存変更の扱い
- 不正入力、保存失敗、外部ファイル変更時のユーザーへの通知

対象に含めないもの:

- LOSまたはclearanceによる妥当性判定の定義（可視性仕様へ分離）
- 通常表示での有効プリセットの初期選択（可視性仕様へ分離）
- プリセット間の補間
- rollの編集。FOV offsetの編集は対象に含める。

## ユーザー要件

### 一覧と選択

- 保存済みプリセットを安定した順序で一覧できる。
- 一覧から編集対象を選ぶと、その保存済み値が編集欄へ表示される。
- active scene中にプリセットを選んだ場合、その構図をプレビュー対象にできる。
- 未保存の変更がある状態で別のプリセットへ移る場合、変更を保存するか破棄するかを選べる。暗黙に失わない。

### active sceneからの編集

- Player参加scene中は、現在適用中のプリセット表示名と編集開始操作を常時確認できる。
- 常設表示には編集開始用hotkeyの現在の割り当てを示し、覚えていなくても操作を確認できる。
- 常設表示はmouse cursorを表示せず、mouse入力を捕捉しない。
- 編集開始操作は現在表示している構図を別のプリセットへ切り替えず、そのプリセットの編集を開始する。
- 編集開始用hotkeyはゲーム内で変更でき、再起動後も割り当てを維持する。
- Editor内では同じhotkeyで閉じられ、未保存変更を暗黙に失わない。
- 保存後にEditorを閉じても、編集したプリセットの可視状態を理由に別のプリセットへ戻さない。
- 編集した保存済みプリセットは、現在のsceneで使用不可でもユーザーの明示選択として通常表示へ引き継ぐ。
- 編集対象が削除済み、または新規編集を保存せず終了した場合は、編集開始前の保存済みプリセットへ戻し、使用不能でも維持する。編集前の選択がない、またはそのIDも削除済みの場合は通常カメラへ戻す。別の使用可能候補を自動選択しない。
- 通常のscene camera表示中はscene進行を止めず、編集開始後にゲーム時間を停止する。
- 現在適用中のプリセットがない場合はその状態を表示し、対象のない編集を開始しない。
- ほかの操作を遮断する画面と同時には操作できない。

### 作成と更新

- 新規プリセットは、現在のカメラ位置を基に作成できる。
- 既存プリセットは、IDを維持したまま構図の数値を更新できる。
- 保存済みプリセットの表示名を改名できる。Saveで確定し、ID、件数、順序、構図、選択を維持する。
- 空の表示名や保存失敗では元の保存内容を維持し、改名案を編集欄に残す。取消では表示名も最後の保存状態へ戻す。同名の別プリセットは許可する。
- 別IDとして残したい場合は「新規保存」として扱い、既存プリセットを上書きしない。
- IDの重複、不正な数値、表現可能範囲外の値は確定できず、理由を画面に表示する。

### 削除と再読込

- 削除は対象を明示し、誤操作を防ぐ確認を伴う。
- 選択中またはプレビュー中のプリセットを削除しても、削除済みの値をカメラが参照し続けない。
- 削除後にプリセットが残る場合は次の選択対象を明確にする。editor中に0件になった場合は最後のposeと所有権を維持して新規作成を可能にし、0件のままeditorを閉じた時点でSmoothCamへ戻す。
- 再読込は外部で変更された保存ファイルを反映できる。未保存の編集がある場合は、破棄の確認なしに再読込しない。

### 保存と取消

- 作成、更新、削除は明示的な確定後に永続化する。
- 保存に失敗した場合、直前まで正常だった保存データを壊さず、編集内容を失わずに再試行できる。
- 取消では、カメラと編集欄の両方を最後に保存した状態へ戻す。
- 編集画面を閉じる、ロードする、new gameへ移る、対象sceneが終了する、といった境界で未保存状態をどう扱ったかが明確である。

## リアルタイム編集要件

- framingの`right`、`up`、orbitの`yaw`、`pitch`、`distance`、またはFOV offsetを動かすたび、保存操作なしでカメラ構図が変わる。
- 反映は次の利用可能なcamera updateまでに行い、連続操作に目視で追従する。
- 数値操作中は保存ファイルを書き換えない。画面上の編集中値と永続化済み値を区別する。
- 不正な途中入力は保存もカメラ反映もせず、最後に有効だったプレビューを維持する。
- camera poseを適用できていない場合はeditorを無効にし、プレビューできない理由を表示する。プリセット0件かつactive sceneから新規previewを開始可能な場合だけ、復旧用の新規作成を許可する。
- Editor中はゲーム時間を停止し、確定済みアンカーを基準に数値変更をpreviewする。すでに発生した外部通知は[シーンカメラ手続き](scene-camera-procedure.md)の採否に従う。通常進行の停止と、遅延した通知の拒否を同じ保証にしない。
- camera所有権を失った場合は他Modと競合して再取得を繰り返さず、プレビューを中止して状態を通知する。
- この段階では補間を加えず、入力値に対する直接的な構図確認を優先する。

## プリセットの意味

SKSE Menuに表示する設定項目とユーザー操作の契約は[`preset-settings-spec.md`](preset-settings-spec.md)に分離する。保存形式は[`fixed-preset-camera-design.md`](fixed-preset-camera-design.md)のschema version 5を使用する。

- `id`はプリセットを識別する固定キーであり、保存データ内で一意とする。作成時に自動付与し、UIから変更できない。
- `name`はIDとは独立した表示名とし、一覧、選択欄、Scene toolbar、削除確認に表示する。
- `framingOffset.right`と`framingOffset.up`はcamera right/upで作る画面平面におけるframing centerの移動量を表す。
- `orbit.yawDegrees`と`orbit.pitchDegrees`はアンカー周囲の視線方向、`orbit.distance`はframing centerとの距離を表す。
- framing offsetを固定したままorbitを変えても、アンカーの画面内Right/Up成分は変化しない。
- view forward方向のoffsetはdistanceと同じcamera poseになるため、独立した値として持たない。
- framing offset、orbit、FOV offsetは有限値とする。yawは`-180..180`度、pitchは`-90..90`度、distanceは0より大きい値、FOV offsetは`-160..160`度に制限する。
- FOV offsetはユーザーの通常FOVを基準とする相対値として保存する。rollは保存せず、プレビュー時も保存後の利用時もカメラはframing centerを見る。
- schema version 5のみを読み書きする。旧形式の自動移行は提供しない。

現在のcamera位置`C`からプリセットを作る場合は、framing offsetを0に置き、`C - anchor`からyaw、pitch、distanceを得る。現在のcameraがアンカー以外を向いていても、取り込み後の向きはアンカーへ揃う。編集中のプリセットから複製する場合はframing offsetを含む全値をそのまま引き継ぐ。

## 設計上の責務分離

| 領域 | 責務 | 持たせない責務 |
|---|---|---|
| UI | 操作の受付、編集中値とエラーの表示、確認 | JSON入出力、cameraの直接操作 |
| 編集セッション | 選択ID、保存済み値、編集中値、dirty状態、保存・取消判断 | game型、ファイル置換 |
| プリセット管理 | 一覧、ID一意性、作成・更新・削除・再読込の整合性 | ImGui状態、camera所有権 |
| 永続化 | version 5の読書き、失敗時の旧データ保護 | scene状態、プレビュー状態 |
| camera preview | anchor相対値からworld poseを作り、安全なcamera更新境界で反映 | 永続化、UI widget状態 |

編集セッションは「保存済み値」と「編集中値」を分けて保持する。camera previewは編集中値を一時的な入力として扱い、プリセット管理の確定済み一覧を書き換えない。これにより、リアルタイム編集、取消、保存失敗からの再試行を同じ状態モデルで扱う。

## 状態と主な遷移

- 閲覧中: 保存済み一覧と選択を表示する。
- 編集中: 選択した保存済み値から編集中値を作る。値が異なればdirtyとする。
- プレビュー中: active sceneとanchorが利用できる間、編集中値をカメラへ反映する。
- 保存中: 入力検証と永続化を行う。成功すれば編集中値を新しい保存済み値とし、失敗すれば編集状態を保つ。
- preview session中: editorはtransformとrevisionだけを送る。sceneとanchorの選択、camera pose、所有権はcamera側が管理する。
- 終了処理中: 保存、破棄、または終了理由による安全なプレビュー解除を完了してから画面を閉じる。

プレビュー中は編集状態に重なる一時状態であり、保存済みデータの状態ではない。editor表示中はゲーム時間を停止するため、通常のscene遷移をeditor状態として扱わない。camera所有権喪失、Apply失敗、ロード、new game、plugin resetでは確認を待たず、安全な復帰を優先する。

## 永続化要件

- 1回の保存操作は、プリセット集合全体として成功または失敗する。
- 書き込み途中の内容を正常な`presets.json`として残さない。
- 保存失敗時は、最後に正常だったファイルを引き続き読める。
- プレビュー操作ではファイルI/Oを行わない。
- JSON parseとファイルI/Oはcamera updateの処理経路から分離する。
- 保存後に同じファイルを再読込し、同じID、順序、数値が得られる。

一時ファイルと置換を利用する方針までは設計要件とするが、OS APIや具体的なファイル名は実装時に決める。

## SKSE Menu Frameworkを使う場合のUI設計

SKSE Menu Framework 3系を採用する。公式consumer APIが提供する次の仕様を利用する。

仕様確認元は[SKSE Menu Framework 3](https://github.com/QTR-Modding/SKSE-Menu-Framework-3)と[公式consumer API header](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API)とする。

- Mod Control PanelへMod固有のページを登録できる。
- 独立したinput-blocking windowを登録できる。
- menuのopen/close eventを購読できる。
- frameworkの導入有無とversionを実行時に確認できる。
- inputの捕捉と、blocking windowが開いているかの確認ができる。
- ImGui APIはframework側のconsumer headerを介して利用する。

リアルタイム編集ではcamera updateが継続する必要があるため、active sceneの常設操作またはMod Control Panel内の導線から専用editor windowを開く。editor表示中はゲーム時間を停止し、mouse、keyboard、gamepad入力をeditorへ捕捉する一方、preview用camera updateだけを継続する。

editorはframework上のinput-blocking windowとして登録し、Mod Control Panel本体を閉じる。時間停止と入力捕捉はframeworkに委ね、camera hook側ではpreview session中の更新だけを許可する。

menu close eventはプレビュー解除の境界に使う。専用editorのClose操作では未保存変更を保存、破棄、編集継続から選ぶ。framework側のmenu closeでは未保存値をeditorに保持したままプレビューだけを解除し、再度editorを開いたときに編集を続けられる。open/close event APIを利用するため、SKSE Menu Framework 3.4以降を必要versionとする。frameworkはsoft dependencyとし、未導入または必要version未満の場合はmenu登録だけを行わず、既存のプリセット読込とcamera機能を維持する。

editorを開けるのはcamera poseが適用済みの場合に限る。editor openはpreview sessionとして通知する。数値変更ごとにrevisionを発行し、そのrevisionのpose適用が確認できるまで保存を無効にする。所有権喪失などでlive previewが成立しなくなった場合は編集操作をロックする。プリセット0件では、active sceneからcamera制御を取得可能な場合に限り、標準値の新規draftからpreview開始を試みる。

初版editorが表示する要素:

- プリセット一覧と選択状態
- 表示名入力と変更不可のID表示
- Pan Right、Pan Up、orbit yaw、pitch、distance、FOV offsetの数値入力
- 現在のcamera位置から値を取得する操作
- 新規保存、上書き保存、取消、削除、再読込
- dirty状態、プレビュー可否、入力または保存エラー
- active sceneで現在適用中のプリセット表示名と編集開始操作を示す常設表示

## 完了条件

- 一覧、作成、更新、削除、再読込の各操作がゲーム内UIから行える。
- active sceneの常設表示から、現在適用中のプリセットを切り替えずに編集開始できる。
- 数値を連続して動かすと、active sceneのカメラが同時に追従する。
- 保存前、保存後、取消後の構図がそれぞれ定義どおりになる。
- editor表示中はゲーム時間を停止し、preview用camera updateだけを継続する。
- editorを閉じた状態でsceneが終了した場合は直ちにcamera制御を返す。
- camera所有権喪失、Apply失敗、ロード、new game、plugin resetではpreview sessionにかかわらずプレビューが残留しない。
- 重複ID、不正数値、書込失敗、壊れた外部ファイルで正常な保存データを失わない。
- SKSE Menu Frameworkがない環境でも、menu以外の既存機能が変わらない。
- unit testでデータ操作と編集状態を、ゲーム内確認で時間停止、input捕捉、camera追従を検証できる。

## 検証状況

- version 5の読込、作成、更新、削除、再読込、重複ID、不正値、壊れた外部ファイルからの再読込をunit testで確認済み。
- 表示名の改名、ID・件数・順序の維持、同名の許可、再読込、保存失敗と再試行をunit testで確認済み。F8からの表示名編集は2026-09-12にユーザーよりゲーム内動作確認の報告あり。
- 画面相対framing offset・orbitからworld poseへの変換と、framing offset 0でのworld位置からorbitへの逆変換をunit testで確認済み。
- SKSE Menu Framework consumer headerを含むDLLのコンパイル、リンク、SKSE export、依存DLL検査に成功済み。
- pause中の連続数値操作への追従、input競合、Close後の復帰、SmoothCam所有権喪失はゲーム内確認待ち。

## 未決事項

- framing offsetとdistanceの実用的な上下限、数値入力のstep、細かい調整用のmodifier。
- 選択中プリセットを次回起動でも記憶するか。
- scene外で新規プリセットの数値入力だけを許可するか。
- gamepadでの細かな数値調整と、通常操作を抑制する範囲。
