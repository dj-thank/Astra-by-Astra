"""Summarize and bundle existing local diagnostics; never upload or alter saves."""
from pathlib import Path
from collections import Counter
from datetime import datetime,timezone
import argparse,json,zipfile
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser()
p.add_argument('--user-dir',type=Path,default=ROOT/'outputs/STAR-user-data')
p.add_argument('--session',type=Path)
p.add_argument('--output',type=Path)
a=p.parse_args()
saved=a.user_dir.resolve()/'Saved'
sessions=sorted((saved/'Diagnostics').glob('*'),key=lambda x:x.stat().st_mtime)
session=a.session.resolve() if a.session else sessions[-1] if sessions else None
if session is None:raise SystemExit('No diagnostic session was found.')
files=sorted(session.glob('flight-*.jsonl'))
if not files:raise SystemExit('No flight recorder files were found.')
rows=[];invalid=0
for file in files:
    if file.stat().st_size>9*1024*1024:raise SystemExit('Unexpected diagnostic file size.')
    with file.open(encoding='utf-8') as stream:
        for line in stream:
            try:rows.append(json.loads(line))
            except json.JSONDecodeError:invalid+=1
rows.sort(key=lambda x:x['sequence'])
if not rows:raise SystemExit('No complete diagnostic records were found.')
flights=[r for r in rows if r['type']=='flight']
events=Counter(r['type'] for r in rows)
pending=Counter()
for r in rows:
    if r['type']=='operation_begin':pending[r['detail']]+=1
    if r['type']=='operation_end':pending[r['detail']]=max(0,pending[r['detail']]-1)
clean=rows[-1]['type']=='session_end'
summary={'state':'CLEAN_EXIT_RECORDED' if clean else 'NO_CLEAN_EXIT_RECORD',
    'interpretation':'正常終了を記録' if clean else '正常終了記録なし。実行中・強制終了・クラッシュの区別は追加確認が必要です。',
    'records':len(rows),'incompleteLines':invalid,'firstUtc':rows[0]['utc'],'lastUtc':rows[-1]['utc'],
    'lastRecord':rows[-1],'lastFlight':flights[-1] if flights else None,
    'maximumSpeedMps':max((r['speedMps'] for r in flights),default=0),'eventCounts':dict(events),
    'pauseEvents':[r for r in rows if r['type'] in ('pause_cause','pause_state')][-20:],
    'longestOperations':sorted((r for r in rows if 'milliseconds' in r),key=lambda r:r['milliseconds'],reverse=True)[:20],
    'unfinishedOperations':{k:v for k,v in pending.items() if v},'uploaded':False}
metadata=session/'session.json'
if metadata.exists():summary['session']=json.loads(metadata.read_text(encoding='utf-8'))
out=(a.output or ROOT/'outputs/diagnostics'/datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%f')).resolve()
if not out.is_relative_to((ROOT/'outputs').resolve()):raise SystemExit('Output must stay in this project outputs directory.')
out.mkdir(parents=True,exist_ok=False)
(out/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
report=f"# 不具合調査ログ\n\n{summary['interpretation']}\n\n- 記録: {len(rows)}件\n- 最終記録: {rows[-1]['utc']}\n- 観測した最高速度: {summary['maximumSpeedMps']/1000:.2f} km/s\n- 最終状態・停止理由・処理時間: summary.json\n\n正常終了記録がないことだけではクラッシュの原因は断定できません。保存データの変更・外部送信はしていません。\n"
(out/'README.md').write_text(report,encoding='utf-8')
with zipfile.ZipFile(out/'diagnostics.zip','x',zipfile.ZIP_DEFLATED) as archive:
    for file in files:archive.write(file,'flight/'+file.name)
    if metadata.exists():archive.write(metadata,'flight/session.json')
    log=saved/'Logs/Star.log'
    if log.exists() and log.stat().st_size<32*1024*1024:archive.write(log,'engine/Star.log')
    archive.write(out/'summary.json','summary.json');archive.write(out/'README.md','README.md')
print(json.dumps({'output':str(out),'state':summary['state'],'records':len(rows),'uploaded':False},ensure_ascii=False))
