# Camera update hook仕様

状態: **実装済み／実機未確認**

## 目的

`TESCameraState::Update`を、SexLab P+のscene通知を安全なゲーム更新境界で処理し、SmoothCamを含む既存camera chainの出力後にscene camera処理を実行するために使用する。

このhookはsceneの開始を検出するものではない。sceneの開始、変更、終了はSexLab P+のイベントから取得し、camera updateは通知の排出とactive scene中の継続更新だけを担当する。

## 採用するライフサイクル

- DLLロード時にはcamera update hookを設置しない。
- 最初の有効なプレイヤー参加sceneの`AnimationStarting`または`AnimationStart`を受信した時、ゲームスレッド上で一度だけ設置する。
- 設置後はゲームプロセス終了まで保持する。
- scene終了、ロード、新規ゲーム、camera state変更では解除しない。
- camera eventを監視した再hook、chain refresh、追加thunkの積み増しは行わない。
- install失敗後の再試行は行わず、そのプロセスではscene camera機能を無効にする。

最初の対象sceneまではSSCのthunkはcamera updateから呼ばれない。設置後はactive scene外でも、現在のcamera stateが更新されるたびにthunkが呼ばれる。その場合は既存chainを実行した後、固定コストの条件判定だけで終了する。

## hook対象

- 対象は`PlayerCamera::cameraStates`に保持されている各`TESCameraState`実体の現在のvtableとする。
- 対象virtualは`TESCameraState::Update`とする。
- SE/AEではvtable slot `0x03`を使用する。
- 現在のPOCと同様にVRは対象外とする。VR対応時はslotを別途検証する。
- nullのstateは無視する。
- 同じvtableを共有するstateはvtable addressで重複排除し、1回だけpatchする。
- 静的な`RE::VTABLE_*`ではなく、実体が現在参照しているvtableを使用する。これはSmoothCamなどが作成した既存chainを取り込むためである。

保持するhook entry数の上限は`RE::CameraStates::kTotal`とする。再hookしないため、同じvtableに対する複数世代のentryは持たない。

## 設置タイミング

SexLab P+ event sinkではhookを直接設置しない。eventをSKSE taskへ渡し、次の順で処理する。

```text
AnimationStarting / AnimationStartを受信
  -> SKSE taskへ移送
  -> scene keyの世代を確認
  -> 参加者snapshotを収集
  -> プレイヤー不参加ならhookを設置せず終了
  -> 未設置ならcamera stateの現在のvtableへ一度だけ設置
  -> 設置成功後にscene eventをmailboxへ積む
  -> 次のcamera updateでmailboxを排出
```

この遅延設置により、`kNewGame`または`kPostLoadGame`で行われるSmoothCamのcamera hook設置後にSSCを外側へ追加する。SSCが取得したoriginalは、その時点で存在するSmoothCamと他のcamera modのchain先頭になる。

プレイヤー不参加sceneはSSCの処理対象にならないため、未設置状態では通知も保持しない。設置済みの場合は従来どおり通知を`src`へ渡し、active sceneとの一致判定は`src`に任せる。

## 設置手順

設置状態は次の3状態だけを持つ。

```text
NotInstalled -> Installed
NotInstalled -> Failed
```

`Installed`と`Failed`はプロセス終了まで終端状態とする。

設置処理は次を満たす。

1. `PlayerCamera`、runtime client、scene sourceが利用可能であることを確認する。
2. 全camera stateから重複のないvtable、Update slot、現在の関数pointerを収集する。
3. null pointer、entry上限超過、候補0件をpatch前に失敗させる。
4. entryごとにoriginalと専用thunkの対応を先に確定する。
5. slotが収集時のpointerと一致する場合だけverified writeでthunkへ置き換える。
6. 全entryのpatch成功後にだけ状態を`Installed`として公開する。

途中のwriteに失敗した場合は、すでに書いたslotがSSCのthunkのままであることを確認してからoriginalへ戻す。rollbackできなかったthunkがある場合も、対応するoriginalとentry storageはプロセス終了まで保持し、安全なpass-throughを継続する。いずれの場合も状態は`Failed`とし、追加の設置や再hookは行わない。

`Failed`で残留したthunkはoriginalを呼ぶだけとし、mailbox排出や`src`の更新は行わない。

## update時の実行順

各thunkは対応するoriginalを必ず先に呼ぶ。

```text
Skyrimがactive camera stateのUpdateを呼ぶ
  -> SSC thunk
  -> 設置時に取得したoriginal chain
     -> Vanilla / SmoothCam / 先行camera mod
  -> nested callならSSCの後段処理を行わずreturn
  -> mailbox pendingなし、かつsrcの更新要求なしならreturn
  -> mailboxをsrcへ排出
  -> ゲームがpause中でなければsrcを1回Update
```

`Update`内から別camera stateの`Update`が同期的に呼ばれた場合に備え、thread-localのdepth guardを使用する。SSCの後段処理は最外側の呼び出しで1回だけ実行する。

## inactive時の契約

hook設置後は、SexLab P+イベントが発火していない時もcamera updateごとにSSC thunk自体は呼ばれる。inactive時に許可する処理は次だけとする。

- 対応するoriginal chainの呼び出し。
- re-entry depthの更新。
- mailboxに通知があるかの確認。
- `src`が更新を必要としているかの確認。
- 条件不成立時の即時return。

inactive pathでは次を禁止する。

- heap allocation。
- mutex取得。
- ファイルI/O、JSON処理。
- Actor、node、camera poseの取得または計算。
- SmoothCam API呼び出し。
- per-update log。
- taskの追加。

したがってcamera update hookはscene loopの所有者ではなく、Skyrimから呼ばれる実行境界である。loopの継続条件とscene状態は`src`が所有する。

## 他のcamera modとのchain契約

- SSCは設置時点のslot pointerをoriginalとして保存し、必ずそこへchainする。
- SSCのscene camera処理は保存したoriginalが戻った後にだけ実行する。
- SSC設置後に別Modが現在のslotをhookし、取得したSSC thunkへ正しくchainする場合もchain自体は維持される。ただし、そのModがoriginal呼び出し後にcameraを書き換える場合はSSCより後のwriterになるため、最終出力の共存は保証しない。
- SSC設置後に別Modがvtableを無関係なcopyへ差し替える、または既存slotへchainしない場合は対応対象外とする。
- 後からのvtable変更を検出してSSCを外側へ積み直す処理は持たない。

scene終了時のunhookも行わない。SSCが保存したoriginalへslotを戻すと、SSC設置後に追加された別Modのhookを上書きする可能性があり、現在のslotだけから安全なchain所有権を証明できないためである。

## sceneとゲームライフサイクル

| 入力 | hook状態 | 動作 |
| --- | --- | --- |
| DLL load / `kDataLoaded` | `NotInstalled` | scene event sinkだけを登録する |
| プレイヤー不参加scene | `NotInstalled` | hookを設置せず通知を破棄する |
| 最初のプレイヤー参加scene開始 | `NotInstalled` | hookを1回だけ設置して通知を積む |
| 2回目以降のscene開始 | `Installed` | hookへ触れず通知だけを積む |
| `AnimationChange` | `Installed` | hookへ触れず通知だけを積む |
| `AnimationEnding` / `AnimationEnd` | `Installed` | camera制御とscene状態を解放するがhookは保持する |
| preload / load / new game | `Installed` | mailbox世代とscene状態をresetするがhookは保持する |
| camera state変更 | `Installed` | 変更先stateに設置済みのthunkを自然に使用する |
| process終了 | `Installed` / `Failed` | OSによるprocess破棄に任せる |

## 例外と失敗時

- hook設置失敗だけではDLLをunloadしない。
- 設置に失敗したscene eventは`src`へ渡さず、SmoothCamへcamera制御を要求しない。
- SSCの後段処理で例外が発生した場合は例外をゲーム側へ越境させず、camera所有権の緊急解放とscene stateの破棄を行う。
- original chainが送出した例外はSSCが原因として握り潰さない。depth guardだけを復元して再送出する。
- originalがnullのentryは公開してはならない。thunkからoriginalを取得できない状態は設置不変条件の破壊として扱う。
- load中にcamera updateが止まっている場合、resetの実処理は次に安全なcamera updateが到来した時に行う。

## 不変条件

1. 各vtable slotへのSSC writeはプロセス中に最大1回である。
2. 公開済みthunkには常に有効なoriginalが1件対応する。
3. original pointerは設置後に変更しない。
4. hook entry数はuniqueなcamera state vtable数を超えない。
5. scene event、camera event、2回目以降のscene開始はhook tableを書き換えない。
6. SSCのcamera処理はoriginal chain完了後にだけ実行する。
7. mailbox pendingも`src`の更新要求もない時は`src::Update`を呼ばない。
8. camera制御の取得・反映・解放はhook自身ではなく`src`の要求によって行う。

## ログと観測点

通常ログは次の境界だけに出す。

- 初回設置を開始したscene key。
- 収集したstate数とunique vtable数。
- 設置成功。
- verified writeまたはrollbackの失敗。
- thunkの初回到達。これは1回だけ記録する。

camera updateごとのログは出さない。デバッグビルドで計測する場合も、inactive呼び出し数と後段処理実行数はcounterへ集計し、明示的な診断時だけ出力する。

## テスト

### 自動テスト

- プレイヤー不参加sceneではinstallを呼ばない。
- 最初のプレイヤー参加sceneで1回だけinstallする。
- 2回目以降のscene、camera event、load resetで追加writeしない。
- 同じvtableを共有するcamera stateを1件へ重複排除する。
- originalを先に呼び、その後にmailboxと`src::Update`を処理する。
- inactive時はoriginal以外の外部処理を行わない。
- nested Updateでは最外側だけが後段処理を行う。
- pause中は通知を排出するが`src::Update`を呼ばない。
- batch途中の失敗で、書き込み済みslotだけを検証付きでrollbackする。
- install失敗後に再試行しない。

### ゲーム内確認

1. DLL導入後、SexLab P+ sceneを一度も開始しない状態ではhook設置ログと初回到達ログが出ない。
2. 最初のプレイヤー参加sceneで設置ログが1回だけ出て、次のcamera updateで初回到達する。
3. scene中の処理がSmoothCamの更新後に実行され、poseが維持される。
4. scene終了後も通常camera操作に変化がなく、inactive pathでcamera APIを呼ばない。
5. 2回目以降のsceneとcamera state変更で追加の設置ログが出ない。
6. セーブloadとnew game後も重複設置せず、次のplayer sceneを処理できる。
7. 対応対象のcamera mod構成で、SSC設置時に取得したoriginal chainが正しく呼ばれる。

ビルド成功はvtable slot、hook順序、実際の発火thread、SmoothCamとのchain順を保証しない。上記のゲーム内確認を完了条件とする。

設置時にnullだったcamera stateが後から生成される構成は、再hookを行わない本仕様では自動追従しない。標準のSE/AE環境と対応対象のcamera mod構成で、必要なstateが最初の対象scene時点ですべて存在することも実機で確認する。

## 実装反映

`CameraHook`は本仕様の`InstallOnce()`方式へ変更済みである。旧実装から次を廃止した。

- `CameraEventSink`と`QueueRefresh()`。
- `InstallOrRefresh()`の再入経路。
- 64件のchain枠と世代別entryの積み増し。
- `refreshQueued_`、`refreshDisabled_`、camera-event sink登録状態。
- 後から書き換えられたvtableを検出して再patchする処理。

代わりに、初回対象sceneからだけ呼ばれる`InstallOnce()`、`RE::CameraStates::kTotal`以下の固定entry、終端的な`Installed` / `Failed`状態を持つ。ビルド、既存unit test、x64/SKSE export、DLL依存関係の検証は完了している。hook順序とゲーム内ライフサイクルは未確認である。
