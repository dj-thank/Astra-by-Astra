#include "Terrain/LunarTerrainGeometry.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace star;
using namespace star::terrain;
namespace {
std::size_t Checks = 0;
void Require(bool value, const char* name) { ++Checks; if (!value) throw std::runtime_error(name); }
double Clamp(double x,double lo,double hi) { return std::max(lo,std::min(hi,x)); }
double Lerp(double a,double b,double t) { return a+(b-a)*t; }
Vec3d Radial(double lat,double lon) { lat *= Pi/180; lon *= Pi/180; return {std::cos(lat)*std::cos(lon),std::cos(lat)*std::sin(lon),std::sin(lat)}; }
template<class T> std::vector<T> Read(const std::filesystem::path& path,std::size_t count)
{
    Require(std::filesystem::file_size(path)==count*sizeof(T),"real DEM byte count");
    std::vector<T> values(count); std::ifstream input(path,std::ios::binary);
    input.read(reinterpret_cast<char*>(values.data()),static_cast<std::streamsize>(count*sizeof(T)));
    Require(input.good(),"real DEM read"); return values;
}
struct RealData {
    int width=0,height=0,rw=0,rh=0;
    double scale=0,radius=0,west=0,east=0,north=0,south=0;
    std::vector<std::int16_t> globe;
    std::vector<float> region;
    std::vector<unsigned char> mask;
    int fw=0,fh=0;
    double fwest=0,feast=0,fnorth=0,fsouth=0,fscale=0,fwidth=0,finset=0;
    std::vector<std::int16_t> far;
    std::vector<unsigned char> confidence;
    RealData(const std::filesystem::path& directory,const std::filesystem::path& meta)
    {
        std::ifstream input(meta); input>>width>>height>>scale>>radius>>rw>>rh>>west>>east>>north>>south;
        Require(width>0&&height>0&&rw>0&&rh>0&&!input.fail(),"metadata parsed");
        globe=Read<std::int16_t>(directory/"moon_ldem_16_i16.bin",static_cast<std::size_t>(width)*height);
        region=Read<float>(directory/"apollo17_height_f32.bin",static_cast<std::size_t>(rw)*rh);
        mask=Read<unsigned char>(directory/"apollo17_valid_u8.bin",static_cast<std::size_t>(rw)*rh);
        if(input>>fw>>fh>>fwest>>feast>>fnorth>>fsouth>>fscale>>fwidth>>finset)
        {
            Require(fw>1&&fh>1&&fwidth>finset,"optional far10m metadata");
            far=Read<std::int16_t>(directory/"apollo17_far_height_i16.bin",static_cast<std::size_t>(fw)*fh);
            confidence=Read<unsigned char>(directory/"apollo17_far_confidence_u8.bin",static_cast<std::size_t>(fw)*fh);
            std::cout<<"Using real 10m far DTM "<<fw<<"x"<<fh<<" (read-only)\n";
        }
    }
    double Global(double lat,double lon) const
    {
        double wrap=std::fmod(lon+180,360); if(wrap<0) wrap+=360;
        const double x=wrap/360*width-0.5,y=Clamp((90-lat)/180*height-0.5,0,height-1.0);
        const int ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));
        const auto at=[&](int a,int b){ return globe[static_cast<std::size_t>(std::max(0,std::min(height-1,b))*width+(a%width+width)%width)]*scale; };
        return Lerp(Lerp(at(ix,iy),at(ix+1,iy),x-ix),Lerp(at(ix,iy+1),at(ix+1,iy+1),x-ix),y-iy);
    }
    double Height(double lat,double lon) const
    {
        const double global=FarBase(lat,lon,Global(lat,lon));
        if(lon<=west||lon>=east||lat<=south||lat>=north) return global;
        const double x=(lon-west)/(east-west)*rw-0.5,y=(north-lat)/(north-south)*rh-0.5;
        const int ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));
        if(ix<0||iy<0||ix+1>=rw||iy+1>=rh) return global;
        const int ids[4]={iy*rw+ix,iy*rw+ix+1,(iy+1)*rw+ix,(iy+1)*rw+ix+1};
        for(int id:ids) if(!mask[static_cast<std::size_t>(id)]||!std::isfinite(region[static_cast<std::size_t>(id)])) return global;
        const double h=Lerp(Lerp(region[static_cast<std::size_t>(ids[0])],region[static_cast<std::size_t>(ids[1])],x-ix),Lerp(region[static_cast<std::size_t>(ids[2])],region[static_cast<std::size_t>(ids[3])],x-ix),y-iy);
        const double edge=std::min({x,rw-1-x,y,rh-1-y}),t=Clamp(edge/60,0,1);
        return Lerp(global,h,t*t*(3-2*t));
    }
    double FarBase(double lat,double lon,double global) const
    {
        if(far.empty()||lon<=fwest||lon>=feast||lat<=fsouth||lat>=fnorth) return global;
        const double x=(lon-fwest)/(feast-fwest)*fw-.5,y=(fnorth-lat)/(fnorth-fsouth)*fh-.5;
        const int ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));
        if(ix<0||iy<0||ix+1>=fw||iy+1>=fh) return global;
        const int ids[4]={iy*fw+ix,iy*fw+ix+1,(iy+1)*fw+ix,(iy+1)*fw+ix+1};
        for(const int i:ids) if(!confidence[static_cast<std::size_t>(i)]) return global;
        const auto at=[&](const auto& values) {
            return Lerp(Lerp(values[static_cast<std::size_t>(ids[0])],values[static_cast<std::size_t>(ids[1])],x-ix),
                        Lerp(values[static_cast<std::size_t>(ids[2])],values[static_cast<std::size_t>(ids[3])],x-ix),y-iy);
        };
        const double t=Clamp((at(confidence)/255*fwidth-finset)/(fwidth-finset),0,1);
        return Lerp(global,at(far)*fscale,t*t*(3-2*t));
    }
};
// UE 5.8 KismetProceduralMeshLibrary::GenerateBoxMesh top face:
// vertices (-1,+1,+1), (+1,+1,+1), (+1,-1,+1), (-1,-1,+1),
// first triangle (0,1,3), normal +Z. This engine fixture has NEGATIVE
// cross(edge1,edge2).normal. A mathematical outward cross is back-facing in UE.
double UnrealFrontFaceReference()
{
    const Vec3d a{-1,1,1}, b{1,1,1}, c{-1,-1,1}, normal{0,0,1};
    const double sign=Vec3d::Dot(Vec3d::Cross(b-a,c-a),normal);
    Require(sign<0,"UE standard top face winding fixture");
    return sign;
}
void PlanChecks()
{
    std::size_t maximumPatches=0;
    for(const auto coordinate: {UV{0,0},UV{20.1908,30.7717},UV{0,179.999},UV{0,-179.999},UV{90,0},UV{-90,0},UV{89.999,120}})
    {
        const auto chart=Chart::At(Radial(coordinate.u,coordinate.v),1737400);
        for(double altitude: {0.0,1199.0,2399.0,2400.0,10000.0,100000.0,300000.0})
        for(const auto offset: {UV{0,0},UV{77,79},UV{641,-329},UV{30000,40000}})
        {
            const auto plan=MakePlan(chart,chart.Direction(offset.u,offset.v),altitude);
            Require(!plan.patches.empty(),"plan nonempty");
            maximumPatches=std::max(maximumPatches,plan.patches.size());
            Require(plan.patches.size()<=MaxGroundPatches,"bounded patch count");
            Require(plan.Contains(chart.Direction(offset.u,offset.v),true,32),"ship inside detailed rectangle");
            if(altitude<2400) { Require(plan.fineStep==5,"landing 5m sampling"); Require(plan.Contains(chart.Direction(offset.u,offset.v),true,512),"512m nearby detail"); }
            else Require(plan.fineStep>5,"altitude coarsens detail");
            double area=0;
            for(const auto& patch:plan.patches)
            {
                const double w=patch.rect.x1-patch.rect.x0,h=patch.rect.y1-patch.rect.y0;
                Require(w>0&&h>0&&w<=64*patch.step&&h<=64*patch.step,"upload dimensions bounded");
                Require(std::abs(w/patch.step-std::round(w/patch.step))<1e-8&&std::abs(h/patch.step-std::round(h/patch.step))<1e-8,"grid edges aligned");
                area+=w*h;
            }
            Require(std::abs(area-(plan.outer.x1-plan.outer.x0)*(plan.outer.y1-plan.outer.y0))<1,"exact rectangle coverage area");
            for(std::size_t i=0;i<plan.patches.size();++i) for(std::size_t j=i+1;j<plan.patches.size();++j)
            {
                const auto& a=plan.patches[i].rect; const auto& b=plan.patches[j].rect;
                Require(std::min(a.x1,b.x1)<=std::max(a.x0,b.x0)||std::min(a.y1,b.y1)<=std::max(a.y0,b.y0),"no overlapping surface interiors");
            }
            const auto capChart=Chart::At(plan.holeDirection,chart.radius);
            for(int n=0;n<72;++n)
            {
                const double a=n*2*Pi/72;
                const auto edge=plan.holeDirection*std::cos(plan.holeAngle)+(capChart.east*std::cos(a)+capChart.north*std::sin(a))*std::sin(plan.holeAngle);
                Require(plan.Contains(edge,false,1),"globe hole strictly inside completed terrain");
            }
        }
    }
    std::cout<<"maximum ground patches="<<maximumPatches<<"; pool"<<2*(MaxGroundPatches+MaxClastPatches)<<" retains ground+25 clast tiles active + pending\n";
    const auto chart=Chart::At(Radial(20.1908,30.7717),1737400);
    const auto a=MakePlan(chart,chart.radial,0),b=MakePlan(chart,chart.Direction(170,0),0);
    std::size_t reusable=0;
    for(const auto& p:a.patches) if(std::find(b.patches.begin(),b.patches.end(),p)!=b.patches.end()) ++reusable;
    Require(reusable>a.patches.size()/2,"most meshes reusable after near-grid shift");
    Require(SamePlan(a,MakePlan(chart,chart.Direction(20,30),0)),"sub-grid movement does not regenerate");
    std::cout<<"unchanged reusable patches after 170m shift="<<reusable<<"/"<<a.patches.size()<<"\n";
}
void ApproachCoverageChecks(const RealData& data)
{
    const auto site=Chart::At(Radial(20.1908,30.7717),data.radius);
    const auto entry=Chart::At(site.Direction(-3000,0),data.radius);
    std::size_t corridorSamples=0;
    for(const auto& chart:{site,entry}) for(int leg=0;leg<=12;++leg)
    {
        const double t=leg/12.0;
        const auto direction=site.Direction(-3000*(1-t),0);
        const auto plan=MakePlan(chart,direction,650*(1-t)+25*t);
        Require(plan.Contains(direction,true,512),"650m to25m flyover retains landing detail and movement headroom");
        double cx=0,cy=0; chart.Project(direction,cx,cy);
        for(double distance:{1000.0,2000.0,4000.0,8000.0,16000.0})
        for(int azimuth=0;azimuth<16;++azimuth)
        {
            const double a=azimuth*2*Pi/16;
            const double x=cx+distance*std::cos(a),y=cy+distance*std::sin(a);
            double step=1e30;
            for(const auto& patch:plan.patches) if(patch.rect.Contains(x,y)) step=std::min(step,patch.step);
            Require(step<=distance/100,"surrounding mid/far detail is present in every viewing direction");
            const auto p=chart.Direction(x,y);
            Require(std::isfinite(data.Height(std::asin(p.z)*180/Pi,std::atan2(p.y,p.x)*180/Pi)),"approach surrounding footprint resolves real source or global fallback");
            ++corridorSamples;
        }
    }
    // Cover large offsets and maximum horizon clamping, not just centered tiles.
    std::size_t maximum=0;
    for(int i=0;i<200;++i)
    {
        const double x=std::fmod(i*7919.0,90000)-45000,y=std::fmod(i*6151.0,90000)-45000;
        const auto plan=MakePlan(site,site.Direction(x,y),i%2?650.0:10000.0,2000000);
        maximum=std::max(maximum,plan.patches.size());
        Require(plan.patches.size()<=MaxGroundPatches,"maximum-relief horizon fits bounded pool");
    }
    std::cout<<"approach surrounding coverage samples="<<corridorSamples<<" maximum clamped-horizon patches="<<maximum<<"\n";
}
void MeshChecks(const RealData& data)
{
    const double engineFacing=UnrealFrontFaceReference();
    const HeightSampler sample=[&](double lat,double lon){ return data.Height(lat,lon); };
    std::size_t vertices=0,triangles=0,samples=0;
    const auto start=std::chrono::steady_clock::now();
    const auto chart=Chart::At(Radial(20.1908,30.7717),data.radius);
    const auto photoOrigin=chart.radial*(data.radius+data.Height(20.1908,30.7717)+1.6);
    const auto plan=MakePlan(chart,chart.radial,0);
    for(const auto& patch:plan.patches)
    {
        const auto mesh=BuildPatch(chart,patch,sample);
        Require(mesh.complete,"real Apollo/global mesh complete");
        Require(mesh.vertices.size()<10000,"single upload vertex ceiling");
        Require(mesh.sourceSamples<35000,"bounded independent-normal and edge-morph samples");
        vertices+=mesh.vertices.size(); triangles+=mesh.indices.size()/3; samples+=mesh.sourceSamples;
        for(const auto& v:mesh.vertices)
        {
            Require(v.position.IsFinite()&&v.normal.IsFinite()&&v.tangent.IsFinite(),"finite vertex attributes");
            Require(std::abs(v.normal.Length()-1)<1e-9,"unit terrain normal");
            Require(v.uv2.u==0,"measured terrain has no synthetic rock material mask");
            Require(std::isfinite(v.uv2.v)&&std::isfinite(v.uv3.u)&&std::isfinite(v.uv3.v),"fixed photographic ENU remains finite");
        }
        for(std::size_t i=0;i<mesh.surfaceIndexCount;i+=3)
        {
            const auto& va=mesh.vertices[static_cast<std::size_t>(mesh.indices[i])];
            const auto photoRebuilt=photoOrigin+chart.east*va.uv3.u+chart.north*va.uv3.v+chart.radial*va.uv2.v;
            Require((photoRebuilt-(mesh.anchorBodyMeters+va.position)).Length()<1e-7,"photographic ENU independently reconstructs measured point");
            const auto& vb=mesh.vertices[static_cast<std::size_t>(mesh.indices[i+1])];
            const auto& vc=mesh.vertices[static_cast<std::size_t>(mesh.indices[i+2])];
            const auto a=UnrealLocalCentimeters(va.position),b=UnrealLocalCentimeters(vb.position),c=UnrealLocalCentimeters(vc.position);
            const auto face=Vec3d::Cross(b-a,c-a);
            Require(Vec3d::Dot(face,UnrealLocalCentimeters(va.normal))*engineFacing>0,"real DEM surface matches UE standard visible top face after Y reflection");
            const auto radial=(mesh.anchorBodyMeters+(va.position+vb.position+vc.position)/3.0).Normalized();
            Require(Vec3d::Dot(face,UnrealLocalCentimeters(radial*80))*engineFacing>0,"surface front-facing from 80m above triangle");
            Require(std::max({va.uv0.u,vb.uv0.u,vc.uv0.u})-std::min({va.uv0.u,vb.uv0.u,vc.uv0.u})<0.5,"no longitude interpolation across globe");
        }
        Require((mesh.indices.size()>mesh.surfaceIndexCount)==(patch.skirtEdges!=0),"skirts only at external perimeter");
        for(std::size_t i=mesh.surfaceIndexCount;i<mesh.indices.size();++i)
            Require(mesh.indices[i]>=0&&static_cast<std::size_t>(mesh.indices[i])<mesh.vertices.size(),"skirt indices valid");
    }
    Require(std::abs(data.Height(20.1908,30.7717)-data.Global(20.1908,30.7717))>1,"real regional DEM actually replaces global source");
    const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"Apollo full ground plan: vertices="<<vertices<<" triangles="<<triangles<<" height reads="<<samples<<" build + checks ms="<<elapsed<<"\n";
    std::cout<<"Apollo height m="<<data.Height(20.1908,30.7717)<<" global m="<<data.Global(20.1908,30.7717)<<"\n";

    const Patch west{{-320,-160,0,160},5,0},east{{0,-160,320,160},5,0};
    const auto left=BuildPatch(chart,west,sample),right=BuildPatch(chart,east,sample);
    for(std::size_t row=0;row<=64;++row)
    {
        const auto& l=left.vertices[row*65+64]; const auto& r=right.vertices[row*65];
        Require((l.position+left.anchorBodyMeters-r.position-right.anchorBodyMeters).Length()<1e-8,"adjacent tile edge positions identical");
        Require((l.normal-r.normal).Length()<1e-9,"adjacent tile halo normals identical");
        Require(std::abs(l.uv1.u-r.uv1.u)<1e-9&&std::abs(l.uv1.v-r.uv1.v)<1e-9,"grain coordinates retained across tile anchors");
    }
    const auto flat=BuildPatch(chart,west,[](double,double){ return 0.0; });
    std::size_t buried=0;
    for(const auto& v:flat.vertices)
    {
        const double depth=data.radius-(flat.anchorBodyMeters+v.position).Length();
        Require(std::abs(depth)<1e-6||std::abs(depth-80)<1e-6,"skirt extends radially below sampled terrain");
        if(depth>1) ++buried;
    }
    Require(buried>=512,"all four edges have skirt vertices");

    for(const auto coord:{UV{0,180},UV{0,-180},UV{90,0},UV{-90,0},UV{89.9997,179.99},UV{-89.9997,-179.99}})
    for(const auto offset:{UV{-157.5,-157.5},UV{-160,-157.5},UV{-157.5,-160},UV{-160,-160}})
    {
        const auto polarChart=Chart::At(Radial(coord.u,coord.v),data.radius);
        const Patch patch{{offset.u,offset.v,offset.u+320,offset.v+320},5,0};
        const auto mesh=BuildPatch(polarChart,patch,sample);
        Require(mesh.complete,"seam/pole mesh complete");
        for(std::size_t i=0;i<mesh.surfaceIndexCount;i+=3)
        {
            const auto& a=mesh.vertices[static_cast<std::size_t>(mesh.indices[i])];
            const auto& b=mesh.vertices[static_cast<std::size_t>(mesh.indices[i+1])];
            const auto& c=mesh.vertices[static_cast<std::size_t>(mesh.indices[i+2])];
            Require(std::max({a.uv0.u,b.uv0.u,c.uv0.u})-std::min({a.uv0.u,b.uv0.u,c.uv0.u})<=0.50001,"polar/seam triangle UV wrap");
            Require(Vec3d::Cross(b.position-a.position,c.position-a.position).Length()>1e-5,"no degenerate pole fan");
            const auto face=Vec3d::Cross(UnrealLocalCentimeters(b.position-a.position),UnrealLocalCentimeters(c.position-a.position));
            Require(Vec3d::Dot(face,UnrealLocalCentimeters((a.position+mesh.anchorBodyMeters).Normalized()))*engineFacing>0,"pole and seam triangles retain UE front face");
        }
    }
    std::atomic_bool cancel{true};
    const auto cancelled=BuildPatch(chart,plan.patches[0],sample,&cancel);
    Require(!cancelled.complete&&cancelled.vertices.empty()&&cancelled.sourceSamples==0,"pre-cancel avoids catalog reads");
    cancel.store(false); int reads=0;
    const auto interrupted=BuildPatch(chart,plan.patches[0],[&](double lat,double lon){ if(++reads==200) cancel.store(true); return data.Height(lat,lon); },&cancel);
    Require(!interrupted.complete&&reads<300,"in-flight cancellation bounded to one row");
    cancel.store(false); reads=0;
    const Patch coarseCancel{{0,0,1280,1280},20,1,{},0};
    const auto normalCancelled=BuildPatch(chart,coarseCancel,[&](double lat,double lon){if(++reads==5000)cancel.store(true);return data.Height(lat,lon);},&cancel);
    Require(!normalCancelled.complete&&reads<5400,"fixed-baseline normal sampling cancels within one row");
}
void PrecisionChecks()
{
    const Vec3d origin{1.433e12,-7.2e11,3.4e11},anchor=origin+Vec3d{300,-170,20};
    const Vec3d local{0.00123,-12.3456,3.123456};
    const auto anchorUE=ToUnrealCentimeters(anchor,origin);
    const auto vertexUE=UnrealLocalCentimeters(local);
    const Vec3d gpu{static_cast<float>(vertexUE.x),static_cast<float>(vertexUE.y),static_cast<float>(vertexUE.z)};
    Require((FromUnrealCentimeters(anchorUE+gpu,origin)-(anchor+local)).Length()<0.001,"astronomical origin preserves sub-mm local geometry");
    Require(vertexUE.y>0,"UE frame explicitly reflects Y");
}
void ClastChecks(const RealData& data)
{
    const auto chart=Chart::At(Radial(20.1908,30.7717),data.radius);
    const HeightSampler sample=[&](double lat,double lon){return data.Height(lat,lon);};
    const auto plan=MakePlan(chart,chart.radial,0);
    std::size_t count=0,maximumUpload=0;
    double buildMilliseconds=0;
    for(const auto& patch:plan.clastPatches)
    {
        const auto before=BuildPatch(chart,patch,sample);
        const auto start=std::chrono::steady_clock::now();
        auto mesh=BuildClastPatch(chart,patch,sample);
        buildMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        maximumUpload=std::max(maximumUpload,mesh.vertices.size());
        Require(mesh.complete,"reconstructed field completed");
        Require(mesh.vertices.size()<=9800,"clasts retain bounded GPU upload");
        Require(mesh.sourceSamples==before.sourceSamples&&mesh.sourceSamples<300,"bounded measured seating samples");
        Require(mesh.clasts.size()==mesh.reconstructedClastCount,"every clast has measured seating provenance");
        std::size_t clastVertices=0,clastIndices=0;
        for(const auto& clast:mesh.clasts)
        {
            Require(clast.firstVertex==clastVertices&&clast.firstIndex==clastIndices,"contiguous independent clast topology");
            clastVertices+=clast.vertexCount; clastIndices+=clast.indexCount;
            Require(clast.radius<=.295&&clast.height<=.3245,"original small noncolliding visual envelope");
            double lowest=1,highest=-1;
            const int sides=clast.primary?(clast.radius>.13?12:8):4;
            for(std::size_t j=0;j<clast.vertexCount;++j)
            {
                const auto& v=mesh.vertices[clast.firstVertex+j];
                const auto delta=v.position-clast.center;
                const double height=Vec3d::Dot(delta,clast.up);
                lowest=std::min(lowest,height); highest=std::max(highest,height);
                Require(std::abs(v.uv2.v-height)<1e-8,"material contact height matches actual geometry");
                Require(v.uv2.u==1&&v.uv3.u>=0&&v.uv3.u<1&&std::abs(v.uv3.v-clast.radius/.295)<1e-12,"stable clast shader attributes");
                Require((delta-clast.up*height).Length()<=clast.radius*1.13*1.15+1e-8,"no expanded horizontal obstacle envelope");
                if(j<static_cast<std::size_t>(sides)) Require(height<0,"every basal vertex buried for continuous contact");
            }
            Require(std::abs(lowest+.30*clast.height)<1e-8&&std::abs(highest-clast.height)<1e-8,"same buried base and maximum height");
            if(clast.primary)
            {
                double crease=0;
                for(int j=0;j<sides;++j)
                    crease+=(mesh.vertices[clast.firstVertex+sides+j].normal-mesh.vertices[clast.firstVertex+3*sides+1+j].normal).Length();
                Require(crease>1,"fractured shoulder is not globally smoothed into clay");
            }
        }
        Require(clastVertices==mesh.vertices.size()&&clastIndices==mesh.indices.size(),"separate clast mesh contains no duplicate terrain surface");
        for(std::size_t i=0;i<mesh.indices.size();i+=3)
        {
            const auto& a=mesh.vertices[static_cast<std::size_t>(mesh.indices[i])];
            const auto& b=mesh.vertices[static_cast<std::size_t>(mesh.indices[i+1])];
            const auto& c=mesh.vertices[static_cast<std::size_t>(mesh.indices[i+2])];
            const auto face=Vec3d::Cross(UnrealLocalCentimeters(b.position-a.position),UnrealLocalCentimeters(c.position-a.position));
            Require(Vec3d::Dot(face,UnrealLocalCentimeters(a.normal+b.normal+c.normal))<0,"clast faces retain UE front winding");
        }
        for(std::size_t i=0;i<mesh.vertices.size();++i)
        {
            const auto& v=mesh.vertices[i];
            Require(v.position.IsFinite()&&v.normal.IsFinite()&&std::abs(v.normal.Length()-1)<1e-9,"clast finite unit normals");
            Require(std::abs(Vec3d::Dot(v.normal,v.tangent))<1e-9,"clast orthogonal tangent");
            double x=0,y=0; chart.Project((v.position+mesh.anchorBodyMeters).Normalized(),x,y);
            const double r=std::sqrt(x*x+y*y);
            Require(r>4.5&&r<4097,"bounded field and clear contact footprint");
        }
        auto repeat=BuildClastPatch(chart,patch,sample);
        Require(repeat.vertices.size()==mesh.vertices.size()&&repeat.indices==mesh.indices,"deterministic reconstructed topology");
        for(std::size_t i=0;i<mesh.vertices.size();++i)
            Require((repeat.vertices[i].position-mesh.vertices[i].position).Length()==0,"deterministic reconstructed placement");
        count+=mesh.reconstructedClastCount;
    }
    Require(count>5500&&count<10000,"dense near field with bounded population");
    const auto field=[&](Vec3d direction,bool primaryOnly=false) {
        std::vector<std::pair<std::pair<double,double>,Vec3d>> tops;
        for(auto p:MakePlan(chart,direction,0).clastPatches)
        {
            if(primaryOnly) p.level=1;
            auto m=BuildClastPatch(chart,p,sample);
            Require(m.vertices.size()<=9800,"shifted field within upload budget");
            for(const auto& c:m.clasts)
                tops.push_back({{c.siteMeters.u,c.siteMeters.v},c.center+m.anchorBodyMeters});
        }
        std::sort(tops.begin(),tops.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        return tops;
    };
    const auto original=field(chart.radial,true);
    for(const auto offset:{UV{70,0},UV{-70,0},UV{0,70},UV{170,170},UV{350,0},UV{-350,0},UV{0,500}})
    {
        const auto moved=field(chart.Direction(offset.u,offset.v));
        const auto movedPrimary=field(chart.Direction(offset.u,offset.v),true);
        const auto shifted=MakePlan(chart,chart.Direction(offset.u,offset.v),0);
        const auto& first=shifted.clastPatches.front().rect;
        const auto& last=shifted.clastPatches.back().rect;
        const Rect window{first.x0,first.y0,last.x1,last.y1};
        std::size_t common=0,nearEye=0;
        for(const auto& v:original)
        {
            double x=0,y=0; chart.Project(v.second.Normalized(),x,y);
            if(!window.Contains(x,y,1)) continue;
            const auto found=std::lower_bound(movedPrimary.begin(),movedPrimary.end(),v.first,[](const auto& a,const auto& key){return a.first<key;});
            Require(found!=movedPrimary.end()&&found->first==v.first,"patch border and density LOD retain shared primary clasts");
            Require((found->second-v.second).Length()<1e-6,"EVA tile shift preserves clast placement");
            ++common;
        }
        for(std::size_t i=0;i<moved.size();++i)
        {
            Require(i==0||moved[i-1].first!=moved[i].first,"no duplicated clasts at patch borders");
            double x=0,y=0; chart.Project(moved[i].second.Normalized(),x,y);
            if((x-offset.u)*(x-offset.u)+(y-offset.v)*(y-offset.v)<100*100) ++nearEye;
        }
        if(std::abs(offset.u)<200&&std::abs(offset.v)<200) Require(common>500,"meaningful shared field compared");
        Require(nearEye>2000,"EVA retains photo-scale nearby population beyond old96m edge");
    }
    const Patch patch{{0,0,60,60},5,0,{},0};
    const auto near=BuildClastPatch(chart,patch,sample);
    auto farPatch=patch; farPatch.level=1;
    const auto far=BuildClastPatch(chart,farPatch,sample);
    std::size_t primary=0;
    for(const auto& c:near.clasts) if(c.primary)
    {
        Require(primary<far.clasts.size(),"density LOD retains every primary");
        const auto& d=far.clasts[primary++];
        Require(c.siteMeters.u==d.siteMeters.u&&c.siteMeters.v==d.siteMeters.v&&c.vertexCount==d.vertexCount,"primary identity and topology match through density LOD");
        for(std::size_t i=0;i<c.vertexCount;++i)
        {
            const auto& a=near.vertices[c.firstVertex+i];const auto& b=far.vertices[d.firstVertex+i];
            Require((a.position-b.position).Length()==0&&(a.normal-b.normal).Length()==0&&a.uv3.u==b.uv3.u,"primary silhouette normals and material remain exact across LOD");
        }
    }
    Require(primary==far.clasts.size(),"density LOD adds no primary objects");
    std::atomic_bool cancel{true}; auto mesh=BuildClastPatch(chart,patch,sample,&cancel);
    Require(!mesh.complete&&mesh.reconstructedClastCount==0,"clast cancellation rejects incomplete upload");
    const auto other=Chart::At(Radial(-20,120),data.radius);
    mesh=BuildClastPatch(other,patch,sample);
    Require(mesh.reconstructedClastCount==0,"no reconstructed rocks elsewhere on Moon");
    std::cout<<"Apollo visual reconstruction clasts="<<count<<"; maximum upload vertices="<<maximumUpload
        <<"; append CPU ms="<<buildMilliseconds<<"; measured DEM untouched\n";
}
void SeamChecks(const RealData& data,Vec3d chartDirection,UV observer)
{
    const auto chart=Chart::At(chartDirection,data.radius);
    const HeightSampler sample=[&](double lat,double lon){return data.Height(lat,lon);};
    const auto plan=MakePlan(chart,chart.Direction(observer.u,observer.v),0);
    std::vector<Mesh> meshes;
    for(const auto& p:plan.patches) meshes.push_back(BuildPatch(chart,p,sample));
    std::size_t joined=0;
    double maximumGap=0,maximumNormalAngle=0;
    for(std::size_t i=0;i<plan.patches.size();++i)
    {
        const auto& fine=plan.patches[i];
        const auto& fm=meshes[i];
        const int fnx=static_cast<int>(std::llround((fine.rect.x1-fine.rect.x0)/fine.step));
        for(unsigned edge=0;edge<4;++edge)
        {
            if(fine.edgeStep[edge]==0) continue;
            Require((fine.skirtEdges&(1u<<edge))==0,"internal LOD boundary has no buried wall");
            const bool vertical=edge<2;
            for(std::size_t j=0;j<plan.patches.size();++j)
            {
                const auto& coarse=plan.patches[j];
                if(coarse.step!=fine.edgeStep[edge]) continue;
                const bool adjacent=edge==0?fine.rect.x0==coarse.rect.x1:
                    edge==1?fine.rect.x1==coarse.rect.x0:edge==2?fine.rect.y0==coarse.rect.y1:fine.rect.y1==coarse.rect.y0;
                if(!adjacent) continue;
                const double lo=std::max(vertical?fine.rect.y0:fine.rect.x0,vertical?coarse.rect.y0:coarse.rect.x0);
                const double hi=std::min(vertical?fine.rect.y1:fine.rect.x1,vertical?coarse.rect.y1:coarse.rect.x1);
                if(hi<=lo) continue;
                const auto& cm=meshes[j];
                const int cnx=static_cast<int>(std::llround((coarse.rect.x1-coarse.rect.x0)/coarse.step));
                const auto coarseVertex=[&](double along)->const Vertex& {
                    const double cx=vertical?(edge==0?coarse.rect.x1:coarse.rect.x0):along;
                    const double cy=vertical?along:(edge==2?coarse.rect.y1:coarse.rect.y0);
                    const int ix=static_cast<int>(std::llround((cx-coarse.rect.x0)/coarse.step));
                    const int iy=static_cast<int>(std::llround((cy-coarse.rect.y0)/coarse.step));
                    return cm.vertices[static_cast<std::size_t>(iy*(cnx+1)+ix)];
                };
                for(double along=lo;along<=hi+fine.step*.1;along+=fine.step)
                {
                    const double fx=vertical?(edge==0?fine.rect.x0:fine.rect.x1):along;
                    const double fy=vertical?along:(edge==2?fine.rect.y0:fine.rect.y1);
                    const int ix=static_cast<int>(std::llround((fx-fine.rect.x0)/fine.step));
                    const int iy=static_cast<int>(std::llround((fy-fine.rect.y0)/fine.step));
                    const auto actual=fm.vertices[static_cast<std::size_t>(iy*(fnx+1)+ix)].position+fm.anchorBodyMeters;
                    double lower=std::floor(along/coarse.step)*coarse.step;
                    if(lower>=hi) lower=hi-coarse.step;
                    const double t=(along-lower)/coarse.step;
                    const auto& ca=coarseVertex(lower); const auto& cb=coarseVertex(lower+coarse.step);
                    const auto expected=(ca.position+cm.anchorBodyMeters)*(1-t)+(cb.position+cm.anchorBodyMeters)*t;
                    const double gap=(actual-expected).Length();
                    maximumGap=std::max(maximumGap,gap);
                    Require(gap<1e-5,"fine boundary lies on actual coarse polyline");
                    const auto expectedNormal=(ca.normal*(1-t)+cb.normal*t).Normalized();
                    const auto actualNormal=fm.vertices[static_cast<std::size_t>(iy*(fnx+1)+ix)].normal;
                    maximumNormalAngle=std::max(maximumNormalAngle,std::acos(Clamp(Vec3d::Dot(actualNormal,expectedNormal),-1,1))*180/Pi);
                    ++joined;
                }
            }
        }
    }
    Require(joined>1000,"meaningful multi-LOD edge coverage");
    const Patch fine{{-160,-160,160,160},5,0,{},0},coarse{{-160,-160,160,160},20,1,{},0};
    const auto fm=BuildPatch(chart,fine,sample),cm=BuildPatch(chart,coarse,sample);
    for(int y=0;y<=16;++y) for(int x=0;x<=16;++x)
        Require((fm.vertices[static_cast<std::size_t>(y*4*65+x*4)].normal-cm.vertices[static_cast<std::size_t>(y*17+x)].normal).Length()<1e-9,
            "same measured normal baseline at shared fine/coarse posts");
    std::cout<<"LOD joined boundary samples="<<joined<<" maximum gap m="<<maximumGap
        <<" maximum shading normal mismatch deg="<<maximumNormalAngle<<"\n";
    Require(maximumNormalAngle<0.001,"shared LOD edge shading agrees with coarse normal interpolation");
}
}
int main(int argc,char** argv)
{
    try {
        Require(argc==3,"data directory and metadata arguments");
        const RealData data(std::filesystem::u8path(argv[1]),std::filesystem::u8path(argv[2]));
        PlanChecks(); ApproachCoverageChecks(data); MeshChecks(data); PrecisionChecks(); ClastChecks(data);
        const auto site=Chart::At(Radial(20.1908,30.7717),data.radius);
        SeamChecks(data,site.radial,{170,79});
        SeamChecks(data,site.radial,{-3000,0});
        SeamChecks(data,site.Direction(-3000,0),{2997,127});
        std::cout<<"PASS "<<Checks<<" assertions; portable geometry only (UE/RHI/4K runtime pending)\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<" after "<<Checks<<" assertions\n"; return 1; }
}
