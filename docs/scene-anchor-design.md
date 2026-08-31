# シーンアンカー設計

状態: **初期設計確定／実装・ゲーム内検証待ち**

## 結論

シーンアンカーを特定 actor の単一 skeleton node には固定しない。初期実装では、全参加者から得た安定した胴体サンプルの中心を原点とし、プレイヤーの actor heading とワールド上方向から直交基底を作る。

アンカーは位置だけでなく、プリセットのローカル座標をワールド座標へ変換する剛体座標フレームである。スケールは持たない。

```text
                         Up (+Z)
                            |
                            |
                    origin  +------ Right
                           /
                          /
                      Forward
```

- `origin`: 参加者ごとの胴体中心を、actor ごとに等しい重みで平均した位置
- `up`: Skyrim のワールド上方向 `(0, 0, 1)`
- `forward`: プレイヤーの actor heading を水平面へ射影した方向
- `right`: `normalize(forward x up)`

プリセットのローカル位置 `(x, y, z)` は、次の式でワールド位置へ変換する。

```text
world = origin + right * x + forward * y + up * z
```

ここで `x` は右、`y` は前、`z` は上である。この軸規約はプリセット保存形式と Core のテストで固定する。

## 解く問題と解かない問題

アンカーが解くのは、シーンと一緒に移動し、プリセットの基準として十分に安定した座標フレームを提供することである。

アンカー自体は次の問題を解かない。

- 全身が画面内に入るかどうか。これは `SubjectBounds` と投影判定の責務である。
- カメラ位置から参加者への LOS や壁とのクリアランス。これは collision query adapter と候補評価の責務である。
- 自動的に良い構図を作ること。アンカーはユーザー作成プリセットを再現する基準にすぎない。
- カメラ pose の補間。アンカーの安定化と、プリセット切り替え時のカメラ補間は別に扱う。

アンカー用 landmark と可視範囲用 landmark も分離する。手足や Head は全身 bounds には必要だが、動きが大きいためアンカー原点へ直接混ぜない。

## 検討した方式

| 方式 | 長所 | 問題 | 判断 |
|---|---|---|---|
| プレイヤー Head の位置・回転 | 実装が単純 | 首振りと animation がカメラ基準へ直結し、床上 pose で回転しやすい | 不採用 |
| プレイヤー Pelvis の位置・回転 | Head より安定 | 単一 actor へ偏り、bone roll/pitch の影響が残る | 単独では不採用 |
| 参加者 bounds の AABB 中心 | 全員を代表しやすい | 手足の大きな動きで原点が揺れ、向きを定義できない | bounds 専用 |
| 参加者の胴体中心 + プレイヤー heading | actor ごとの偏りを抑え、向きの符号が決定的 | node 名と heading の実機安定性を確認する必要がある | 初期方式として採用 |
| 参加者配置の PCA / 主軸 | 多人数配置を表現できる | 対称配置で 180 度反転し、少人数ではノイズに弱い | 将来の補助候補 |

## Controller が収集する生サンプル

Controller adapter は、現在の参加者 snapshot にある live actor handle をゲームスレッドで解決し、各 actor について一つの `torsoCenter` を作る。

初期 node 候補は次のとおりとする。

1. `NPC Pelvis [Pelv]` と `NPC Spine1 [Spn1]` が両方あれば、その中点
2. 片方だけあれば、その node のワールド位置
3. 両方なければ actor reference のワールド位置を degraded sample として使用

node 名は標準 skeleton を前提とする候補であり、vanilla、XPMSSE、独自 race での存在はゲーム内で確認する。候補の追加は Controller の設定に閉じ込め、Core に node 名を持ち込まない。

実装上は CommonLibSSE-NG の `TESObjectREFR::Get3D()` と `NiAVObject::GetObjectByName()` または同等の公開 helper を使い、`NiAVObject::world.translate` をそのフレームの値として読む。3D の再生成や cell unload により無効になるため、node の raw pointer をフレーム間で保持しない。

サンプルには少なくとも次の情報を含める。

```cpp
struct ParticipantAnchorSample {
    Vec3 torsoCenter;
    bool isPrimary;       // 初期版では player
    SampleQuality quality;
};

struct SceneAnchorInput {
    std::span<const ParticipantAnchorSample> participants;
    std::optional<Vec3> primaryForward;
    std::uint64_t epoch;
    float deltaTime;
};
```

これは概念上の型であり、実装時に命名を確定する。RE/SKSE 型は Controller で純粋な Core 型へ変換する。

## 原点の計算

有効かつ有限な `torsoCenter` を actor ごとに一つだけ採用し、その算術平均を raw origin とする。node が二つ取れた actor だけが二倍の重みを持たないよう、先に actor 内で中心を作ってから actor 間で平均する。

player を含む live participant が一人以上あればアンカーを生成できる。参加者の一部が一時的に解決できない場合は残りから degraded anchor を生成するが、player handle 自体が失われた場合は現行 Controller の fail-safe に従ってシーンカメラを終了する。

actor reference 位置への fallback は、特殊 skeleton でもアンカーを完全に失わないためのものとする。ただし足元寄りになるため、後続のプリセット妥当性評価では quality を参照できるようにする。

## 向きの計算

初期版の primary actor は player とする。参加者 alias の順序は役割順である保証をまだ持たないため、配列の先頭を primary にはしない。

Controller は player の actor heading を水平な単位ベクトルへ変換して `primaryForward` として渡す。Core はワールド上方向との直交化、有限値、長さを検証して `forward`、`right`、`up` を構築する。skeleton node の pitch、roll、scale はアンカー基底へ伝播させない。

向きは毎フレーム bone animation に追従させず、`epoch` の開始時に採用してその epoch 中は固定する。これにより actor の細かな向き変化でプリセット全体が周回することを防ぐ。

- シーン開始で新しい epoch を作る。
- P+ の animation / stage 境界を検出できるようになったら、その境界でも epoch を更新する。
- 境界イベントを実装するまでは、向きはシーン開始時の値を維持し、原点だけを動的に更新する。
- epoch 開始時に heading が取れない場合は前 epoch の向きを維持する。シーン最初の epoch でも取れない場合はワールド `+Y` を degraded fallback とする。

stage 境界で新しい向きへ切り替える際のカメラ移動は、アンカー補間ではなく pose transition として扱う。アンカーの意味を中間状態で歪めないためである。

## 安定化と欠損処理

raw origin は胴体 animation の小さな揺れを含むため、受理済み origin に対して時間基準の指数平滑を行う。フレームレート依存の固定係数は使わない。

初期 tuning 候補は次のとおりとする。これらは仕様値ではなく、ゲーム内計測で確定する。

- origin の half-life: `100-150 ms`
- 全サンプル欠損時に前回アンカーを保持する猶予: `250 ms`
- cell/load 境界、長い frame gap、scene generation 変更: 即時破棄
- stage epoch 変更または大きな空間的不連続: 新しい raw origin へ再 seed

不連続判定の距離は固定値だけで決めず、参加者 bounds の半径と組み合わせる。初期候補は `max(200 game units, 2 * groupRadius)` である。これも実測対象とする。

安定化ロジックは暗黙の singleton state にせず、前状態を入力、次状態を出力する純粋な Core 関数として表現する。同じ入力列から同じ結果を再現でき、単体テストで scene/epoch reset を検証できるようにする。

## レイヤー境界

```text
Controller / game thread
  actor handle 解決
  3D と node の取得
  actor heading の変換
  stage/scene epoch の発行
              |
              v
Core
  actor ごとの等重み集約
  直交基底の構築と検証
  origin の安定化
  local <-> world 変換
              |
              v
Preset validation / rig
  SubjectBounds の画面内判定
  LOS / clearance 判定
  CameraPose の選択
```

`SceneAnchor` は次の最小契約を持つ。

```cpp
struct SceneAnchor {
    Vec3 origin;
    Vec3 right;
    Vec3 forward;
    Vec3 up;
    AnchorQuality quality;
};
```

`AnchorQuality` は少なくとも `full`、`degraded`、`held` を区別し、invalid は `std::optional` など値の不在で表す。anchor へ `RE::Actor*`、node pointer、FormID、SmoothCam API object を格納しない。

## 更新位置と性能予算

サンプリングは現行 `TESCameraState::Update` hook の original と SmoothCam 更新が完了した後、Controller のゲームスレッド処理内で行う。この時点の skeleton world transform を読む。

node tree lookup は参加者数に比例するため、次の制約を置く。

- 固定長 participant snapshot を再利用し、hot path で動的確保しない。
- actor ごとのアンカー node lookup は最大二つとする。
- raw node pointer をキャッシュせず、必要ならサンプリング頻度を 30 Hz 程度へ制限して間のフレームは安定化済みアンカーを再利用する。
- quality や fallback のログは状態変化時だけ出し、毎フレーム出力しない。

サンプリング間引きの要否は計測して決める。先に 30 Hz を必須仕様にはしない。

## ライフサイクルと失敗時の扱い

- `AnimationStart` で ownership を取得した後、最初の有効サンプルから tracker を初期化する。
- scene key または mailbox generation が変わったら、以前の anchor と smoothing state を再利用しない。
- 全サンプル欠損が猶予を超えた場合は pose を生成せず、Controller が SmoothCam へ復帰する。
- degraded anchor は即時終了条件にはしない。ただし安全な preset が一件も得られなければ、後続の preset selection 方針に従う。
- pause 中は現行どおり更新を止め、unpause 後の大きな `deltaTime` を smoothing にそのまま入れず再 seed 判定へ送る。

## 検証計画

### Core 単体テスト

- participant ごとの node 数に関係なく actor が等重みになる。
- `right`、`forward`、`up` が有限、単位長、相互直交で、規定の handedness を持つ。
- local `+X/+Y/+Z` がそれぞれ right/forward/up へ変換される。
- NaN、ゼロ長 heading、欠損 participant を拒否または degraded にする。
- epoch 中は heading が変化しても基底が変わらず、epoch 更新時だけ再構築される。
- scene generation 変更、長い frame gap、空間的不連続で tracker が再 seed される。
- 短い欠損は `held`、猶予超過は invalid になる。

### ゲーム内検証

- 立位、床上、家具使用で原点が参加者群の胴体付近を追従する。
- stage 内の Head、手足、胸部 animation で anchor 基底が揺れない。
- stage 切り替え、位置合わせ、actor 回転で不自然な長距離追従や 180 度反転を起こさない。
- vanilla / XPMSSE skeleton と少なくとも一つの独自 race で node fallback と quality を確認する。
- 1 人、2 人、3 人以上、極端な身長差で actor 等重みの原点が妥当である。
- actor の一時 unload、3D 再構築、pause/load を跨いで古い node pointer や古い anchor を使用しない。

### 初期実装の完了条件

- 原点と三軸をログまたは固定テストプリセットから判別できる。
- 単一 Head 固定より明らかに小さい jitter で参加者群へ追従する。
- scene/epoch を跨いで以前のアンカーが混入しない。
- node 欠損または無効値で CTD せず、degraded fallback または SmoothCam 復帰になる。

## 残る調査事項

- P+ から animation / stage 境界と animation identifier を安定して取得する方法。
- player actor heading が各種 alignment と furniture animation でどの時点から安定するか。
- 標準 node 名の race / skeleton ごとの存在率と、fallback 優先順位。
- origin half-life、欠損猶予、不連続閾値の実測 tuning。
- primary を player 以外の animation role に切り替える必要があるか。これは role 情報を取得できてから再検討する。
