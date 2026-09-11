# 固定プリセット読み込みとカメラ制御の設計

状態: **schema version 5／表示名をIDから分離**

## 目的

特定のファイルから固定カメラ構図を読み込み、プレイヤー参加シーンのアンカー確定後にSmoothCamからカメラ制御を取得して、画面相対のframing offsetを保ったままアンカーをorbitする位置から映す。

本書の保存形式と構図の定義は現在の仕様として使用する。シーン全体の動作は[`scene-camera-procedure.md`](scene-camera-procedure.md)、可視性とA/D選択は[`clearance-design.md`](clearance-design.md)、編集操作は[`preset-settings-spec.md`](preset-settings-spec.md)に従う。失敗時の動作もシーンカメラ手続きへ集約する。

## 最小成立条件

次のすべてを満たした場合だけカメラ制御を取得する。

1. 固定ファイルを読み込める。
2. 1件以上の有効なプリセットを取得できる。
3. 現在のシーンアンカーが確定している。
4. プリセットのframing offsetとorbitをworld poseへ変換できる。
5. world位置からframing centerを見る有限な回転を生成できる。
6. SmoothCamがカメラ制御を渡せる。

いずれかが失敗した場合はカメラを取得せず、通常のSmoothCam表示を維持する。

通常表示の初期選択では可視性仕様の使用可能条件も満たす必要がある。編集用previewは、可視性を理由に禁止しない。

## 固定ファイル

保存ファイルは次の1個に固定する。

```text
Data/SKSE/Plugins/SexlabSceneCamera/presets.json
```

このファイルはユーザーがUIから最初のプリセットを作成した時点で生成するユーザーデータであり、ビルド出力および配布物には含めない。インストールまたは更新によってゲーム環境の既存ファイルを上書きしない。UIから同じファイルを明示的にreloadできるが、複数ファイルの探索や別ファイルによる階層的な上書きは行わない。

構造化されたプリセット配列と将来のCRUDを扱いやすくするためJSONを使用する。既存の依存関係に`nlohmann-json`が含まれているため、新しいparser依存は追加しない。

## schema version 5

SKSE Menuに表示する設定と操作の仕様は[`preset-settings-spec.md`](preset-settings-spec.md)に分離する。以下は固定プリセット読込で使用する保存形式である。

```json
{
  "schemaVersion": 5,
  "presets": [
    {
      "id": "default",
      "name": "Default",
      "framingOffset": {
        "right": 0.0,
        "up": 0.0
      },
      "orbit": {
        "yawDegrees": 0.0,
        "pitchDegrees": 16.699244,
        "distance": 208.80613
      },
      "fovOffsetDegrees": 0.0
    }
  ]
}
```

- `schemaVersion`は必須とし、`5`のみ読み書きする。旧形式の自動移行は提供しない。
- `presets`は配列とし、全件を読み込む。通常表示では最初の使用可能候補を初期選択し、その後のA/Dと選択維持は可視性仕様に従う。
- `id`は空でないUTF-8文字列とし、ファイル内で一意にする。後続CRUDでも同じIDを使用する。
- `name`は必須の表示名で、空でない127バイト以内のUTF-8文字列とする。同名は許可する。改名しても`id`は変わらない。
- `framingOffset.right`は現在のcamera right方向、`framingOffset.up`は現在のcamera up方向にframing centerを移動する量である。有限な数値とし、単位はSkyrim unitとする。
- `orbit.yawDegrees`は注視中心を回る水平角で、`-180`から`180`度とする。アンカーforwardはプレイヤーActorの水平前方の逆を指すため、`0`度ではプレイヤー正面側、`-180`または`180`度では背面側にカメラを置く。
- `orbit.pitchDegrees`は仰俯角で、`-90`から`90`度とする。正値ではカメラを注視中心より上へ置く。
- `orbit.distance`は注視中心からカメラまでの距離で、有限かつ0より大きいSkyrim unitとする。
- `fovOffsetDegrees`はユーザーの通常の三人称FOVへ加える相対値で、有限かつ`-160`から`160`度とする。実際のFOVは`10`から`170`度へ制限する。
- rollは保存しない。カメラは常にframing centerを見る。
- 未知のトップレベルまたはプリセット項目は、同じschema version内の後方互換な拡張として無視する。
- 必須項目の欠落、型違い、非有限値、重複IDはファイル全体の読み込み失敗とする。部分的な採用は初版では行わない。

## 座標変換

アンカーの水平forwardを`F`、world上方向を`U = (0, 0, 1)`、右方向を`R = F × U`とする。yawを`y`、pitchを`p`とすると、画面座標系は次になる。

```text
viewForward = R * (-sin(y) * cos(p))
            + F * ( cos(y) * cos(p))
            + U * (-sin(p))

cameraRight = R * cos(y) + F * sin(y)
cameraUp = cameraRight × viewForward

framingCenter = anchor.position
              + cameraRight * framingOffset.right
              + cameraUp * framingOffset.up

cameraPosition = framingCenter - viewForward * distance
```

現在のアンカー実装ではforwardが水平かつ正規化済みなので、`R`も水平な単位ベクトルになる。たとえば`F = (0, 1, 0)`なら`R = (1, 0, 0)`である。framing offsetは画面平面に固定されるため、yawやpitchを変更してもアンカーの画面内オフセットは変わらない。

view forward方向への平行移動は、カメラ位置と向きを計算するとdistance変更と同じ結果になる。そのためversion 3ではforward offsetを持たない。

カメラの視線方向は次で求める。

```text
viewForward = normalize(framingCenter - cameraPosition)
```

この方向とworld上方向から、既存の`CameraPose.rotation`へ渡す直交回転行列をCoreで生成する。distanceが0以下、正規化できない、行列に非有限値が生じる場合はpose生成失敗とする。真上・真下から見る配置ではworld上方向との外積が退化するため、アンカーrightを補助軸として使用する。

## 読み込み時期と保持

- `kDataLoaded`で初回読込を行い、検証済みプリセットのimmutable snapshotを保持する。以後はUIから明示的にreloadできる。
- camera update hook内ではファイルI/OやJSON parseを行わない。
- 初回読込でファイルが存在しない、開けない、parseできない、schema検証に失敗した場合はエラーをログへ記録し、snapshotを空にする。reload失敗時は最後に有効だったsnapshotを維持する。
- プリセット読み込み失敗だけではDLLのロードを失敗させない。
- 外部変更は自動監視せず、明示的なreload時だけ反映する。

## 責務分割

### Runtime

- 固定パスからファイルを読み込む。
- JSONをRuntimeの値型へ変換し、schemaと基本的な入力値を検証する。
- 読み込み結果のsnapshotを`src`へ提供する。
- 既存のSmoothCam APIを使ってカメラ制御を取得し、poseとFOV offsetを反映してから、復帰、解放する。
- ファイルI/O、JSON、SmoothCam、ゲーム型をCoreへ公開しない。

### Core

- アンカー、画面相対framing offset、orbitからworld位置を計算する。
- world位置からframing centerを見る回転行列を計算する。
- 非有限値と退化した入力を拒否する。
- ファイル、SmoothCam、ゲーム状態を知らない。

### src

- アンカー確定後に可視性仕様に従って初期プリセットを選ぶ。
- Coreへpose計算を要求する。
- pose生成成功後だけRuntimeへカメラ取得と反映を要求する。
- シーンとカメラ所有権のライフサイクルを管理する。

## カメラ利用時の契約

- 読み込み成功と、現在のシーンでの使用可能性を分ける。データが正常でも可視性条件で初期選択されない場合がある。
- 通常表示の初期選択・A/D・選択維持は[可視性仕様](clearance-design.md)に従う。
- 編集用previewは[設定仕様](preset-settings-spec.md)に従う。使用不能なプリセットも編集できる。
- 入力欠落、構図生成失敗、取得拒否、表示失敗、所有権喪失は[シーンカメラ手続き](scene-camera-procedure.md)の失敗表に従う。
- ファイルの初回読込失敗は空の一覧として扱い、Reload失敗は最後の正常な一覧を維持する。いずれもDLL自体のロード失敗にはしない。
- シーンカメラの終了時には、所有している場合にFOV offsetを解除して制御を返す。
- ファイルの自動監視、複数ファイルによる上書き、roll、プリセット間の補間は提供しない。

## テスト

### Core test

- アンカー位置が他の参加者に依存せず、プレイヤーの身体中心になる。
- アンカーforwardがプレイヤーActorの水平前方の逆になる。
- プレイヤーの身体中心または水平前方が無効な場合はアンカーを生成しない。
- 身体中心が変化した場合は、同じ相対構図を保ってcamera poseも追従する。
- `forward = +Y`のアンカーでframing offsetとorbitが期待するworld位置になる。
- アンカーforwardが回転した場合も同じローカル構図になる。
- 生成した回転の視線方向がframing centerを向く。
- framing right/upがyaw・pitch変更後も同じ画面内成分になる。
- framing offset 0ではyaw、pitch、distanceとworld位置の相互変換が一致する。
- 真上・真下のカメラ位置でも有限な回転を生成する。
- distance 0、非有限値、退化入力を拒否する。

### Loader test

- 正常なschemaを全件読み込める。
- ファイルなし、壊れたJSON、未知version、必須項目欠落、型違い、非有限値、重複IDを拒否する。
- 未知の追加項目を無視できる。
- schema version 5の表示名とFOV offsetを読み書きでき、必須項目欠落、非有限値と範囲外を拒否する。

### ゲーム内確認

1. 固定ファイルを配置してプレイヤー参加シーンを開始する。
2. デバッグモードのHUDアンカーがプレイヤーActorの水平前方の逆を向き、Yaw `0`が正面側、Yaw `±180`が背面側になることを確認する。
3. 人型とクリーチャーを含むシーンでも、他参加者の骨格に影響されずプレイヤーの腰・胸からアンカーを取得できることを確認する。プレイヤーの必要部位を取得できなければ新しいアンカーを生成しない。
4. アンカー確定後にカメラが指定orbit位置へ移動し、framing centerを向くことを確認する。
5. プレイヤーの移動と姿勢変化にアンカーとカメラが追従し、取得不能なフレームでは直前の構図を維持することを確認する。
6. FOV offsetの正負で画角が広がる、狭まること、および実FOVが`10..170 degree`を越えないことを確認する。
7. シーン終了後にFOV offsetが残留せずSmoothCamへ戻ることを確認する。
8. ファイル削除、不正JSON、極端な値で、カメラを取得せずゲームが継続することを確認する。

ビルド成功はファイルパス、カメラ行列、SmoothCamとの実行順を保証しないため、上記は実機確認を完了条件とする。
