# STAR Flight Input

UE 5.8 / Win64 用のプロジェクト内フライトスティック入力。SDL 3.4.16 のジョイスティック API を `IInputDevice` に接続し、Enhanced Input から使える固有の `FKey` を送ります。SDL の video/audio/gamepad サブシステムは初期化しません。

## セットアップと検証

プロジェクトのルートで実行します。

```powershell
pwsh -File Tools/Input/Setup-SDL3.ps1
pwsh -File Tools/Input/Build-InputTools.ps1 -RunTests
& work/input-build/StarJoystickProbe.exe --seconds 5
```

セットアップは公式 VC 開発バンドルを `work/input-downloads` に取得し、固定 SHA-256 を照合します。ヘッダーとライセンスは Git 管理対象、Win64 の `.lib` / `.dll` は無視対象で、セットアップにより各 checkout で再取得できます。`ThirdParty/SDL3/provenance.json` に出所とダイジェストを保持します。UE のビルド時には DLL を実行ファイルの配置先へステージします。

ポータブル検証は同じ入力コアと SDL バックエンドを MSVC でコンパイルします。仮想 SDL ジョイスティックを使った結果は実機の操縦、UE コンパイル、パッケージ起動の合格には該当しません。

## ゲーム側の API

ゲームモジュールの Build.cs に `StarFlightInput` 依存を追加します。ゲームスレッドから利用してください。

```cpp
#include "StarFlightInputModule.h"
#include "StarFlightKeys.h"

if (auto* Input = FStarFlightInputModule::GetIfAvailable())
{
    const FStarFlightInputStatus Status = Input->GetStatus();
    const FStarFlightControlFrame Frame = Input->GetControlFrame();
    // Status.bRequiresPause が true ならゲームを一時停止する。
    // Frame と FKeys は同じ入力なので、同じ操作を両方から適用しない。
}
```

- `GetStatus()` : `bInitialized`, `bConnected`, `bAxisDataReady`, `bFocused`, `bArmed`, `bRequiresPause`, `bProfileValid`, `bMappingConfirmed`, `ConnectionGeneration`, 日本語 `Message`。デバイスの `DeviceName`, `ReportedName`, `GUID`, `VendorId`, `ProductId`, `NumAxes`, `NumButtons`, `NumHats` も含みます。既知の VID/PID は表示名を使い、SDL が返した元の名前を `ReportedName` に保持します。
- `GetControlFrame()` : `Yaw/Pitch/Roll` は -1～1、`Throttle` は 0～1、`LookX/LookY` は -1/0/1、`Buttons[16]` は割り当て後の押下状態。停止条件では全成分がゼロです。
- `GetRawState()` : SDL 番号の `Axes`, `Buttons`, `Hats` と個数、`Connected`, `AxisDataReady`。校正 UI 用であり、操縦には直接適用しません。
- `GetDevices()` : 更新済みのデバイス一覧。除外対象も名前・VID/PID・種類を示します。除外したホイール/ゲームパッドは開かないため軸などの個数は 0 です。
- `GetProfile() / SetProfile(Profile, bPersist=true)` : 校正・曲線・反転・割り当ての取得と変更。保存時は接続中の機器が必要です。無効な値や機器にない番号を拒否し、以前の設定を維持します。
- `GetDeviceSelection() / SetDeviceSelection(Selection, bPersist=true)` : VID/PID/GUID で選択。VID 必須、PID の 0 と空 GUID は該当項目を絞り込みません。
- `SetGameplayEnabled(false)` : メニュー・一時停止時に呼びます。`true` はユーザーが再開したときに呼びます。毎フレームの無条件 `true` 呼び出しで一時停止要求を解除しないでください。

`bRequiresPause` は接続していた機器の切断またはフォーカス喪失で保持されます。明示的な再開でも、中立・低スロットル・全ボタン解放・POV 中央の保持を確認するまで再武装しません。判定には応答曲線を適用する前の校正値を使い、大きなデッドゾーンによる中立判定の誤通過を防ぎます。初回から機器が存在しない場合はキーボード/ゲームパッド操作を妨げる一時停止要求を出しません。切断後にキーボード操作で再開することは可能です。

## キーと初期割り当て

| `FStarFlightKeys` | FKey 名 | 値 / 初期物理番号 |
|---|---|---|
| Yaw / Pitch / Roll / Throttle | `StarFlight_Yaw` / `StarFlight_Pitch` / `StarFlight_Roll` / `StarFlight_Throttle` | SDL 軸 0 / 1 / 2 / 3 |
| LookX / LookY | `StarFlight_LookX` / `StarFlight_LookY` | SDL POV 0、上が LookY +1 |
| Scan | `StarFlight_Scan` | ボタン 0 |
| ToggleView | `StarFlight_ToggleView` | ボタン 1 |
| Brake | `StarFlight_Brake` | ボタン 2 |
| ToggleGear | `StarFlight_ToggleGear` | ボタン 3 |
| ToggleCruise | `StarFlight_ToggleCruise` | ボタン 4 |
| TargetNext | `StarFlight_TargetNext` | ボタン 5 |
| TogglePhoto | `StarFlight_TogglePhoto` | ボタン 6 |
| Pause | `StarFlight_Pause` | ボタン 7 |
| RecenterLook | `StarFlight_RecenterLook` | ボタン 8 |
| Precision | `StarFlight_Precision` | ボタン 9 |
| Aux11～Aux16 | `StarFlight_Aux11`～`StarFlight_Aux16` | ボタン 10～15 |

意図する操作はスティック左右＝ヨー、前後＝ピッチ、ねじり＝ロール、スライダー＝スロットル、POV＝視点、トリガー＝スキャンです。SDL 軸番号・スロットル反転の初期値は暫定値です。実機を各方向に動かして確定するまで `MappingConfirmed=false` として表示し、T.16000M の軸順序が実証済みとは扱いません。

```cpp
auto Profile = Input->GetProfile();
Profile.Axes[0].Index = 0;       // yaw に割り当てる、確認済みの SDL 軸番号
Profile.Axes[0].Minimum = -32768;
Profile.Axes[0].Center = 0;
Profile.Axes[0].Maximum = 32767;
Profile.Axes[0].Deadzone = 0.06f;
Profile.Axes[0].Exponent = 1.5f;
Profile.Axes[0].Inverted = false;
Profile.ButtonMap[0] = 0;       // Scan の物理ボタン。-1 は無効
Profile.HatIndex = 0;           // -1 は無効
// 確認が済んだ時点でのみ Profile.MappingConfirmed = true;
const bool bSaved = Input->SetProfile(Profile);
```

保存先は `Saved/StarFlightInput/selection.starinput` と `Saved/StarFlightInput/Profiles/<SDL GUID>.starinput`。UTF-8、バージョン付き、読み込みは全項目を検査してから反映し、書き込みは一時ファイルから置換します。読み込めないプロファイルは入力を有効にせず、元ファイルを保持します。同型機 2 台が同じ SDL GUID を持つ場合、シリアルなしでは永続的に識別できないため自動選択を停止します。

## Windows のデバイス経路

プロセス内で `SDL_JOYSTICK_WGI=1` と `SDL_JOYSTICK_DIRECTINPUT=0` を設定し、SDL の Windows.Gaming.Input raw controller 経路を使います。SDL の stock DirectInput OpenJoystick は排他的取得と force feedback 初期化を行うため、この経路は使いません。G29、ホイール型、通常のゲームパッドは `SDL_OpenJoystick` しません。初期選択は Thrustmaster VID `044F`、4 軸以上・16 ボタン以上・POV 1 個以上のフライトスティックです。ゲームパッドは UE の通常入力に任せます。

WGI の発見は非同期で、非アクティブなプロセスでは実入力値を取得できない場合があります。ゲーム画面が前面であることと `bAxisDataReady` を別々に確認します。校正/実機受入手順は `Tools/Input/HARDWARE-ACCEPTANCE.md` を参照してください。

一次資料: [SDL 3.4.16 公式リリース](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16)、[SDL joystick API](https://wiki.libsdl.org/SDL3/CategoryJoystick)、[SDL WGI hint](https://wiki.libsdl.org/SDL3/SDL_HINT_JOYSTICK_WGI)、[SDL DirectInput 実装](https://github.com/libsdl-org/SDL/blob/release-3.4.16/src/joystick/windows/SDL_dinputjoystick.c)、[UE 5.8 IInputDevice](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/InputDevice/IInputDevice)。
