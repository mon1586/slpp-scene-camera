# ロードマップ

## 1. カメラ切り替え POC

状態: **主経路のゲーム内確認済み／境界ケース検証待ち**

### 目的

SexLab P+ のプレイヤー参加シーンで SmoothCam から独自カメラへ切り替え、シーン終了後に確実に SmoothCam へ戻せることを確認する。

切り替え成功を目視するため、当初は参加者群の中心から大きく上空へ移動する仮カメラを使用した。このPOCは実機確認を終え、現在のコードから削除済みである。

### 実装範囲

- ネイティブ SKSE 側で SexLab P+ の `ModCallbackEvent` を購読する。
- 開始イベントから参加者を取得し、プレイヤー参加シーンだけを選別する。
- SmoothCam の公式 API からカメラ制御を取得する。
- POC用Coreが返す上空カメラposeを適用する（検証完了後に削除済み）。
- 対応する終了イベントでカメラを復帰させ、SmoothCam へ制御を返す。
- 開始、取得結果、状態遷移、終了、復帰結果をログへ記録する。
- 重複イベント、順序違い、制御取得失敗、シーン中断時の復旧処理を用意する。

### 完了条件

- プレイヤー参加シーンの開始時、カメラが明確に上空へ移動する。
- NPC のみのシーンではカメラが切り替わらない。
- シーン終了後、通常の SmoothCam 操作へ戻る。
- 重複または古い終了イベントで、現在のシーンのカメラが誤って解除されない。
- 失敗や中断後も、独自カメラ制御が残留しない。
- ログからイベント順序と各 API 呼び出し結果を追跡できる。

### 現在地

- src/runtime/core分離のSKSE DLLを実装済み。
- P+ の未接頭辞 native `ModCallbackEvent` と公式 Papyrus hook の関係をソース照合済み。
- P+ の `SendModEvent(HookEvent, thread_id)` が thread ID を `strArg` に載せることへ対応済み。
- SmoothCam V2 API による取得、interpolator 継続、goal 復帰、解放を実装済み。
- DLL のコンパイル、リンク、SKSE export、依存 DLL 検査は成功。
- AE 1.6.1170 で DLL のロード、P+ イベント順序、`strArg` の thread ID、player を含む参加者取得を確認済み。
- 初回ゲーム内ログで `AddTask` の実行スレッドと SmoothCam スレッドが一致せず、所有権取得前に POC が安全に中止したことを確認済み。
- `PlayerCamera::Update` の vtable hook はインストールログだけが出て実際には呼ばれないことを確認。SmoothCam が実際に利用する `TESCameraState::Update` の後段 hook へ変更済み。
- SmoothCam の公開実装に存在しないプラグイン側の先行スレッド拒否を削除し、API 自身の結果値を処理する構成へ変更済み。
- 開始・終了命令は固定長 mailbox へ積み、`TESCameraState::Update` hook で SmoothCam 更新後に排出する。
- AE 1.6.1170 で `TESCameraState::Update` hook の発火、SmoothCam の所有権取得、上空 pose、終了後の goal 復帰と所有権解放をゲーム内確認済み。
- `RELOCATION_ID` のSE/AE順序を逆にしたことで、ビルドとロードは成功しても対象コードパスでCTDする事例を確認・修正済み。再発防止策は `native-integration-failure-patterns.md` に記録した。
- レビュー後の hardening として、SexLab.esm sender 検証、ゲームスレッドでの参加者 snapshot、終了・中断 fail-safe、世代付き mailbox、専用 thunk による vtable chain 保持、Idle fast path、対応 camera-state 制限を実装済み。
- 上空POCは将来仕様として守る対象ではないため削除した。代わりに、scene keyと状態遷移を `SceneSession` テストで、アンカー計算をCoreテストで固定する。
- hardening 後 DLL のコンパイル、リンク、SKSE export、依存 DLL 検査は成功。ゲーム起動、追加ログ、主経路の動作も再確認済み。
- 残作業は NPC-only、重複・古い終了イベント、シーン中断、ロード、特殊カメラ、後発カメラ Mod との共存確認。

### POC では扱わないもの

- 実用的な構図
- スムージング品質
- 障害物回避とカメラ衝突
- 入力操作、プリセット、MCM
- 設定可能な正式パラメータ

## 2. カメラ機能の検討

状態: **アンカー実装・実機確認済み／次はraycast・clearance診断**

処理順序とRuntime/Coreの呼び分けは [`scene-camera-procedure.md`](scene-camera-procedure.md) に分離する。

### 目標

- シーン参加者が画面から見切れることを防ぐ。
- シーンが壁や家具に近い場合でも、遮蔽やクリアランス不足で見えづらくなることを防ぐ。
- 自動で「良い画角」を生成する問題は解かず、ユーザー作成プリセットから安全に使えるものを選ぶ。

### 開発順

最初にread-onlyの固定プリセットを読み込み、実用camera poseの生成からカメラ切り替えまでを一つの縦切りとして完成させる。次にclearanceを考慮しないプリセットCRUDを実装し、raycastは実際に読み込まれたプリセット候補だけを評価する。この順序なら、実行時のmock専用providerやraycast診断専用の候補生成機能を作らずに済む。

#### 2.1 固定プリセットの読み込みとカメラ切り替え

詳細設計は[`fixed-preset-camera-design.md`](fixed-preset-camera-design.md)に記載する。

- 最小のschema versionを持つ保存形式を決め、同梱した固定プリセットをread-onlyで読み込む。
- プリセットはID、画面相対のframing right・up、yaw・pitch・distanceを持つ。
- アンカーのforward、right、upからframing centerとorbitをworld poseへ変換する。
- parserで数値の有限性、値域、必須項目、重複ID、未知version、壊れたファイルを検証する。
- clearanceとLOSは判定せず、読み込みに成功したプリセットをすべてvalidとして扱う。
- 先頭プリセットのpose確定後にSmoothCamのカメラ制御を取得し、固定poseを反映する。
- アニメーション変更後は新しいアンカーから同じプリセットを再変換する。
- 読み込み失敗または有効なプリセットが0件の場合はカメラ制御を取得しない。
- シーン終了、reset、watchdog、所有権喪失時は既存の復帰・解放経路を使う。editor表示中はゲーム時間を停止し、通常のシーン終了自体を進行させない。

完了条件:

- 先頭の固定プリセットが指定したアンカー相対位置へカメラを移動し、アンカー方向を向く。
- アニメーション変更後も新しいアンカーを基準に同じ構図を再現する。
- 終了時と異常時にSmoothCamへ確実に戻る。

SmoothCam V2 APIによる取得、pose反映、goal復帰、解放の主経路はPOCで実装・実機確認済みである。この段階では新しい奪取経路を作らず、読み込んだposeを既存経路へ接続する。
20260902完了

#### 2.2 clearance非依存のプリセットCRUD

状態: **実装済み／Core・repository自動テスト済み／ゲーム内確認待ち**

詳細設計は[`preset-crud-design.md`](preset-crud-design.md)に記載する。

SKSE Menuに表示するcamera設定と操作の仕様は[`preset-settings-spec.md`](preset-settings-spec.md)に記載する。

- 安定したpreset IDで、一覧、選択、作成、更新、削除、再読込を行える。
- 作成・更新時に必須項目、数値の有限性、値域を一貫して検証し、失敗理由をユーザーへ示す。
- 保存失敗や書き込み中断があっても、最後に正常だった保存データを失わない。
- 未保存の編集、取消、再読込、選択中プリセットの削除について、予測可能な状態遷移を持つ。
- LOS・clearanceの結果に関係なく、プリセットを編集・保存できる。

ゲーム内編集では、画面相対のPan Right・Pan Upと、yaw・pitch・distanceを分けて扱う。view forward方向のoffsetはdistanceと重複するため持たない。数値編集中は保存操作を待たず、その場でカメラへ反映する。取消時は保存済みの構図へ戻し、確定時だけ永続化する。clearance表示と保存時警告は後段で追加する。

UI frameworkにはSKSE Menu Framework 3.4以降を採用する。Mod Control Panelからinput-blockingの専用editorを開き、ゲーム時間はframeworkに停止させたままcamera preview更新だけを継続する。framework未導入時はmenu登録だけを無効にして既存のプリセット読込とcamera機能を維持する。

editorはcamera pose適用済みの場合だけ有効にする。editorはsceneやanchorを参照せず、編集中のtransformとrevisionだけをpreview channelへ送る。editor表示中はゲーム時間を停止し、preview用camera updateだけを継続する。所有権喪失、Apply失敗、ロード、new game、plugin resetでは確認を待たず復帰する。Improved Cameraは既知競合として非サポートとし、検出時はcameraを取得しない。

完了条件:

- ゲーム内でCRUDと再読込を完結できる。
- framingの`Pan Right`、`Pan Up`とorbitの`yaw`、`pitch`、`distance`の数値操作にカメラが目視で追従する。
- editor表示中はscene進行を停止し、preview用camera updateだけが継続する。
- editor外のscene終了では即時にcamera制御を返す。
- camera所有権喪失などの安全境界ではpreview sessionにかかわらず編集中のcamera poseが残留しない。
- 不正入力と保存失敗で既存の正常なプリセットを壊さない。
- SKSE Menu Framework未導入時も、既存のプリセット読込とcamera機能が変わらない。

#### 2.3 読み込み済み候補のraycast診断

- 読み込んだ各プリセットのworld poseをraycast対象にし、空間全体を先に走査しない。
- 各候補について、候補点から参加者の代表点へのLOSと、候補点周囲のclearanceを別々に計測する。
- 始点、終点、hit有無、hit位置、hit fraction、可能なら法線と衝突対象種別をRuntimeから返す。
- アンカー取得または再取得時に全候補を同じ更新内で評価するが、collision query自体は候補とrayごとに行う。
- debug時は全rayを計測・表示する。通常動作では無効が確定した候補の残りのrayを省略できるようにする。
- 候補点、rayの終点、実際のhit位置、有効・無効の理由をワールド表示とログで確認できるようにする。
- シーン終了、reset、ロード、セル変更時にdebug表示をすべて破棄する。

完了条件:

- 開けた場所、壁際、狭い室内、家具付近で、表示されたhit位置が見た目のcollision位置と一致する。
- 同じ候補に対してLOS失敗とclearance不足を区別できる。
- 参加者自身のcollisionを障害物として扱うか、LOSの到達対象として扱うかを用途別に制御できる。
- ray始点がcollision内部にある場合を識別でき、通常の「遮蔽あり」と混同しない。
- プリセット数、ray本数、所要時間をログで確認できる。

#### 2.4 clearance仕様の確定

診断結果を見ながら、次を固定する。

- 参加者ごとのLOS代表点をPelvisだけにするか、Headなどを加えるか。
- 候補点周囲のサンプル方向数と必要距離。
- カメラを点として扱うか、半径を持つsweepを追加するか。sweepを使う場合の初期半径はSmoothCamの実装値も参考にするが、このModの実機結果から決める。
- static、terrain、家具、actorなど、clearanceとLOSそれぞれで対象にするcollision layer。
- 有効判定を全参加者LOS必須にするか、主要点の割合または優先度で決めるか。
- valid候補が0件の場合はカメラを取得しない、直前のvalid poseを維持する、SmoothCamへ復帰する、のどれにするか。

ここで固定した入力と判定はゲーム型を含まないCoreの値型にし、境界値をunit testで固定する。

#### 2.5 有効候補の選択

- Runtimeが取得したLOS・clearance結果をCoreへ渡し、候補ごとの有効・無効と理由を返す。
- 選択規則は決定的にし、同じ入力なら常に同じ初期候補を選ぶ。
- `A` / `D`では有効候補だけを循環する。
- アニメーション変更後はアンカー、候補world pose、raycast結果、有効候補を一組として再構築する。
- CRUD画面と保存操作へ現在poseの判定結果と警告を追加する。

#### 2.6 補間と仕上げ

- キー長押し、UI入力中、コンソール表示中などの入力抑制を決める。
- 位置、回転、FOVの補間を追加する。
- 補間経路にもclearanceが必要かを実機確認し、必要なら移動中のsweepまたは再評価を追加する。
- 多数のプリセットで単発評価がフレーム予算を超える場合は、上限設定または複数フレームへの分割を追加する。

### 暫定パイプライン

次の流れを候補とする。アンカーが static collision 内に計算された場合の扱い、raycast の本数・方向、必要クリアランスなどは未確定である。

1. 現在のアニメーションに対するシーンアンカーを生成する。
2. プリセットを読み込み、アンカーからの相対位置・相対回転・FOVなどをworld pose候補へ変換する。
3. CommonLibSSE-NGから利用できるraycastを使い、各候補について参加者へのLOSと壁・家具からのclearanceを計測する。
4. 計測結果から有効な候補だけを残し、初期候補を選択する。
5. シーン中は`A` / `D`入力で有効候補の前後へ切り替えられるようにする。

Core はアンカー、参加者 bounds、raycast/clearance のサンプル結果、プリセット群を入力として、候補の妥当性判定と pose 選択を行う。実際の node 取得、raycast 呼び出し、キー入力、外部 Mod API 呼び出しは Runtime 側へ残す。

### プリセット作成

SKSE Menu Framework 3を第一候補とし、ユーザーがゲーム内でカメラを動かしながらプリセットを作成・保存できるようにする。編集windowはゲームを止めず、数値操作をその場でカメラへ反映する。自動構図計算を主機能にせず、人間が作った画角に対して見切れ・LOS・クリアランスの安全判定を提供する方針とする。

### シーンアンカー

要件は [`scene-anchor-design.md`](scene-anchor-design.md) に分離した。アンカーは現在のアニメーションに対する撮影基準であり、プリセットの相対座標系と、カメラ候補位置への raycast 起点として使う。

初期計算案では、全参加者の Pelvis node の平均をアンカー位置とする。複数参加者ではアンカーからプレイヤーの Pelvis へ向かう水平ベクトルを向きとし、一人または方向を作れない配置でのみプレイヤーの水平な向きの逆を使う。

アンカーはシーン開始時に計算し、同じアニメーション中は node へ追従させず固定する。アニメーションが変更された場合は、新しいアニメーションが反映された後に再計算し、プリセット候補と raycast 結果も再評価する。シーン終了時には破棄する。

### アニメーション切り替え

シーン内でアニメーションが切り替わるたびに、アンカー、参加者 bounds、有効領域、プリセット候補を再評価する。高い位置のアニメーションから低い位置のアニメーションへ移行した際に、以前の高いカメラ位置をそのまま維持しないことを要件候補とする。

P+の`AnimationChange`を変更開始通知として受信し、1秒後の最初の更新でアンカーを再計算する暫定境界を実装した。P+内部の同期完了通知は外部公開されていないため、この待機は完了保証ではない。stageは独立した更新境界として扱わない。

### 可視性フォールバック案

単なるアイデアとして、プレイヤーが別の参加 NPC に遮られて見えない場合、SLP+ 側に利用可能な透明化機能があれば一時的に呼び出す案を残す。外部 API の存在・契約・復帰保証は未確認であり、採用する場合も Core から直接呼ばず Runtime の互換実装として追加する。

### 未決事項

- Pelvis node を取得できない参加者と、static collision 内に計算されたアンカーの扱い。
- 1秒待機でnode transformの反映が間に合わない実例があるか。
- camera pose実装後、SmoothCam APIを使わないfree camera／photo mode／camera Modとの同時更新が発生しないか。
- 見切れ防止の対象を全身、上半身、主要部位のどこまでにするか。
- LOS とクリアランスから連続領域を作る方法と、計算頻度・性能予算。
- valid なプリセットが0件の場合の復帰方法。
- プリセット切り替え時の位置・回転・FOV の補間。
- 壁際、狭い室内、多人数、極端な身長差での優先順位。
