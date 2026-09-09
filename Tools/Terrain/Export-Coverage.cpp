// Read the exact same5m/10m/global fixture as native terrain tests. This exports
// CPU geometry evidence only, never a rendered game/photo-fidelity result.
#define main StarTerrainTestMain
#include "../../Tests/Terrain/LunarTerrainTests.cpp"
#undef main
#include <iomanip>

namespace {
int SourceTier(const RealData& data,double lat,double lon)
{
    if(lon>data.west&&lon<data.east&&lat>data.south&&lat<data.north)
    {
        const double x=(lon-data.west)/(data.east-data.west)*data.rw-.5;
        const double y=(data.north-lat)/(data.north-data.south)*data.rh-.5;
        const double edge=std::min({x,data.rw-1-x,y,data.rh-1-y});
        if(edge>=60) return 4; // Existing regional mask is100% valid.
        if(edge>=0) return 3;
    }
    if(data.far.empty()||lon<=data.fwest||lon>=data.feast||lat<=data.fsouth||lat>=data.fnorth) return 0;
    const double x=(lon-data.fwest)/(data.feast-data.fwest)*data.fw-.5;
    const double y=(data.fnorth-lat)/(data.fnorth-data.fsouth)*data.fh-.5;
    const int ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));
    if(ix<0||iy<0||ix+1>=data.fw||iy+1>=data.fh) return 0;
    unsigned minimum=255;
    for(int row=0;row<2;++row) for(int col=0;col<2;++col)
        minimum=std::min(minimum,static_cast<unsigned>(data.confidence[static_cast<std::size_t>((iy+row)*data.fw+ix+col)]));
    return minimum==255?2:minimum>11?1:0;
}
}
int main(int argc,char** argv)
{
    if(argc!=4) return 2;
    const RealData data(std::filesystem::u8path(argv[1]),std::filesystem::u8path(argv[2]));
    const auto directory=std::filesystem::u8path(argv[3]);
    std::filesystem::create_directories(directory);
    const auto chart=Chart::At(Radial(20.1908,30.7717),data.radius);
    const HeightSampler sample=[&](double lat,double lon){return data.Height(lat,lon);};
    for(const auto& scenario:{std::pair<const char*,double>{"landing",0},std::pair<const char*,double>{"approach",-3000}})
    {
        const auto start=std::chrono::steady_clock::now();
        const auto plan=MakePlan(chart,chart.Direction(scenario.second,0),scenario.second==0?25:650);
        std::vector<Mesh> meshes; std::size_t vertices=0,triangles=0,reads=0;
        std::ofstream patches(directory/(std::string(scenario.first)+"-patches.csv"));
        patches<<"x0,y0,x1,y1,step,level\n";
        for(const auto& patch:plan.patches)
        {
            patches<<patch.rect.x0<<','<<patch.rect.y0<<','<<patch.rect.x1<<','<<patch.rect.y1<<','<<patch.step<<','<<patch.level<<'\n';
            meshes.push_back(BuildPatch(chart,patch,sample));
            const auto& mesh=meshes.back(); if(!mesh.complete)return 3;
            vertices+=mesh.vertices.size();triangles+=mesh.surfaceIndexCount/3;reads+=mesh.sourceSamples;
        }
        const double buildMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::ofstream raster(directory/(std::string(scenario.first)+"-samples.csv"));
        raster<<std::setprecision(12)<<"east,north,height,meshHeight,error,sourceTier,meshStep\n";
        for(int yi=-200;yi<=200;++yi) for(int xi=-200;xi<=200;++xi)
        {
            // Non-post-aligned samples reveal actual triangle interpolation loss.
            const double x=xi*100.0+13.7,y=yi*100.0+23.9;
            std::size_t index=0;
            while(index<plan.patches.size()&&!plan.patches[index].rect.Contains(x,y))++index;
            if(index==plan.patches.size())return 4;
            const auto& patch=plan.patches[index]; const auto& mesh=meshes[index];
            const int nx=static_cast<int>(std::llround((patch.rect.x1-patch.rect.x0)/patch.step));
            const int ny=static_cast<int>(std::llround((patch.rect.y1-patch.rect.y0)/patch.step));
            const double gx=(x-patch.rect.x0)/patch.step,gy=(y-patch.rect.y0)/patch.step;
            const int ix=std::min(nx-1,static_cast<int>(gx)),iy=std::min(ny-1,static_cast<int>(gy));
            const double fx=gx-ix,fy=gy-iy;
            const auto at=[&](int cx,int cy){return mesh.vertices[static_cast<std::size_t>(cy*(nx+1)+cx)].position+mesh.anchorBodyMeters;};
            const auto actual=fx+fy<=1?at(ix,iy)*(1-fx-fy)+at(ix+1,iy)*fx+at(ix,iy+1)*fy:
                at(ix+1,iy)*(1-fy)+at(ix,iy+1)*(1-fx)+at(ix+1,iy+1)*(fx+fy-1);
            const auto direction=chart.Direction(x,y);
            const double lat=std::asin(direction.z)*180/Pi,lon=std::atan2(direction.y,direction.x)*180/Pi;
            const double measured=sample(lat,lon),rendered=Vec3d::Dot(actual,direction)-chart.radius;
            raster<<x<<','<<y<<','<<measured<<','<<rendered<<','<<rendered-measured<<','<<SourceTier(data,lat,lon)<<','<<patch.step<<'\n';
        }
        std::ofstream summary(directory/(std::string(scenario.first)+"-summary.json"));
        summary<<"{\"status\":\"CPU_GEOMETRY_NOT_GAME\",\"patches\":"<<plan.patches.size()<<",\"vertices\":"<<vertices
            <<",\"surfaceTriangles\":"<<triangles<<",\"heightReads\":"<<reads<<",\"buildMs\":"<<buildMs
            <<",\"outerSpanMeters\":"<<plan.outer.x1-plan.outer.x0<<",\"globeHoleAngleDegrees\":"<<plan.holeAngle*180/Pi<<"}\n";
        std::cout<<scenario.first<<" vertices="<<vertices<<" buildMs="<<buildMs<<"\n";
    }
}
