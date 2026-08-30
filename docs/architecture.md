# アーキテクチャ

## 目的

SexLab P+ のプレイヤー参加シーンが始まったら SmoothCam からカメラ制御を取得し、独自のシーンカメラへ切り替える。シーン終了時には独自カメラを停止し、SmoothCam に制御を返す。

初期実装はネイティブ SKSE プラグインから SexLab P+ の `ModCallbackEvent` を受信する方針とし、専用 Papyrus スクリプトや ESP は追加しない。イベントの発火順序とペイロードは POC で実機確認し、期待どおり取得できない場合だけ連携方法を再検討する。

## 設計原則

- **Controller レイヤー**に、カメラ制御の取得・切り替え・終了・復帰をすべて閉じ込める。
- **Core レイヤー**は、シーンをどう写すかだけを決定する。
- 依存方向は `Controller -> Core` の一方向とする。
- Core は SexLab P+、SmoothCam、SKSE のイベント、カメラ所有権を知らない。
- Head を固定アンカーにはしない。最優先の画作り要件は、対象キャラクターが画角から見切れないことである。

## 全体構成

```text
SexLab P+ ModCallbackEvent
            |
            v
+---------------- Controller layer ----------------+
| イベント判定 -> 状態管理 -> SmoothCam 制御取得   |
|                    |                              |
| 参加者情報の収集 --+--> Core 呼び出し --> Pose適用|
|                                                   |
| シーン終了 -> カメラ復帰 -> SmoothCam 制御解放   |
+------------------------|--------------------------+
                         v
                 +-- Core layer --+
                 | 構図・位置・向き |
                 | FOV・補間（将来）|
                 +-----------------+
```

## Controller レイヤー

Controller は「いつ独自カメラに切り替え、いつ元へ戻すか」の唯一の責任者である。

### 責務

- SexLab P+ のシーンイベントを受信する。
  - 想定イベント: `AnimationStarting`、`AnimationStart`、`AnimationEnding`、`AnimationEnd`
  - `sender` と thread ID をシーン識別子として保持する。
- シーン参加者を取得し、プレイヤーが含まれるシーンだけを対象にする。
- イベント sink では必要な識別情報だけを固定長 mailbox に保存し、カメラや NiNode の操作は `PlayerCamera::Update` のカメラ更新スレッドへ渡す。
- SmoothCam の公式 API を通じてカメラ制御を取得する。
- 復帰に必要な状態を保持し、終了時に SmoothCam の目標位置へ戻してから制御を解放する。
- 毎フレーム、参加者の位置・可視範囲などを `CameraFrameInput` に変換して Core へ渡す。
- Core が返した `CameraPose` を実際のゲームカメラへ適用する。
- 重複開始、古い終了イベント、NPC のみのシーン、制御取得失敗を安全に無視または復旧する。
- ロード、新規ゲーム、シーン中断などでもカメラ制御を保持したままにしない。

SmoothCam API の呼び出しとゲームカメラへの書き込みは Controller 内部の adapter だけが行う。Core からこれらの API を直接呼び出してはならない。

### 状態機械

| 状態 | 意味 | 主な遷移 |
|---|---|---|
| `Idle` | SmoothCam が通常どおりカメラを制御している | 対象シーンの開始準備で `Preparing` |
| `Preparing` | 参加者とシーン識別子を確認し、切り替え準備中 | 制御取得成功で `Active`、失敗・対象外で `Idle` |
| `Active` | 独自カメラを適用中 | 対応する終了イベントで `Restoring` |
| `Restoring` | SmoothCam へ安全に復帰中 | 解放完了または復旧処理完了で `Idle` |

すべての終了イベントは、現在のシーン識別子と一致した場合だけ状態を変更する。どの失敗経路からでも、所有しているカメラ制御を解放して `Idle` に戻れるようにする。

### 想定コンポーネント

- `SexLabPEventSource`: `ModCallbackEvent` を受信し、シーンイベントへ正規化する。
- `SceneCameraController`: 状態機械とシーンのライフサイクルを管理する。
- `SmoothCamOwnership`: SmoothCam の制御取得、復帰、解放を隠蔽する。
- `CameraOutput`: `CameraPose` をゲームカメラへ適用する。
- `GameThreadMailbox`: イベント受信側から安全な更新コンテキストへ命令を渡す。

イベントから毎フレーム更新へつなぐ際、`AddTask` の自己再投入ループは使わない。連続更新はカメラ更新コールバックまたは更新 hook で行い、イベント sink は開始・終了要求の通知だけに限定する。

SmoothCam の所有権 API は SmoothCam 自身が初期化されたスレッドから呼ぶ必要がある。実機ログでは SKSE `AddTask` の実行スレッドが SmoothCam のスレッドと一致しなかったため、`AddTask` はこの境界には使用しない。`SexLabEventSink` とロード関連メッセージは値だけを `SceneEventMailbox` へ積み、`CameraHook` が `PlayerCamera::Update` 内で mailbox を排出してから状態機械とカメラ出力を更新する。mailbox が満杯になった場合は、終了イベントの取りこぼしによる所有権残留を避けるため次のカメラ更新で強制リセットする。

### SexLab P+ イベント境界

公開 API が保証する非同期 hook は `HookAnimationStart` / `HookAnimationEnd` で、Papyrus の受信引数は `(int aiThreadID, bool abHasPlayer)` である。一方、現在の P+ 同梱 `sslThreadModel.psc` はその公開 hook と同時に、`SendModEvent("AnimationStart", thread_id)` / `SendModEvent("AnimationEnd", thread_id)` という未接頭辞の互換イベントも送っている。POC のネイティブ sink は後者を受信する。

SKSE の `Form.SendModEvent` は `(eventName, strArg, numArg)` なので、上記の2引数呼び出しでは thread ID は数値引数ではなく `strArg` に文字列として格納される。Controller は `strArg` を整数として解析し、他の sender との互換性のため `numArg` をフォールバックにする。`sender` は thread quest の FormID として保持し、thread ID と組み合わせて古い終了イベントを排除する。

この未接頭辞イベントは公開 API には記載されていないため、POC でイベント名・`sender`・payload を必ずログ検証する。将来 P+ がこの互換イベントを削除した場合は、公式 `HookAnimationStart` / `HookAnimationEnd` を受ける最小 Papyrus bridge を Controller 入力 adapter として追加する。Core には影響させない。

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
    Rotation rotation;
    float fov;
};

CameraPose Evaluate(const CameraFrameInput& input);
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

第1段階では画作りの品質を評価しない。参加者群の中心を基準に、固定の大きな Z オフセットを加えた上空位置へカメラを移し、下方へ向ける。参加者情報を取得できない場合はプレイヤー位置を基準にする。

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
