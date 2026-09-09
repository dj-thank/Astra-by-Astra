// CPU proof only. Compile against candidate or baseline terrain source/header.
// Requires repository root include for the existing read-only real DEM fixture.
#include "Terrain/LunarTerrainGeometry.h"
#include "Tests/Validation/RealDem.h"
#include <iomanip>
using namespace star::terrain;
int main(int argc,char** argv)
{
    if(argc!=4) return 2;
    const RealData data(std::filesystem::u8path(argv[1]),std::filesystem::u8path(argv[2]));
    const auto chart=Chart::At(Radial(20.1908,30.7717),data.radius);
    const HeightSampler sample=[&](double lat,double lon){return data.Height(lat,lon);};
    const Patch patch{{0,0,60,60},5,0,{},0};
    const auto ground=BuildPatch(chart,patch,sample),clasts=BuildClastPatch(chart,patch,sample);
    if(!ground.complete||!clasts.complete) return 3;
    const auto origin=chart.radial*(chart.radius+sample(20.1908,30.7717));
    const auto enu=[&](Vec3d p){return Vec3d{Vec3d::Dot(p,chart.east),Vec3d::Dot(p,chart.north),Vec3d::Dot(p,chart.radial)};};
    std::ofstream out(std::filesystem::u8path(argv[3])); out<<std::setprecision(12);
    out<<"{\"status\":\"CPU_GEOMETRY_EXPORT_NOT_GAME\",\"clastCount\":"<<clasts.reconstructedClastCount;
    const auto write=[&](const char* name,const Mesh& mesh){
        out<<",\""<<name<<"\":{\"positions\":[";
        for(std::size_t i=0;i<mesh.vertices.size();++i)
        { const auto p=enu(mesh.vertices[i].position+mesh.anchorBodyMeters-origin); if(i)out<<',';out<<'['<<p.x<<','<<p.y<<','<<p.z<<']'; }
        out<<"],\"normals\":[";
        for(std::size_t i=0;i<mesh.vertices.size();++i)
        { const auto n=enu(mesh.vertices[i].normal); if(i)out<<',';out<<'['<<n.x<<','<<n.y<<','<<n.z<<']'; }
        out<<"],\"indices\":[";
        for(std::size_t i=0;i<mesh.indices.size();++i){if(i)out<<',';out<<mesh.indices[i];}
        out<<"]}";
    };
    write("ground",ground); write("clasts",clasts);
#ifdef STAR_CLAST_V4
    out<<",\"ranges\":[";
    for(std::size_t i=0;i<clasts.clasts.size();++i)
    {
        const auto& c=clasts.clasts[i]; const auto p=enu(c.center+clasts.anchorBodyMeters-origin);
        if(i)out<<',';out<<"{\"first\":"<<c.firstVertex<<",\"count\":"<<c.vertexCount<<",\"radius\":"<<c.radius;
        out<<",\"center\":["<<p.x<<','<<p.y<<','<<p.z<<"]}";
    }
    out<<']';
#endif
    out<<"}\n";
    std::cout<<"clasts="<<clasts.reconstructedClastCount<<" vertices="<<clasts.vertices.size()<<" triangles="<<clasts.indices.size()/3<<"\n";
}
