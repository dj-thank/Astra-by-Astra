"""Compile current production sampling functions with a minimal stdlib UE math/array shim."""
from pathlib import Path
import json,subprocess
ROOT=Path(__file__).resolve().parents[3];out=ROOT/'work/lunar-panorama/full-region';source=(ROOT/'Source/Star/Runtime/StarDataCatalog.cpp').read_text(encoding='utf-8');near=json.loads((ROOT/'Content/Star/Data/apollo17.json').read_text());far=json.loads((ROOT/'Content/Star/Data/apollo17_far.json').read_text())
def body(name):
 start=source.index(name);start=source.rfind('\n',0,start)+1;pos=source.index('{',start);depth=1;end=pos+1
 while depth:
  if source[end]=='{':depth+=1
  if source[end]=='}':depth-=1
  end+=1
 return source[start:end]
code=r'''
#include <vector>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <cassert>
using int16=int16_t;using int32=int32_t;using uint8=uint8_t;
template<class T> struct TArray:std::vector<T>{bool IsEmpty()const{return this->empty();}};
struct FMath{template<class T>static T Clamp(T x,T a,T b){return std::max(a,std::min(x,b));}template<class T>static T Min(T a,T b){return std::min(a,b);}template<class T>static T Max(T a,T b){return std::max(a,b);}static double Fmod(double a,double b){return std::fmod(a,b);}static int32 FloorToInt(double x){return (int32)std::floor(x);}template<class T>static bool IsFinite(T x){return std::isfinite(x);}static double Lerp(double a,double b,double f){return a+(b-a)*f;}static double DegreesToRadians(double x){return x*3.14159265358979323846/180;}static double Cos(double x){return std::cos(x);}static double Sqrt(double x){return std::sqrt(x);}};
double Smooth01(double x){x=FMath::Clamp(x,0.,1.);return x*x*(3-2*x);}
struct FStarDataCatalog{
TArray<int16> GlobalHeights,FarHeights;TArray<float> RegionalHeights;TArray<uint8> RegionalValid,FarConfidence;
int32 GlobalWidth=5760,GlobalHeight=2880,RegionalWidth=2800,RegionalHeightCount=2400,FarWidth=4993,FarHeightCount=5800;
double RegionWest,RegionEast,RegionNorth,RegionSouth,FarWest,FarEast,FarNorth,FarSouth,MaximumHeight=12000,FarHeightScale=.25,FarBlendWidth=500,FarBlendInset=20;
double GlobeHeight(double,double)const;bool RegionalHeight(double,double,double&,double&)const;bool FarRegionalHeight(double,double,double&,double&)const;double MoonHeight(double,double,bool* = nullptr)const;
double OldMoonHeight(double a,double b)const{double g=GlobeHeight(a,b),h=g,blend=0;return RegionalHeight(a,b,h,blend)?FMath::Lerp(g,h,blend):g;}
};
template<class T>void load(TArray<T>& a,const char* p){std::ifstream f(p,std::ios::binary|std::ios::ate);assert(f);auto n=f.tellg();a.resize((size_t)n/sizeof(T));f.seekg(0);f.read(reinterpret_cast<char*>(a.data()),n);assert(f);}
'''
code+='\n'.join(body('FStarDataCatalog::'+n) for n in ['GlobeHeight','RegionalHeight','FarRegionalHeight','MoonHeight'])
code+='\nint main(){FStarDataCatalog c;\n'
for prefix,m in [('Region',near),('Far',far)]:
 for prop,key in [('West','westLongitudeDegrees'),('East','eastLongitudeDegrees'),('North','northLatitudeDegrees'),('South','southLatitudeDegrees')]:code+=f'c.{prefix}{prop}={m[key]:.17g};\n'
for name,path in [('GlobalHeights','moon_ldem_16_i16.bin'),('RegionalHeights','apollo17_height_f32.bin'),('RegionalValid','apollo17_valid_u8.bin'),('FarHeights','apollo17_far_height_i16.bin'),('FarConfidence','apollo17_far_confidence_u8.bin')]:code+=f'load(c.{name},"Content/Star/Data/{path}");\n'
code+=r'''
int preserved=0,farExact=0;double maxNear=0,maxBoundary=0,sumBand=0,maxDtm=0,sumDtm=0,maxGradientJump=0;int bandCount=0,interiorCount=0,dtmCount=0;
for(int y=0;y<=100;y++)for(int x=0;x<=100;x++){double lon=c.RegionWest+(c.RegionEast-c.RegionWest)*x/100.;double lat=c.RegionSouth+(c.RegionNorth-c.RegionSouth)*y/100.;double old=c.OldMoonHeight(lat,lon),now=c.MoonHeight(lat,lon);maxNear=std::max(maxNear,std::abs(old-now));double ep=std::min(std::min(x/100.*2800-.5,2799-(x/100.*2800-.5)),std::min(y/100.*2400-.5,2399-(y/100.*2400-.5)));if(ep>=60){assert(old==now);interiorCount++;}else{sumBand+=std::abs(old-now);bandCount++;double raw,blend;if(c.RegionalHeight(lat,lon,raw,blend)){maxDtm=std::max(maxDtm,std::abs(raw-now));sumDtm+=std::abs(raw-now);dtmCount++;}}preserved++;}
for(int y=100;y<c.FarHeightCount-100;y+=113)for(int x=100;x<c.FarWidth-100;x+=107){int i=y*c.FarWidth+x;if(c.FarConfidence[i]!=255||c.FarConfidence[i+1]!=255||c.FarConfidence[i+c.FarWidth]!=255||c.FarConfidence[i+c.FarWidth+1]!=255)continue;double lon=c.FarWest+(x+.5)/c.FarWidth*(c.FarEast-c.FarWest),lat=c.FarNorth-(y+.5)/c.FarHeightCount*(c.FarNorth-c.FarSouth);if(lon>c.RegionWest-.03&&lon<c.RegionEast+.03&&lat>c.RegionSouth-.03&&lat<c.RegionNorth+.03)continue;double expected=c.FarHeights[i]*.25,actual=c.MoonHeight(lat,lon);assert(std::abs(expected-actual)<1e-5);farExact++;}
for(int j=1;j<100;j++){double lat=c.RegionSouth+(c.RegionNorth-c.RegionSouth)*j/100.;double a=c.MoonHeight(lat,c.RegionWest-1e-9),b=c.MoonHeight(lat,c.RegionWest+1e-9);maxBoundary=std::max(maxBoundary,std::abs(a-b));assert(std::abs(a-b)<.001);}
std::ofstream horizon("work/lunar-panorama/full-region/runtime_horizon.csv");horizon<<"azimuth,elevation,distance,height\n";double r0=1737400.,la0=FMath::DegreesToRadians(20.1908),lo0=FMath::DegreesToRadians(30.7717),ground=c.MoonHeight(20.1908,30.7717);
for(int az=0;az<360;az+=5){double a=FMath::DegreesToRadians(az),best=-90,bestD=0,bestH=0;for(double d=500;d<30000;d+=25){double lat=la0+d*std::cos(a)/r0,lon=lo0+d*std::sin(a)/(r0*std::cos(la0));double he=c.MoonHeight(lat*180/3.14159265358979323846,lon*180/3.14159265358979323846),rr=r0+he,dl=lon-lo0,e=rr*std::cos(lat)*std::sin(dl),n=rr*(std::sin(lat)*std::cos(la0)-std::cos(lat)*std::sin(la0)*std::cos(dl)),u=rr*(std::sin(lat)*std::sin(la0)+std::cos(lat)*std::cos(la0)*std::cos(dl))-(r0+ground)-1.6,el=std::atan2(u,std::hypot(e,n))*180/3.14159265358979323846;if(el>best){best=el;bestD=d;bestH=he;}}horizon<<std::setprecision(12)<<az<<","<<best<<","<<bestD<<","<<bestH<<"\n";}
for(int j=1;j<100;j++){double lat=c.RegionSouth+(c.RegionNorth-c.RegionSouth)*j/100.,step=.05/(1737400.*std::cos(FMath::DegreesToRadians(20.)))*180/3.14159265358979323846;double outside=(c.MoonHeight(lat,c.RegionWest-.8*step)-c.MoonHeight(lat,c.RegionWest-1.2*step))/.02,inside=(c.MoonHeight(lat,c.RegionWest+1.2*step)-c.MoonHeight(lat,c.RegionWest+.8*step))/.02;maxGradientJump=std::max(maxGradientJump,std::abs(outside-inside));}assert(maxGradientJump<.01);std::ofstream transition("work/lunar-panorama/full-region/transition_cpu.json");transition<<std::setprecision(15)<<"{\"interiorSamplesUnchanged\":"<<interiorCount<<",\"edgeSamples\":"<<bandCount<<",\"maxOldToNewEdgeChangeMeters\":"<<maxNear<<",\"meanOldToNewEdgeChangeMeters\":"<<sumBand/bandCount<<",\"maxNewEdgeVsNative5mMeters\":"<<maxDtm<<",\"meanNewEdgeVsNative5mMeters\":"<<sumDtm/dtmCount<<",\"maxBoundaryGradientJump\":"<<maxGradientJump<<"}";assert(c.MoonHeight(0,0)==c.GlobeHeight(0,0));assert(farExact>100);assert(c.FarHeights.size()*2+c.FarConfidence.size()<145*1024*1024);
std::cout<<std::setprecision(15)<<"{\"status\":\"PRODUCTION_SAMPLING_FUNCTIONS_CPU_PASS\",\"nearPointsCompared\":"<<preserved<<",\"maxOldToNewHeightChangeMeters\":"<<maxNear<<",\"exactFarGridSamples\":"<<farExact<<",\"nearBoundaryPairMaxDifferenceMeters\":"<<maxBoundary<<"}"<<std::endl;
}
'''
(out/'catalog_harness.cpp').write_text(code,encoding='utf-8',newline='\n')
vc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat'
batch=f'@echo off\ncall "{vc}" -arch=x64 >nul\ncl /nologo /EHsc /O2 /std:c++17 /Fe:work\\lunar-panorama\\full-region\\catalog_harness.exe /Fo:work\\lunar-panorama\\full-region\\catalog_harness.obj work\\lunar-panorama\\full-region\\catalog_harness.cpp >work\\lunar-panorama\\full-region\\compile.log 2>&1\nif errorlevel 1 exit /b 1\nwork\\lunar-panorama\\full-region\\catalog_harness.exe >work\\lunar-panorama\\full-region\\catalog_cpu.json\n'
(out/'compile.cmd').write_text(batch,encoding='utf-8',newline='\r\n');result=subprocess.run(['cmd','/c',str(out/'compile.cmd')],cwd=ROOT);print((out/'compile.log').read_text(errors='replace')[:2500]);print((out/'catalog_cpu.json').read_text() if (out/'catalog_cpu.json').exists() else 'NO_RUN');raise SystemExit(result.returncode)
