#include "Terrain/LunarTerrainGeometry.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
using namespace star;
using namespace star::terrain;
namespace {
int checks=0,failures=0;
void Check(bool ok,const char* name) {++checks;if(!ok){++failures;std::cerr<<"FAIL "<<name<<'\n';}}
}
int main() {
    const auto chart=Chart::At({1,0,0},1737400);
    Patch patch{{0,0,20,20},10,0,{},0};
    const auto flat=[](double,double){return 0.;};
    Check(BuildPatch(chart,patch,flat).complete,"valid synthetic patch");
    for(int kind=0;kind<7;++kind) {
        auto p=patch;
        switch(kind) {
        case 0:p.step=0;break;
        case 1:p.step=std::numeric_limits<double>::quiet_NaN();break;
        case 2:p.rect.x1=std::numeric_limits<double>::max();break;
        case 3:p.rect.x1=21;break;
        case 4:p.edgeStep[0]=std::numeric_limits<double>::quiet_NaN();break;
        case 5:p.edgeStep[0]=-10;break;
        default:p.rect.x0=std::numeric_limits<double>::infinity();break;
        }
        Check(!BuildPatch(chart,p,flat).complete,"invalid grid rejected before conversion");
    }
    bool rejected=false;
    try {rejected=!BuildPatch(chart,patch,{}).complete;}catch(...){ }
    Check(rejected,"empty sampler fails without throwing");
    auto invalid=chart;invalid.radius=0;
    Check(!BuildPatch(invalid,patch,flat).complete,"zero-radius chart rejected");
    double x=123,y=456;
    Check(!invalid.Project({1,0,0},x,y)&&x==123&&y==456,"failed chart projection preserves outputs");
    Check(!chart.Project({std::numeric_limits<double>::quiet_NaN(),0,0},x,y),"NaN direction not silently replaced");
    for(int failAt=28;failAt<=43;++failAt) {
        int calls=0;
        auto sample=[&](double,double){return ++calls==failAt?std::numeric_limits<double>::quiet_NaN():0.;};
        auto mesh=BuildPatch(chart,patch,sample);
        Check(calls>=failAt&&!mesh.complete,"missing normal/morph sample never publishes complete mesh");
    }
    auto a=MakePlan(chart,{1,0,0},100);
    auto b=a;b.chart.radius+=1;
    Check(!SamePlan(a,b),"changed radius invalidates cached plan");
    b=a;b.chart.east=-b.chart.east;
    Check(!SamePlan(a,b),"changed chart basis invalidates cached plan");
    Check(SamePlan(a,a),"identical plan reusable");
    std::atomic_bool cancel{true};Check(!BuildPatch(chart,patch,flat,&cancel).complete,"cancelled work not complete");
    const auto mesh=BuildPatch(chart,patch,flat);
    for(const auto& v:mesh.vertices)Check(v.position.IsFinite()&&v.normal.IsFinite()&&v.tangent.IsFinite(),"valid vertices finite");
    Check(!BuildPatch(Chart::At({0,0,0},1737400),patch,flat).complete,"zero chart origin is not silently replaced");
    Check(MakePlan(chart,{1,0,0},100,std::numeric_limits<double>::quiet_NaN()).patches.empty(),"invalid terrain range has no plan");
    // Exercise generated ring grids, polar fans, seams and coarse-edge morphing
    // with a synthetic sphere. This does not assert measured DEM accuracy.
    for(const Vec3d direction : {Vec3d{1,0,0},Vec3d{0,0,1},Vec3d{-1,0.01,0.1},Vec3d{0.8,0.5,0.34}}) {
        const auto c=Chart::At(direction,1737400);
        for(double altitude : {0.,5000.,200000.}) {
            const auto generated=MakePlan(c,direction,altitude);
            Check(!generated.patches.empty()&&generated.patches.size()<=MaxGroundPatches,"bounded generated plan");
            for(const auto& tile:generated.patches) {
                const auto m=BuildPatch(c,tile,flat);
                Check(m.complete&&!m.vertices.empty()&&!m.indices.empty(),"generated grid remains renderable");
                Check(std::all_of(m.indices.begin(),m.indices.end(),[&](auto i){return i>=0&&static_cast<std::size_t>(i)<m.vertices.size();}),"every triangle index is in bounds");
            }
        }
    }
    std::cout<<checks<<" terrain robustness checks; failures="<<failures<<" (synthetic heights, not measured DEM)\n";return failures?1:0;
}
