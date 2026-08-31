# アーキテクチャ

## 目的

SexLab P+ のプレイヤー参加シーンが始まったら SmoothCam からカメラ制御を取得し、独自のシーンカメラへ切り替える。シーン終了時には独自カメラを停止し、SmoothCam に制御を返す。

初期実装はネイティブ SKSE プラグインから SexLab P+ の `ModCallbackEvent` を受信する方針とし、専用 Papyrus スクリプトや ESP は追加しない。イベントの発火順序とペイロードは POC で実機確認し、期待どおり取得できない場合だけ連携方法を再検討する。

## 設計原則

- **Controller レイヤー**に、カメラ制御の取得・切り替え・終了・復帰をすべて閉じ込める。
- **Core レイヤー**は、シーンをどう写すかだけを決定する。
- 依存方向は `Controller -> Core` の一方向とする。
- Core は SexLab P+、SmoothCam、SKSE のイベント、カメラ所有権を知らない。
- 外部のシーン Mod とカメラ Mod には、それぞれ `ISceneController` と `ICameraController` を通して依存する。
- `SceneCameraCoordinator` は具象 Mod adapter を知らず、composition root だけが interface と具象を接続する。
- Head を固定アンカーにはしない。最優先の画作り要件は、対象キャラクターが画角から見切れないことである。

## 全体構成

```text
SexLab P+ -> SexLabPSceneController -> ISceneController --+
                                                          |
                                                          v
                                              SceneCameraCoordinator -> Core
                                                          |
                                                          v
SmoothCam <- SmoothCamCameraController -> ICameraController
```

## Controller レイヤー

Controller レイヤーは、外部 Mod adapter とシーンカメラの調停を分離する。

### 責務

- `ISceneController` は、シーン Mod 固有のイベントと参加者取得を共通の scene key、イベント、participant snapshot へ変換する。
- `ICameraController` は、カメラ Mod 固有の所有権取得、pose 適用、復帰、解放を共通操作として提供する。
- `SceneCameraCoordinator` は両 interface と Core だけに依存し、対象シーンの選別、状態遷移、Core 評価、fail-safe を調停する。
- composition root は使用する `ISceneController` と `ICameraController` の具象を選択し、`SceneCameraCoordinator` へ注入する。
- イベント sink では送信元と payload の検証だけを行い、ゲーム状態の参照と hook 導入は一回限りのゲームスレッド task へ渡す。
- quest alias の走査はゲームスレッド task で固定長 snapshot に変換し、カメラ更新 hook 内では alias lock や動的確保を行わない。
- 毎フレーム、参加者の位置・可視範囲などを `CameraFrameInput` に変換して Core へ渡す。
- 重複開始、古い終了イベント、NPC のみのシーン、制御取得失敗を安全に無視または復旧する。
- `AnimationEnding`、参加者消失、非対応カメラ状態、所有権喪失、watchdog、ロード、新規ゲームのいずれからでも制御を残留させない。

初期構成では `SexLabPSceneController` が `ISceneController` を、`SmoothCamCameraController` が `ICameraController` を実装する。別のシーン Mod またはカメラ Mod へ対応するときは具象 adapter を追加し、Coordinator と Core は変更しない。

### 状態機械

| 状態 | 意味 | 主な遷移 |
|---|---|---|
| `Idle` | SmoothCam が通常どおりカメラを制御している | 対象シーンの開始準備で `Preparing` |
| `Preparing` | 参加者とシーン識別子を確認し、切り替え準備中 | 制御取得成功で `Active`、失敗・対象外で `Idle` |
| `Active` | 独自カメラを適用中 | 対応する終了イベントで `Restoring` |
| `Restoring` | SmoothCam へ安全に復帰中 | 解放完了または復旧処理完了で `Idle` |

すべての終了イベントは、現在のシーン識別子と一致した場合だけ状態を変更する。どの失敗経路からでも、所有しているカメラ制御を解放して `Idle` に戻れるようにする。

### コンポーネント

- `ISceneController`: シーンイベントと参加者取得の外部 Mod 非依存 port。
- `SexLabPSceneController`: `ModCallbackEvent` と quest alias を `ISceneController` の契約へ変換する adapter。
- `ICameraController`: カメラ所有権と pose 出力の外部 Mod 非依存 port。
- `SmoothCamCameraController`: SmoothCam API とゲームカメラ出力を `ICameraController` の契約へ変換する adapter。
- `SceneSession`: Skyrim API から独立した純粋な状態機械として、scene key と遷移だけを管理する。
- `SceneCameraCoordinator`: 2つの port、参加者 snapshot、Core を調停し、`SceneSession` を駆動する。
- `CameraOutput`: `SmoothCamCameraController` 内部で `CameraPose` をゲームカメラへ適用する。
- `SceneEventMailbox`: イベント受信側から安全な更新コンテキストへ命令を渡す。

イベントから毎フレーム更新へつなぐ際、`AddTask` の自己再投入ループは使わない。連続更新はカメラ更新コールバックまたは更新 hook で行い、イベント sink は開始・終了要求の通知だけに限定する。

初回実装では `GetSmoothCamThreadId()` と現在スレッドをプラグイン側で比較していたが、導入済み SmoothCam の API 実装は所有権を atomic に管理し、`RequestCameraControl` 自体にそのスレッド制約はない。この先行拒否は削除し、API が返す `BadThread` を含む結果値をそのまま扱う。

`PlayerCamera::Update` の vtable hook は AE 1.6.1170 で呼ばれないことを実機ログで確認したため使用しない。代わりに、SmoothCam V2 が使用可能で、かつ最初のプレイヤー参加シーンをゲームスレッド上で確認した時点で、各 `TESCameraState::Update` vtable を SmoothCam の後段から hook する。hook はゲームと既存 Mod の更新を先に呼び、次に mailbox を排出し、最後に状態機械とカメラ出力を更新する。

各 vtable slot には専用 thunk と専用 original を割り当てる。後発 Mod が hook 済み vtable を複製しても original の探索を vtable アドレスに依存させず、後発 Mod が slot を再差し替えした場合は camera-state 遷移または次回シーン開始時に再走査して新しいチェーンを作る。チェーン中に旧 thunk と新 thunk の両方が含まれる場合も、thread-local の入れ子判定により SSC の後処理は最外周で一度だけ行う。

Idle かつ mailbox が空なら、hook は original 呼び出し後に atomic 判定だけで戻る。mailbox が満杯の場合は次の hook で強制リセットする。ロード境界では generation を更新して旧世代イベントを破棄し、所有権解放要求は mailbox の次回排出だけに依存させない。

### SexLab P+ イベント境界

公開 API が保証する非同期 hook は `HookAnimationStart` / `HookAnimationEnd` で、Papyrus の受信引数は `(int aiThreadID, bool abHasPlayer)` である。一方、現在の P+ 同梱 `sslThreadModel.psc` はその公開 hook と同時に、`SendModEvent("AnimationStart", thread_id)` / `SendModEvent("AnimationEnd", thread_id)` という未接頭辞の互換イベントも送っている。初期 `ISceneController` adapter は後者を受信する。

SKSE の `Form.SendModEvent` は `(eventName, strArg, numArg)` なので、上記の2引数呼び出しでは thread ID は数値引数ではなく `strArg` に文字列として格納される。`SexLabPSceneController` は `strArg` を整数として解析し、他の sender との互換性のため `numArg` をフォールバックにする。`sender` は thread quest の FormID として共通 scene key へ変換し、thread ID と組み合わせて古い終了イベントを排除する。

この未接頭辞イベントは公開 API には記載されていない。共有 dispatcher 上の同名イベントによる誤作動を防ぐため、sender は `TESQuest` かつ定義元ファイルが `SexLab.esm` の場合だけ受理する。将来 P+ が互換イベントを削除した場合は、公式 `HookAnimationStart` / `HookAnimationEnd` を受ける別の `ISceneController` adapter を追加する。Coordinator と Core には影響させない。

## Core レイヤー

Core は「対象をどう画面に収めるか」を計算する、副作用のないカメラロジックである。

### 入出力境界

```cpp
struct CameraFrameInput {
    std::vector<SubjectBounds> subjects;
    CameraPose currentPose;
    Viewport viewport;
    float deltaTime;
};

struct CameraPose {
    Vec3 position;
    RotationMatrix rotation;
    float fov;
};

std::optional<CameraPose> Evaluate(const CameraFrameInput& input);
```

型名は概念を示すもので、実装時に確定する。

### 責務

- 全参加者または選択された主要人物がセーフフレーム内に収まる構図を決める。
- 参加者群から注視点、カメラ位置、向き、距離、FOV を計算する。
- 必要に応じて移動や回転を補間する。
- 入力が一時的に欠けた場合のフォールバックを決める。

`SubjectBounds` は Head 一点を意味しない。actor の境界、root、pelvis、chest、head など複数のサンプルから、画面に収めるべき範囲を Controller 側で収集して渡せる形にする。どの点を実際に使うか、全身を常に収めるか、主要人物を優先するかはロードマップ第2段階で決める。

### 非責務

- SexLab P+ イベントの購読や解釈
- プレイヤー参加シーンかどうかの判定
- SmoothCam の所有権取得・解放
- ゲームカメラへの直接書き込み
- スレッド切り替えとライフサイクル管理

## POC 用 Core

第1段階では画作りの品質を評価しない。参加者群の中心を基準に、固定の大きな Z オフセットを加えた上空位置と真下向き回転を Core が返す。参加者情報を取得できない場合はプレイヤー位置を基準にし、それも無効なら pose を返さず Controller が復帰する。`CameraOutput` は Core の pose を機械的にゲーム型へ写すだけで、構図を決めない。

この挙動の目的は、独自カメラへ制御が切り替わったことを見た目で明確に確認することだけである。高さ、FOV、補間方法は POC の仮値とし、正式なカメラ機能には引き継がない。

## 不変条件

- Controller は SmoothCam の制御取得に成功したときだけ独自カメラを書き込む。
- Controller は自分が所有している場合だけカメラ制御を解放する。
- NPC のみのシーンではカメラを切り替えない。
- 終了イベントは現在アクティブなシーンと照合する。
- Core は外部 API を呼ばず、同じ入力に対して再現可能な `CameraPose` を返す。
- 正常終了・異常終了のどちらでも、最終的に SmoothCam へ制御を返す。

## POC で検証が必要な事項

- SexLab P+ の各 ModEvent の実際の発火順序、thread ID、`sender` の内容。
- thread ID が `strArg` に10進文字列として届くこと。
- `sender` の quest alias から参加者とプレイヤーを安定して特定できるか。
- SmoothCam の制御取得・目標位置への復帰・解放の呼び出し順序。
- イベント受信スレッドからカメラ更新スレッドへの受け渡し。
- SexLab の自動フリーカメラ機能と競合しない設定または制御方法。
