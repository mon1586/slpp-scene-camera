# Runtime仕様

## 目的

Runtimeは、ゲーム、SKSE、外部Modとの接続を担当する。外部固有の型、イベント名、スレッド境界を`src`が扱える通知と入出力へ変換し、`src`から要求された結果を外部へ反映する。

Runtimeは手続きの順序、待機時間、計算結果の採否を決めない。アンカーをいつ再計算するかなどの判断は`src`が持ち、個別の計算は`core`が持つ。

## SexLab P+入力

SexLab P+が送るSKSE `ModCallbackEvent`を購読し、次の未接頭辞イベントをRuntimeの`SceneEvent`へ変換する。

| 外部イベント | Runtime通知 | 用途 |
| --- | --- | --- |
| `AnimationStarting` | `kAnimationStarting` | シーン開始準備 |
| `AnimationStart` | `kAnimationStart` | 初回アニメーションの同期完了 |
| `AnimationChange` | `kAnimationChange` | P+ hotkeyによる実行中アニメーションの変更開始 |
| `AnimationEnding` | `kAnimationEnding` | シーン終了開始 |
| `AnimationEnd` | `kAnimationEnd` | シーン終了完了 |

`HookAnimationStart`、`HookAnimationChange`、`HookAnimationEnd`も互換入力として同じRuntime通知へ変換する。

`AnimationChange`は変更後の姿勢が利用可能になったことを保証しない。Runtimeは受信直後に通知し、待機や再計算は行わない。

### Scene key

Runtime通知は、送信元QuestのForm IDとP+ thread IDを組み合わせたscene keyを持つ。

- 送信元は`SexLab.esm`で定義されたQuestに限定する。プラグインファイル名の大文字小文字は区別しない。
- thread IDは`strArg`の整数文字列から取得する。
- 互換入力として、有限で`int32`範囲内の`numArg`も受け付ける。
- event名、送信元、thread IDのいずれかが不正な入力は通知しない。

## 参加者と身体中心入力

開始通知を`src`へ渡す前に、Runtimeは送信元QuestのReference Aliasから参加者を収集し、Runtime内部のopaqueなsnapshotとして通知へ付加する。ゲーム固有のhandle型は`src`へ公開しない。

- 同じActorは一度だけ含める。
- プレイヤーを含むかを記録する。
- 最大32人とし、超過時もプレイヤーを優先して保持する。
- 収集とActorHandle化はゲームスレッドで行う。

`src`からアンカー入力を要求された場合、snapshot内のプレイヤーを取得し、現在の姿勢における腰と胸の中間とActorの水平前方を返す。他の参加者の3D有無、骨格、位置はアンカー入力へ影響させない。snapshot内のプレイヤーActor、腰と胸それぞれの有限で有効な位置、または有効な水平前方を取得できない場合は入力を返さない。

`src`から可視性入力を要求された場合、snapshot内の各参加者について、現在の3D全体を囲むワールド境界の中心を身体中心として一つ返す。Actorまたは有効な身体中心を取得できない参加者は、評価不能として識別できる入力を返す。

アニメーション変更時は参加者snapshotを作り直さず、開始時に保持したsnapshotを使う。参加者構成の変更は現在の対象外とする。

## 実行境界

SKSEイベントsinkでは入力の検証とイベント変換だけを行う。変換した通知はSKSE taskを経由して固定長mailboxへ積み、`TESCameraState::Update`の既存処理と後続camera modのchainが完了した後に排出する。

同じcamera updateでの順序は次の通りとする。

```text
既存のTESCameraState::Update chain
  -> mailboxの通知をsrcへ渡す
  -> ポーズ中でなければsrcへ更新を通知する
```

Runtimeは`src`が更新を必要としている間だけ後段処理を呼ぶ。active scene中はwatchdogを維持するため更新を継続する。ポーズ中は更新を通知しない。待機期限がポーズ中に経過した場合、実際の入力取得と計算はポーズ解除後の最初の更新になる。

### Camera update hookのライフサイクル

camera-state update hookは、最初のプレイヤー参加scene開始時に一度だけ設置する。以後はcamera event、scene開始、camera state変更を契機とした再hookを行わず、scene終了、ロード、新規ゲームでも解除しない。active scene外ではhookを残し、mailboxと`NeedsUpdate()`の確認だけで後段処理を終了する。

以前の実装が行っていたcamera eventごとのrefreshと複数世代のSSC chain保持には、同等の先例と必要性を確認できなかった。特定イベントまたは一時的な状態でだけ本処理を行う既存SKSE Modも、一度設置したhookを保持して不要時にoriginalへ流す方式を採用しているため、この疑義を解消する形で一度だけの設置へ変更した。

詳細は[`hook-spec.md`](hook-spec.md)に記載する。

設置後はSexLab P+イベントが発火していない時もcamera updateごとにthunk自体は呼ばれる。ただしinactive pathではoriginal chain、re-entry depth、mailbox、更新要求の確認以外を行わない。

## カメラ出力

カメラ制御はSmoothCamの公開APIを通して取得・更新・解放する。

- Runtimeは`src`から渡されたcamera poseとFOV offsetだけを反映する。
- Runtime自身はcamera poseやアンカー位置を決めない。
- FOV offsetは通常の三人称FOVを基準に適用し、実FOVを`10..170 degree`へ制限する。
- 解放時は必要な復帰処理を行ってからSmoothCamへ所有権を返す。
- 所有中に解放する場合はFOV offsetを解除してからSmoothCamへ所有権を返す。
- 所有権を取得していない場合、解放要求は外部状態を変更しない。

有効なcamera poseとFOV offsetを準備できない場合はカメラ制御を取得しない。

## デバッグ可視化

デバッグモード中は、`src`から渡されたアンカー、プリセット候補位置、可視性ray、身体中心、hit位置、hit法線を現在の画面へ投影して表示する。

- Runtimeはワールド座標を画面上の点と線へ変換するが、位置の決定や結果の採否は行わない。
- アンカーは位置とforward方向を識別できる表示とする。
- ゲーム世界へ参照、一時エフェクト、専用assetを追加しない。
- デバッグモードがOFF、シーン終了、ロード、新規ゲーム、resetのいずれかでは表示しない。
- 表示に失敗した場合はアンカー計算、可視性評価、シーンカメラ手続きを継続する。

## 異常時

- mailboxが満杯になった場合は保留通知を破棄し、次のcamera updateで`src`へresetを要求する。
- ロードまたは新規ゲームではmailboxの世代を更新し、古いtaskと通知を無効化する。
- Runtime境界で例外が発生した場合、通常はatomicなreset要求だけを`src`へ渡し、外部状態の変更を次のcamera update境界まで遅延する。camera update境界自体が失敗した場合だけ、その安全境界内で緊急解放とstate破棄を行う。
- camera-state hookのbatch設置に失敗した場合は、書き込み済みslotを検証付きでrollbackし、状態を`Failed`として以後の設置を行わない。rollbackできず残留したthunkはoriginalだけを呼ぶ。
- scene keyが一致するか、通知を採用するか、処理を終了するかは`src`が判断する。

## 現在扱わないもの

- `StageStart`を独立した更新境界として通知すること。
- P+内部の`OnAnimationSynchronized`をhookまたは改変して完了通知を追加すること。
- `AnimationChange`以外の経路でP+内部のactive sceneが変更されたことを推測すること。
- 身体中心の揺れをRuntime側で平滑化すること。Runtimeは補正前の身体中心とポーズ時間を含まない更新間隔を渡し、撮影アンカーの平滑化はアンカー仕様に従う。
