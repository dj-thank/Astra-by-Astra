# ソースからビルドする

## 必要なもの

- Windows 11 x64
- Unreal Engine **5.8.2**（利用者自身で正規に導入）
- Visual Studio C++ Build ToolsとWindows SDK。エンジンが要求する.NET SDKも必要です。
- PowerShell 7、Python 3、Git
- ソース・展開済み素材・ビルド・キャッシュ用の空き容量

エンジン本体・Editor・SDKはこのリポジトリに含めません。Gitの大きなファイルを避けるため、素材はReleaseに分離しています。

## 準備

```powershell
git clone https://github.com/dj-thank/STAR.git
cd STAR
python -m pip install -r requirements.txt
$env:STAR_UE_ROOT = 'C:/Program Files/Epic Games/UE_5.8'
pwsh -File Tools/Setup-Content.ps1
pwsh -File Tools/Input/Setup-SDL3.ps1
```

`Setup-Content.ps1` はバージョン固定の公開素材ZIP（ゲーム素材と編集用原素材の2つ）をダウンロードし、SHA-256を確認してから `Content` と `Art` を展開します。既存ファイルへ異なる内容を上書きしません。既に編集した素材がある場合は新しいcloneで試してください。

## Editorとゲーム

```powershell
pwsh -File Tools/Unreal/Build-Star.ps1 -Target Editor -NoUba
pwsh -File Tools/Unreal/Prepare-Content.ps1
pwsh -File Tools/Unreal/Build-Star.ps1 -Target Package -NoUba
```

ゲームは `outputs/Star-Win64/Windows/Star.exe` に出力されます。`Star.uproject` を開いて編集することもできます。

`Prepare-Content.ps1` は音源をインポートし、手元のエンジンからVR表示用マテリアルを生成します。専用のEditorプロセスを使います。同じプロジェクトを別のEditorで開いたまま実行しないでください。

## 音を作り直す

```powershell
pwsh -File Tools/Audio/Build-Sounds.ps1
pwsh -File Tools/Unreal/Prepare-Content.ps1
```

公開版の音は元の録音や外部サービスを必要とせず、C++とPythonで再生成できます。音源ファイルもMITライセンスです。

## テスト

```powershell
pwsh -File Tools/Simulation/Test-Native.ps1
pwsh -File Tools/Simulation/Test-Astronomy.ps1
pwsh -File Tools/Audio/Test-Routing.ps1
```

CIはエンジンを使わない中核のC++テストを実行します。CIの成功だけでは、描画・実機入力・VRが動作したことにはなりません。フルゲームはWindowsの製品用EXEで確認してください。

## 素材・地形を変更する

`Art` に機体モデル、`Content/Star` にゲーム用素材、`Data` に観測データと加工の記録があります。`Tools/Art` と `Tools/Lookdev` の個別生成スクリプトは素材開発用で、追加のPythonライブラリや原データが必要なものがあります。通常のビルドで全スクリプトを実行する必要はありません。
