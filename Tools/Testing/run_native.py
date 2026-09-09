#!/usr/bin/env python3
"""GCC/Clang native regression runner. All fixtures are local and outputs stay in work/."""
from pathlib import Path
import argparse, concurrent.futures, json, os, re, subprocess, sys
parser=argparse.ArgumentParser(description='Run self-contained C++17 tests without Unreal or downloaded release content.')
parser.add_argument('--sanitize',action='store_true',help='Enable AddressSanitizer and UndefinedBehaviorSanitizer.')
parser.add_argument('--compiler',default=os.environ.get('CXX','g++'))
parser.add_argument('--jobs',type=int,default=2)
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
out=root/'work'/('native-sanitized' if args.sanitize else 'native-tests')
out.mkdir(parents=True,exist_ok=True)
S='Source/Star/'
flight=S+'Simulation/FlightSimulation.cpp'; audio=S+'Presentation/StarAudioRouting.cpp'; nav=S+'Navigation/NavigationPilot.cpp'; obs=S+'Simulation/ObservationGeometry.cpp'
data=json.loads((root/'Tests/Fixtures/navigation-bodies.json').read_text())
lines=[]
for b in data['bodies']:
 if b['id'] not in ('sun','earth','moon','saturn'): continue
 vals=[*b['positionMeters'],b['radiusMeters'],100000 if b['id']=='earth' else 150000 if b['id']=='saturn' else 0, int(b['id']=='moon'),12000 if b['id']=='moon' else 0]
 lines.append(b['id']+' '+' '.join(str(x) for x in vals))
fixture=out/'dated-bodies.txt';fixture.write_text('\n'.join(lines)+'\n')
source=(root/'Source/Star/Runtime/StarPlayerController.cpp').read_text()
methods=[]
for name in ('NavigationBypassed','ResetNavigation','SetFlightPaused','ToggleNavigationSafeBrake','ApplyNavigationControls'):
 m=re.search(r'(?:bool|void) AStarPlayerController::'+name+r'\([^\r\n]*\)(?: const)?\s*\{',source)
 assert m,name
 depth=1;end=m.end()
 while depth:
  if source[end]=='{':depth+=1
  if source[end]=='}':depth-=1
  end+=1
 methods.append(source[m.start():end])
(out/'runtime-methods.inc').write_text('\n'.join(methods))
targets={
 'robustness':([flight,'Tests/Simulation/RobustnessTests.cpp'],[]),
 'raster-policy':(['Tests/Terrain/RasterReadPolicyTests.cpp'],[]),
 'flight':([flight,'Tests/Simulation/FlightSimulationTests.cpp'],[]),
 'routing':([audio,'Tools/Audio/AudioRoutingTests.cpp'],[]),
 'engine':([audio,'Tests/Audio/EngineDynamicsTests.cpp'],[]),
 'listener':([audio,'Tests/Audio/ListenerModeTests.cpp'],[]),
 'dsp':([S+'Presentation/StarShipAudioDSP.cpp','Tests/Audio/ShipAudioDSPTests.cpp'],[]),
 'observation':([flight,obs,'Tests/Observation/ObservationGeometryTests.cpp'],[]),
 'world':([flight,obs,'Tests/Simulation/WorldContinuityTests.cpp'],[str(root/'Tests/Fixtures/ephemeris-world-test.bin'),'1789776000']),
 'navigation':([flight,nav,'Tests/Navigation/NavigationPilotTests.cpp'],[str(fixture)]),
 'controller':([flight,nav,'Tools/Navigation/RuntimePolicyTests.cpp'],[]),
}
def run(item):
 name,(sources,arguments)=item
 cmd=[args.compiler,'-std=c++17','-O1' if args.sanitize else '-O2','-Wall','-Wextra','-Werror','-I'+str(root/'Source/Star'),'-I'+str(root/'Source/Star/Simulation'),'-I'+str(root/'Source/Star/Presentation'),'-I'+str(out),*[str(root/s) for s in sources],'-o',str(out/name)]
 if args.sanitize:cmd[1:1]=['-g','-fsanitize=address,undefined,float-cast-overflow','-fno-omit-frame-pointer','-fno-sanitize-recover=all']
 build=subprocess.run(cmd,capture_output=True,text=True,timeout=180)
 (out/(name+'-build.log')).write_text(build.stdout+build.stderr)
 if build.returncode: return {'name':name,'build':build.returncode,'error':build.stderr[-1500:]}
 test=subprocess.run([str(out/name),*arguments],cwd=out,capture_output=True,text=True,timeout=180)
 (out/(name+'-test.log')).write_text(test.stdout+test.stderr)
 return {'name':name,'build':0,'exit':test.returncode,'output':(test.stdout+test.stderr)[-3500:]}
with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,min(args.jobs,4))) as pool:
 results=list(pool.map(run,targets.items()))
for r in results: print(json.dumps(r))
(out/'results.json').write_text(json.dumps(results,indent=2))

failed=any(r.get("build")!=0 or r.get("exit")!=0 for r in results)
print(f"{len(results)} native test executables; {'FAILED' if failed else 'ALL PASSED'}. Not an Unreal/game/device acceptance test.")
sys.exit(1 if failed else 0)
