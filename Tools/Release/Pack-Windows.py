"""Package a tested game with portable launchers, notices and per-file hashes."""
from pathlib import Path
import argparse,hashlib,json,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--engine-root',type=Path,required=True);args=parser.parse_args()
stage=ROOT/'outputs/Star-Win64';out=ROOT/'outputs/STAR-Windows.zip'
assert (stage/'Windows/Star/Binaries/Win64/Star.exe').is_file()
assert not out.exists(),'Preserve previous release ZIPs'
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b)
 return h.hexdigest()
files=[]
for p in sorted((stage/'Windows').rglob('*')):
 if not p.is_file():continue
 rel=p.relative_to(stage);parts={s.lower() for s in rel.parts}
 if parts & {'saved','userdata','gpudumpviewer'}:continue
 if p.suffix.lower() in {'.pdb','.target','.log'} or p.name.startswith('Manifest_') or p.name=='StagedBuild_Star.ini':continue
 assert not any(s in p.name.lower() for s in ['unrealeditor','unrealbuildtool','shadercompileworker'])
 files.append((p,rel.as_posix()))
for p in sorted((args.engine_root/'Engine/Source/ThirdParty/Licenses').glob('*')):
 if p.is_file():files.append((p,'licenses/engine/'+p.name))
files += [(ROOT/'LICENSE','LICENSE'),(ROOT/'THIRD_PARTY_NOTICES.md','THIRD_PARTY_NOTICES.md'),(ROOT/'docs/CREDITS.md','CREDITS.md'),(ROOT/'docs/PLAYING.md','PLAYING.md'),(ROOT/'docs/NETWORK.md','NETWORK.md'),(ROOT/'Plugins/StarFlightInput/ThirdParty/SDL3/LICENSE.txt','licenses/SDL3.txt'),(ROOT/'Content/Star/UI/Fonts/OFL.txt','licenses/Noto-OFL.txt')]
base='''@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"
set __COMPAT_LAYER=HIGHDPIAWARE
if not exist "%~dp0Windows\\Star.exe" (
 echo ゲームのファイルが足りません。ZIPをすべて展開してください。
 pause
 exit /b 1
)
{check}start "" "%~dp0Windows\\Star.exe" -UserDir="%~dp0UserData" -NoVSync -ExecCmds="t.MaxFPS 0" {flags}
'''
vr_check='''reg query "HKLM\\SOFTWARE\\Khronos\\OpenXR\\1" /v ActiveRuntime >nul 2>&1
if errorlevel 1 (
 echo PC側のOpenXRランタイムを設定し、VRヘッドセットを接続してください。
 pause
 exit /b 2
)
'''
generated={
 'Play-STAR.cmd':base.format(check='',flags='').replace('\n','\r\n').encode('utf-8'),
 'Play-STAR-VR.cmd':base.format(check=vr_check,flags='-vr -StarVR').replace('\n','\r\n').encode('utf-8'),
 'README.md':'''# STAR — Windows版

ZIPをすべて展開し、`Play-STAR.cmd` を開いてください。EXEだけを移動せず、フォルダー全体を保管してください。

Windows 11 x64 / DirectX 12対応GPU向けの実験的なプレリリースです。操作はPLAYING.md、素材はCREDITS.md、利用条件はTHIRD_PARTY_NOTICES.mdをご覧ください。

セーブ・設定・写真はこのフォルダーのUserDataへ保存します。更新版へ引き継ぐときは、ゲームを終了してからUserDataをコピーしてください。

VRはPCにOpenXRランタイムを設定し、ヘッドセットを接続してPlay-STAR-VR.cmdから起動します。実機の両眼表示・追従・操作・性能は未検証です。

この実行版は未署名です。Windowsの保護機能によって実行が拒否される環境があります。保護機能を無効にする手順は案内していません。改造用ソースとビルド手順を公開しています。

ソース・報告・更新: https://github.com/dj-thank/STAR
'''.encode('utf-8')}
manifest={'version':'0.4.0-preview.1','sourceCommit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'buildConfiguration':'Development preview','headsetVerified':False,'signed':False,'files':[{'path':name,'bytes':p.stat().st_size,'sha256':sha(p)} for p,name in files]+[{'path':name,'bytes':len(body),'sha256':hashlib.sha256(body).hexdigest()} for name,body in generated.items()]}
generated['release-info.json']=(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n').encode('utf-8')
with zipfile.ZipFile(out,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for p,name in files:z.write(p,name)
 for name,body in generated.items():z.writestr(name,body)
with zipfile.ZipFile(out) as z:assert z.testzip() is None
assert out.stat().st_size<2_000_000_000,'Release asset exceeds upload budget'
report={'archive':str(out),'sha256':sha(out),'bytes':out.stat().st_size,'files':len(files)+len(generated),'sourceCommit':manifest['sourceCommit']}
(ROOT/'work/windows-package.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report))
