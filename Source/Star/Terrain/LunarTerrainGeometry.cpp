#include "Terrain/LunarTerrainGeometry.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace star::terrain {
namespace {
double Clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
double Snap(double value, double step) { return std::round(value / step) * step; }
bool Cancelled(const std::atomic_bool* c) { return c && c->load(std::memory_order_relaxed); }
bool Equal(const Rect& a, const Rect& b) { return a.x0 == b.x0 && a.x1 == b.x1 && a.y0 == b.y0 && a.y1 == b.y1; }
bool Equal(Vec3d a, Vec3d b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool ValidChart(const Chart& c) {
    const auto unit = [](Vec3d v) { return v.IsFinite() && std::abs(v.LengthSquared()-1) < 1e-8; };
    return std::isfinite(c.radius) && c.radius >= 1 && c.radius <= 1e11 &&
        unit(c.radial) && unit(c.east) && unit(c.north) &&
        std::abs(Vec3d::Dot(c.radial,c.east)) < 1e-8 &&
        std::abs(Vec3d::Dot(c.radial,c.north)) < 1e-8 &&
        Vec3d::Dot(Vec3d::Cross(c.radial,c.east),c.north) > 1-1e-8;
}
Vec3d ApolloSiteDirection()
{
    constexpr double latitude=20.1908*Pi/180,longitude=30.7717*Pi/180;
    return {std::cos(latitude)*std::cos(longitude),std::cos(latitude)*std::sin(longitude),std::sin(latitude)};
}
void AddRectangle(Plan& plan, Rect rect, double step, unsigned level)
{
    // Bound each RHI upload and keep vertex coordinates local to a small patch.
    const double span = step * MaxPatchCells;
    for (double y = rect.y0; y < rect.y1 - step * 0.1; y += span)
        for (double x = rect.x0; x < rect.x1 - step * 0.1; x += span)
            plan.patches.push_back({{x,y,std::min(x+span,rect.x1),std::min(y+span,rect.y1)},step,level});
}
double EdgeDistance(Vec3d center, Vec3d inward) { return std::asin(Clamp(Vec3d::Dot(center,inward.Normalized()),-1.0,1.0)); }
}

bool Rect::Contains(double x, double y, double margin) const
{
    return x >= x0 + margin && x <= x1 - margin && y >= y0 + margin && y <= y1 - margin;
}
Chart Chart::At(Vec3d direction, double radiusMeters)
{
    Chart c;
    if (!direction.IsFinite() || direction.Length() < 1e-12) { c.radius = 0; return c; }
    c.radial = direction.Normalized();
    // A chart is retained as the ship moves. The polar fallback cannot rotate an
    // existing chart or make it singular when the ship subsequently crosses a pole.
    const Vec3d reference = std::abs(c.radial.z) > 0.99 ? Vec3d{1,0,0} : Vec3d{0,0,1};
    c.east = Vec3d::Cross(reference,c.radial).Normalized();
    c.north = Vec3d::Cross(c.radial,c.east).Normalized();
    c.radius = radiusMeters;
    return c;
}
Vec3d Chart::Direction(double x, double y) const { return (radial*radius + east*x + north*y).Normalized(); }
bool Chart::Project(Vec3d direction, double& x, double& y) const
{
    if (!ValidChart(*this) || !direction.IsFinite() || direction.Length() < 1e-12) return false;
    direction = direction.Normalized();
    const double d = Vec3d::Dot(direction,radial);
    if (d <= 0.1) return false;
    const double nextX = radius * Vec3d::Dot(direction,east) / d;
    const double nextY = radius * Vec3d::Dot(direction,north) / d;
    if (!std::isfinite(nextX) || !std::isfinite(nextY)) return false;
    x = nextX; y = nextY;
    return true;
}
bool Patch::operator==(const Patch& other) const {
    return level == other.level && step == other.step && Equal(rect,other.rect) &&
        edgeStep == other.edgeStep && skirtEdges == other.skirtEdges;
}
bool Plan::Contains(Vec3d direction, bool fineOnly, double margin) const
{
    double x = 0, y = 0;
    return chart.Project(direction,x,y) && (fineOnly ? fine : outer).Contains(x,y,margin);
}
Plan MakePlan(const Chart& chart, Vec3d direction, double altitude, double terrainRange)
{
    Plan plan;
    plan.chart = chart;
    double x = 0, y = 0;
    if (!chart.Project(direction,x,y) || !std::isfinite(altitude) || !std::isfinite(terrainRange)) return plan;
    // 5m posts through the final 1.2km. Power-of-two changes keep all rings aligned.
    const double level = std::floor(std::log2(std::max(1.0, altitude / 1200.0)));
    plan.fineStep = 5.0 * std::pow(2.0,Clamp(level,0.0,8.0));
    double half = plan.fineStep * 128;
    double centerX = Snap(x,plan.fineStep*32), centerY = Snap(y,plan.fineStep*32);
    plan.fine = {centerX-half,centerY-half,centerX+half,centerY+half};
    plan.outer = plan.fine;
    AddRectangle(plan,plan.fine,plan.fineStep,0);

    // Include the geometric horizon plus relief. At ground level this reaches
    // hundreds of km, not a flat small patch whose far edge is visible.
    const double viewHeight = std::max(0.0,altitude) + std::max(1000.0,terrainRange) + 100;
    const double horizon = std::sqrt(viewHeight*(2.0*chart.radius+viewHeight));
    const double required = Clamp(horizon * 1.15,81920.0,1310720.0);
    double outerStep = plan.fineStep;
    for (unsigned ring = 1; half < required && ring <= 12; ++ring)
    {
        half *= 2;
        // Preserve intermediate measured relief around an approaching/walking
        // observer: 5 -> 10 -> 20 -> 40m, instead of jumping immediately to20m.
        // Beyond20.48km retain the previous horizon budget. Grid density does
        // not claim better source resolution outside the regional DTM.
        const unsigned densityOffset = half <= 20480.0 ? 0u : 1u;
        outerStep = plan.fineStep * std::pow(2.0,static_cast<double>(ring+densityOffset));
        // Keep the existing world-fixed streaming thresholds despite denser
        // vertices; small observer movement must not rebuild twice as often.
        const double centerStep=outerStep*(densityOffset==0?16:8);
        centerX = Snap(x,centerStep); centerY = Snap(y,centerStep);
        const Rect inner = plan.outer;
        plan.outer = {centerX-half,centerY-half,centerX+half,centerY+half};
        AddRectangle(plan,{plan.outer.x0,plan.outer.y0,plan.outer.x1,inner.y0},outerStep,ring);
        AddRectangle(plan,{plan.outer.x0,inner.y1,plan.outer.x1,plan.outer.y1},outerStep,ring);
        AddRectangle(plan,{plan.outer.x0,inner.y0,inner.x0,inner.y1},outerStep,ring);
        AddRectangle(plan,{inner.x1,inner.y0,plan.outer.x1,inner.y1},outerStep,ring);
    }
    const double midX = (plan.outer.x0+plan.outer.x1)*0.5, midY = (plan.outer.y0+plan.outer.y1)*0.5;
    plan.holeDirection = chart.Direction(midX,midY);
    // Each gnomonic rectangle edge is a great-circle plane. This spherical cap is
    // strictly inside all four boundaries, even off-chart-center and at the poles.
    double angle = std::min({
        EdgeDistance(plan.holeDirection,chart.east-chart.radial*(plan.outer.x0/chart.radius)),
        EdgeDistance(plan.holeDirection,-chart.east+chart.radial*(plan.outer.x1/chart.radius)),
        EdgeDistance(plan.holeDirection,chart.north-chart.radial*(plan.outer.y0/chart.radius)),
        EdgeDistance(plan.holeDirection,-chart.north+chart.radial*(plan.outer.y1/chart.radius))});
    plan.holeAngle = std::max(0.0,angle - 2.0*outerStep/chart.radius - 0.001);
    // Mark shared boundaries, including equal-LOD tile edges. Fine posts will
    // land on the coarse polyline; buried internal walls are no longer needed.
    for(auto& a:plan.patches) for(const auto& b:plan.patches)
    {
        const bool overlapY=std::min(a.rect.y1,b.rect.y1)>std::max(a.rect.y0,b.rect.y0);
        const bool overlapX=std::min(a.rect.x1,b.rect.x1)>std::max(a.rect.x0,b.rect.x0);
        const bool neighbor[4]={a.rect.x0==b.rect.x1&&overlapY,a.rect.x1==b.rect.x0&&overlapY,
            a.rect.y0==b.rect.y1&&overlapX,a.rect.y1==b.rect.y0&&overlapX};
        for(unsigned edge=0;edge<4;++edge) if(neighbor[edge])
        {
            a.skirtEdges &= ~(1u<<edge);
            if(b.step>a.step) a.edgeStep[edge]=std::max(a.edgeStep[edge],b.step);
        }
    }
    // This window streams a fixed lunar population. It does not reseed or move
    // rocks when the observer walks; only whole world-fixed tiles are replaced.
    if(plan.fineStep==5.0 && altitude<150.0 && Vec3d::Dot(direction.Normalized(),ApolloSiteDirection())>std::cos(4350.0/chart.radius))
    {
        const double clastX=std::floor((x+1e-6)/60.0)*60.0,clastY=std::floor((y+1e-6)/60.0)*60.0;
        for(int iy=-2;iy<=2;++iy) for(int ix=-2;ix<=2;++ix)
            plan.clastPatches.push_back({{clastX+ix*60,clastY+iy*60,clastX+(ix+1)*60,clastY+(iy+1)*60},
                5,static_cast<unsigned>(std::max(std::abs(ix),std::abs(iy))>1),{},0});
    }
    return plan;
}
bool SamePlan(const Plan& a, const Plan& b)
{
    return a.chart.radius == b.chart.radius && Equal(a.chart.radial,b.chart.radial) &&
        Equal(a.chart.east,b.chart.east) && Equal(a.chart.north,b.chart.north) &&
        a.fineStep == b.fineStep && a.patches == b.patches &&
        a.clastPatches == b.clastPatches;
}
Vec3d UnrealLocalCentimeters(Vec3d v) { return {100*v.x,-100*v.y,100*v.z}; }

Mesh BuildPatch(const Chart& chart, const Patch& patch, const HeightSampler& sample, const std::atomic_bool* cancel)
{
    Mesh mesh;
    if (!ValidChart(chart) || !sample || Cancelled(cancel) ||
        !std::isfinite(patch.step) || patch.step <= 0 ||
        !std::isfinite(patch.rect.x0) || !std::isfinite(patch.rect.x1) ||
        !std::isfinite(patch.rect.y0) || !std::isfinite(patch.rect.y1)) return mesh;
    for (double edge : patch.edgeStep) if (!std::isfinite(edge) || edge < 0) return mesh;
    const double cellsX = (patch.rect.x1-patch.rect.x0)/patch.step;
    const double cellsY = (patch.rect.y1-patch.rect.y0)/patch.step;
    const auto validCells = [](double n) {
        return std::isfinite(n) && n >= 1 && n <= MaxPatchCells && std::abs(n-std::round(n)) < 1e-8;
    };
    if (!validCells(cellsX) || !validCells(cellsY) ||
        patch.rect.x0 + patch.step <= patch.rect.x0 || patch.rect.x1 + patch.step <= patch.rect.x1 ||
        patch.rect.y0 + patch.step <= patch.rect.y0 || patch.rect.y1 + patch.step <= patch.rect.y1) return mesh;
    const int nx = static_cast<int>(std::round(cellsX)), ny = static_cast<int>(std::round(cellsY));
    bool samplesValid = true;
    const auto position = [&](double x, double y) {
        const auto radial = chart.Direction(x,y);
        const double lat = std::asin(Clamp(radial.z,-1.0,1.0))*180/Pi;
        // Longitude is undefined at the exact pole: use one datum sample there.
        const double lon = std::abs(radial.z) > 1.0-1e-14 ? 0.0 : std::atan2(radial.y,radial.x)*180/Pi;
        const double height = sample(lat,lon);
        ++mesh.sourceSamples;
        const auto point = radial*(chart.radius+height);
        if (!std::isfinite(height) || chart.radius+height <= 0 || !point.IsFinite()) samplesValid = false;
        return point;
    };
    mesh.anchorBodyMeters = position((patch.rect.x0+patch.rect.x1)*0.5,(patch.rect.y0+patch.rect.y1)*0.5);
    if (!mesh.anchorBodyMeters.IsFinite()) return mesh;
    // Fixed photographic camera, independent of the moving terrain chart.
    const double photoLat=20.1908*Pi/180,photoLon=30.7717*Pi/180;
    const auto photoChart=Chart::At({std::cos(photoLat)*std::cos(photoLon),std::cos(photoLat)*std::sin(photoLon),std::sin(photoLat)},chart.radius);
    const double photoHeight=sample(20.1908,30.7717);
    ++mesh.sourceSamples;
    const auto photoOrigin=photoChart.radial*(chart.radius+(std::isfinite(photoHeight)?photoHeight:0)+1.6);

    // A one-post halo gives central-difference normals with ONE source height per
    // grid point. Near 5m geometry and normals use measured 5m terrain, not noise.
    const int stride = nx+3;
    std::vector<Vec3d> positions(static_cast<std::size_t>(stride*(ny+3)));
    for (int y = -1; y <= ny+1; ++y)
    {
        if (Cancelled(cancel)) return mesh;
        for (int x = -1; x <= nx+1; ++x)
        {
            auto p = position(patch.rect.x0+x*patch.step,patch.rect.y0+y*patch.step);
            if (!p.IsFinite()) return mesh;
            positions[static_cast<std::size_t>((y+1)*stride+x+1)] = p;
        }
    }
    const auto at = [&](int x, int y) { return positions[static_cast<std::size_t>((y+1)*stride+x+1)]; };
    const auto morphPosition = [&](double x,double y,Vec3d p) {
        for(unsigned edge=0;edge<4;++edge)
        {
            const double coarse=patch.edgeStep[edge];
            if(coarse<=patch.step) continue;
            const double distances[4]={x-patch.rect.x0,patch.rect.x1-x,y-patch.rect.y0,patch.rect.y1-y};
            const double t=Clamp(1-distances[edge]/(2*coarse),0,1);
            if(t<=0) continue;
            const bool vertical=edge<2;
            const double along=vertical?y:x;
            const double lo=std::floor(along/coarse)*coarse,blend=(along-lo)/coarse;
            const double fixed=edge==0?patch.rect.x0:(edge==1?patch.rect.x1:(edge==2?patch.rect.y0:patch.rect.y1));
            const auto a=vertical?position(fixed,lo):position(lo,fixed);
            const auto b=vertical?position(fixed,lo+coarse):position(lo+coarse,fixed);
            const auto measured=vertical?position(fixed,along):position(along,fixed);
            p+=(a*(1-blend)+b*blend-measured)*(t*t*(3-2*t));
        }
        return p;
    };
    const auto makeVertex = [&](Vec3d p, Vec3d tangent, Vec3d north) {
        Vertex v;
        v.position = p-mesh.anchorBodyMeters;
        v.normal = Vec3d::Cross(tangent,north).Normalized(p.Normalized());
        if (Vec3d::Dot(v.normal,p) < 0) v.normal = -v.normal;
        v.tangent = tangent.Normalized(chart.east);
        const auto radial = p.Normalized();
        const double lon = std::atan2(radial.y,radial.x);
        const double lat = std::asin(Clamp(radial.z,-1.0,1.0));
        v.uv0 = {(lon+Pi)/(2*Pi),(Pi/2-lat)/Pi};
        // A chart stays fixed while tiles/LODs follow the ship. Keep near texture
        // arithmetic in meters near zero, not million-meter longitude floats.
        chart.Project(radial,v.uv1.u,v.uv1.v);
        const auto photoDelta=p-photoOrigin;
        v.uv2={0,Vec3d::Dot(photoDelta,photoChart.radial)};
        v.uv3={Vec3d::Dot(photoDelta,photoChart.east),Vec3d::Dot(photoDelta,photoChart.north)};
        return v;
    };
    // Position stitching alone left up to22.5deg shading jumps on the real
    // Apollo DTM: fine edge posts retained gradients absent from the coarse
    // edge's interpolated normals. Cache source normals at coarse endpoints,
    // and use the same smooth strip as position morphing. DEM/collision remain
    // unchanged and interior posts retain their measured10m gradient baseline.
    std::map<std::pair<double,double>,Vec3d> edgeNormals;
    const auto normalAt = [&](double x,double y) {
        const auto key=std::make_pair(x,y);
        const auto found=edgeNormals.find(key);
        if(found!=edgeNormals.end()) return found->second;
        const auto normal=Vec3d::Cross(position(x+5,y)-position(x-5,y),
            position(x,y+5)-position(x,y-5)).Normalized(chart.Direction(x,y));
        edgeNormals.emplace(key,normal);
        return normal;
    };
    const auto morphNormal = [&](double x,double y,Vec3d normal) {
        for(unsigned edge=0;edge<4;++edge)
        {
            const double coarse=patch.edgeStep[edge];
            if(coarse<=patch.step) continue;
            const double distances[4]={x-patch.rect.x0,patch.rect.x1-x,y-patch.rect.y0,patch.rect.y1-y};
            const double t=Clamp(1-distances[edge]/(2*coarse),0,1);
            if(t<=0) continue;
            const bool vertical=edge<2;
            const double along=vertical?y:x;
            const double lo=std::floor(along/coarse)*coarse,blend=(along-lo)/coarse;
            const double fixed=edge==0?patch.rect.x0:(edge==1?patch.rect.x1:(edge==2?patch.rect.y0:patch.rect.y1));
            const auto a=vertical?normalAt(fixed,lo):normalAt(lo,fixed);
            const auto b=vertical?normalAt(fixed,lo+coarse):normalAt(lo+coarse,fixed);
            const auto measured=vertical?normalAt(fixed,along):normalAt(along,fixed);
            normal+=((a*(1-blend)+b*blend).Normalized()-measured)*(t*t*(3-2*t));
        }
        return normal.Normalized(chart.Direction(x,y));
    };
    mesh.vertices.reserve(static_cast<std::size_t>((nx+1)*(ny+1)+8*(nx+ny)+48));
    for (int y=0; y<=ny; ++y) for (int x=0; x<=nx; ++x)
    {
        if(x==0&&Cancelled(cancel)) return mesh;
        const double px=patch.rect.x0+x*patch.step,py=patch.rect.y0+y*patch.step;
        // Fixed 10m normal baseline (5m either side), independent of render LOD.
        // A 160m mesh must not average the measured mountain shading over 320m.
        const auto east=patch.step==5.0?at(x+1,y)-at(x-1,y):position(px+5,py)-position(px-5,py);
        const auto north=patch.step==5.0?at(x,y+1)-at(x,y-1):position(px,py+5)-position(px,py-5);
        auto vertex=makeVertex(morphPosition(px,py,at(x,y)),east,north);
        vertex.normal=morphNormal(px,py,vertex.normal);
        vertex.tangent=(vertex.tangent-vertex.normal*Vec3d::Dot(vertex.tangent,vertex.normal)).Normalized(chart.east);
        mesh.vertices.push_back(vertex);
    }
    mesh.indices.reserve(static_cast<std::size_t>((nx*ny+2*(nx+ny))*6));

    const auto triangle = [&](std::int32_t a, std::int32_t b, std::int32_t c) {
        std::int32_t ids[3] = {a,b,c};
        Vertex v[3] = {mesh.vertices[static_cast<std::size_t>(a)],mesh.vertices[static_cast<std::size_t>(b)],mesh.vertices[static_cast<std::size_t>(c)]};
        bool polar[3];
        int pivot = 0;
        for (int i=0; i<3; ++i)
        {
            polar[i] = std::abs((v[i].position+mesh.anchorBodyMeters).Normalized().z) > 1-1e-14;
            if (!polar[i]) pivot = i;
        }
        const double referenceU = v[pivot].uv0.u;
        for (int i=0; i<3; ++i) if (!polar[i]) v[i].uv0.u += std::round(referenceU-v[i].uv0.u);
        for (int i=0; i<3; ++i) if (polar[i]) v[i].uv0.u = (v[(i+1)%3].uv0.u+v[(i+2)%3].uv0.u)*0.5;
        for (int i=0; i<3; ++i)
        {
            if (std::abs(v[i].uv0.u-mesh.vertices[static_cast<std::size_t>(ids[i])].uv0.u) > 1e-12)
            {
                ids[i] = static_cast<std::int32_t>(mesh.vertices.size());
                mesh.vertices.push_back(v[i]);
            }
        }
        // The adapter reflects Y, while UE front faces use clockwise winding
        // (cross(edge1,edge2).normal < 0; see UE GenerateBoxMesh's +Z face).
        // Reverse the RH inward construction once, including skirts/pole fans.
        // Otherwise the visible ground is culled and its buried skirts show.
        mesh.indices.insert(mesh.indices.end(),{ids[0],ids[2],ids[1]});
    };

    // Insert an exact pole when it lies inside a cell; latitude/longitude UVs then
    // form an ordinary pole fan instead of stretching across half the Moon.
    double poleX = 0, poleY = 0;
    const Vec3d pole{0,0,chart.radial.z >= 0 ? 1.0 : -1.0};
    bool hasPole = chart.Project(pole,poleX,poleY) && patch.rect.Contains(poleX,poleY);
    const double gridPoleX = (poleX-patch.rect.x0)/patch.step, gridPoleY = (poleY-patch.rect.y0)/patch.step;
    // Grid-edge poles already have a vertex when both coordinates are integral.
    hasPole = hasPole && (std::abs(gridPoleX-std::round(gridPoleX)) > 1e-7 || std::abs(gridPoleY-std::round(gridPoleY)) > 1e-7);
    for (int y=0; y<ny; ++y)
    {
        if (Cancelled(cancel)) return mesh;
        for (int x=0; x<nx; ++x)
        {
            const std::int32_t a=y*(nx+1)+x,b=a+1,c=a+nx+1,d=c+1;
            if (hasPole && gridPoleX >= x-1e-7 && gridPoleX <= x+1+1e-7 && gridPoleY >= y-1e-7 && gridPoleY <= y+1+1e-7)
            {
                const auto p = position(poleX,poleY);
                const auto index = static_cast<std::int32_t>(mesh.vertices.size());
                mesh.vertices.push_back(makeVertex(p,chart.east,chart.north));
                if (gridPoleY > y+1e-7) triangle(a,index,b);
                if (gridPoleX < x+1-1e-7) triangle(b,index,d);
                if (gridPoleY < y+1-1e-7) triangle(d,index,c);
                if (gridPoleX > x+1e-7) triangle(c,index,a);
            }
            else { triangle(a,c,b); triangle(b,c,d); }
        }
    }
    mesh.surfaceIndexCount = mesh.indices.size();
    // Shared LOD boundaries are connected by edge morphing, not visible buried
    // walls. Keep skirts only at the external plan perimeter behind the globe.
    const double depth = Clamp(patch.step*8,80.0,30000.0);
    const auto skirt = [&](std::int32_t a, std::int32_t b) {
        Vertex va = mesh.vertices[static_cast<std::size_t>(a)], vb = mesh.vertices[static_cast<std::size_t>(b)];
        va.position = va.position-(va.position+mesh.anchorBodyMeters).Normalized()*depth;
        vb.position = vb.position-(vb.position+mesh.anchorBodyMeters).Normalized()*depth;
        const auto ia = static_cast<std::int32_t>(mesh.vertices.size()); mesh.vertices.push_back(va);
        const auto ib = static_cast<std::int32_t>(mesh.vertices.size()); mesh.vertices.push_back(vb);
        triangle(a,b,ia); triangle(b,ib,ia);
    };
    for (int x=0; x<nx; ++x)
    {
        if(patch.skirtEdges&4) skirt(x,x+1);
        if(patch.skirtEdges&8) skirt(ny*(nx+1)+x+1,ny*(nx+1)+x);
    }
    for (int y=0; y<ny; ++y)
    {
        if(patch.skirtEdges&1) skirt((y+1)*(nx+1),y*(nx+1));
        if(patch.skirtEdges&2) skirt(y*(nx+1)+nx,(y+1)*(nx+1)+nx);
    }
    mesh.complete = samplesValid && !Cancelled(cancel);
    for (const auto& v : mesh.vertices) {
        if (!v.position.IsFinite() || !v.normal.IsFinite() || !v.tangent.IsFinite() ||
            !std::isfinite(v.uv0.u) || !std::isfinite(v.uv0.v) ||
            !std::isfinite(v.uv1.u) || !std::isfinite(v.uv1.v) ||
            !std::isfinite(v.uv2.u) || !std::isfinite(v.uv2.v) ||
            !std::isfinite(v.uv3.u) || !std::isfinite(v.uv3.v)) mesh.complete = false;
    }
    // A missing normal, seam or pole sample is just as incomplete as a missing
    // primary post; normalization fallbacks must not disguise unavailable data.
    if (!mesh.complete) { mesh.vertices.clear(); mesh.indices.clear(); mesh.surfaceIndexCount = 0; }
    return mesh;
}
void AppendReconstructedClasts(Mesh& mesh, const Chart& chart, const Patch& patch, const std::atomic_bool* cancel, bool dense, bool companions)
{
    if (!mesh.complete || mesh.reconstructedClastCount || patch.level != 0 || patch.step != 5.0) return;
    // This is an art reconstruction, not recovered Apollo17 geology. The sparse
    // centimeter/decimeter clasts fill a scale band neither the 5m DEM nor the
    // 38mm ALSCC soil tile observes. Positions stay in a FIXED lunar site chart.
    const auto site=Chart::At(ApolloSiteDirection(),chart.radius);
    // Enumerate only site-grid blocks intersecting this patch. The field itself
    // is fixed, extending 4km around the landing site, not centered on the eye.
    constexpr double fieldRadius=4096.0, blockMeters=24.0;
    double minX=fieldRadius,minY=fieldRadius,maxX=-fieldRadius,maxY=-fieldRadius;
    for(double py:{patch.rect.y0,patch.rect.y1}) for(double px:{patch.rect.x0,patch.rect.x1})
    {
        double sx=0,sy=0;
        if(!site.Project(chart.Direction(px,py),sx,sy)) return;
        minX=std::min(minX,sx); maxX=std::max(maxX,sx);
        minY=std::min(minY,sy); maxY=std::max(maxY,sy);
    }
    minX=std::max(minX,-fieldRadius); maxX=std::min(maxX,fieldRadius);
    minY=std::max(minY,-fieldRadius); maxY=std::min(maxY,fieldRadius);
    if(minX>=maxX || minY>=maxY) return;
    const int nx = static_cast<int>(std::llround((patch.rect.x1-patch.rect.x0)/patch.step));
    const int ny = static_cast<int>(std::llround((patch.rect.y1-patch.rect.y0)/patch.step));
    const auto random = [](std::uint32_t seed) {
        seed ^= seed >> 16; seed *= 0x7feb352du; seed ^= seed >> 15;
        seed *= 0x846ca68bu; seed ^= seed >> 16;
        return static_cast<double>(seed & 0xffffffu)/16777216.0;
    };
    const auto cellSeed=[](int cx,int cy) {
        return static_cast<std::uint32_t>(cx+24)*92837111u ^ static_cast<std::uint32_t>(cy+24)*689287499u ^ 1701972u;
    };
    for(int by=static_cast<int>(std::floor(minY/blockMeters));by<=static_cast<int>(std::floor(maxY/blockMeters));++by)
    for(int bx=static_cast<int>(std::floor(minX/blockMeters));bx<=static_cast<int>(std::floor(maxX/blockMeters));++bx)
    {
        if (Cancelled(cancel)) { mesh.complete = false; return; }
        // Fixed quota per SITE block, never a patch-relative cutoff. Two lowest
        // priorities preserve original 4m cell coordinates/hash while allowing
        // the field to continue through every fine tile within upload budgets.
        std::array<int,36> chosen{}; int chosenCount=dense?0:2; double priority[2]={2,2};
        for(int k=0;k<36;++k)
        {
            const double p=random(cellSeed(bx*6+k%6,by*6+k/6));
            if(dense) { if(p<0.65) chosen[static_cast<std::size_t>(chosenCount++)]=k; }
            else if(p<priority[0]) {priority[1]=priority[0];chosen[1]=chosen[0];priority[0]=p;chosen[0]=k;}
            else if(p<priority[1]) {priority[1]=p;chosen[1]=k;}
        }
        for(int choice=0;choice<chosenCount;++choice)
        for(int variant=0;variant<(dense&&companions?3:1);++variant)
        {
        const int k=chosen[static_cast<std::size_t>(choice)];
        const int cx=bx*6+k%6,cy=by*6+k/6;
        // Primary clasts keep their previous coordinates/hash. Two smaller
        // companions fill the centimeter band without enlarging the photo tile.
        const auto seed=cellSeed(cx,cy)+static_cast<std::uint32_t>(variant)*104729u;
        const double sx = (cx+0.15+0.7*random(seed+1))*4;
        const double sy = (cy+0.15+0.7*random(seed+2))*4;
        const double distance = std::sqrt(sx*sx+sy*sy);
        // Only the remote 3.5-4km field edge fades. Fine/coarse mesh changes are
        // >500m from an EVA camera; most clasts there are subpixel, but the rare
        // largest silhouettes still need root's 4K temporal review.
        if (distance < (variant==0?12.0:5.0) || distance > fieldRadius) continue;
        double x=0,y=0;
        if (!chart.Project(site.Direction(sx,sy),x,y) || x < patch.rect.x0 || x >= patch.rect.x1 ||
            y < patch.rect.y0 || y >= patch.rect.y1) continue;
        const double gx=(x-patch.rect.x0)/patch.step, gy=(y-patch.rect.y0)/patch.step;
        const int ix=static_cast<int>(std::floor(gx)), iy=static_cast<int>(std::floor(gy));
        if(ix<0 || iy<0 || ix>=nx || iy>=ny) continue;
        const double fx=gx-ix,fy=gy-iy;
        const auto& a=mesh.vertices[static_cast<std::size_t>(iy*(nx+1)+ix)];
        const auto& b=mesh.vertices[static_cast<std::size_t>(iy*(nx+1)+ix+1)];
        const auto& c=mesh.vertices[static_cast<std::size_t>((iy+1)*(nx+1)+ix)];
        const auto& d=mesh.vertices[static_cast<std::size_t>((iy+1)*(nx+1)+ix+1)];
        // Seat on the EXACT rendered triangle (not an independent height sample,
        // which can float above a differently oriented 5m triangulation).
        const Vec3d center = fx+fy<=1 ? a.position*(1-fx-fy)+b.position*fx+c.position*fy :
            b.position*(1-fy)+c.position*(1-fx)+d.position*(fx+fy-1);
        Vec3d up=Vec3d::Cross(b.position-a.position,c.position-a.position).Normalized();
        if(fx+fy>1) up=Vec3d::Cross(d.position-b.position,c.position-b.position).Normalized();
        if(Vec3d::Dot(up,center+mesh.anchorBodyMeters)<0) up=-up;
        if(Vec3d::Dot(up,(center+mesh.anchorBodyMeters).Normalized())<0.94) continue;
        const Vec3d east=(chart.east-up*Vec3d::Dot(chart.east,up)).Normalized();
        const Vec3d north=Vec3d::Cross(up,east).Normalized();
        const double edge=Clamp((fieldRadius-distance)/512,0,1);
        const double landing=variant==0?Clamp((distance-12)/10,0,1):Clamp((distance-5)/5,0,1);
        const double radius=(variant==0?(0.055+0.24*std::pow(random(seed+3),3)):
            (0.010+0.025*std::pow(random(seed+3),2)))*edge*landing*landing*(3-2*landing);
        if(radius<0.008) continue;
        const double height=radius*(0.55+0.55*random(seed+4));
        const double rotation=2*Pi*random(seed+5), aspect=0.65+0.5*random(seed+6);
        // Eight/twelve-sided chipped shoulders replace the five-sided extrusion. Tiny
        // companions retain a cheaper silhouette; no density or radius increase.
        const int sides=variant==0?(radius>.13?12:8):4;
        const int rings=variant==0?3:2;
        const int vertexCount=sides*rings+1+(variant==0?sides:0);
        const auto first=static_cast<std::int32_t>(mesh.vertices.size());
        if(mesh.vertices.size()+static_cast<std::size_t>(vertexCount)>9800) break;
        ClastRange range;
        range.firstVertex=mesh.vertices.size(); range.firstIndex=mesh.indices.size();
        range.center=center; range.up=up; range.radius=radius; range.height=height;
        range.siteMeters={sx,sy}; range.primary=variant==0;
        const double shiftX=radius*(random(seed+70)-0.5)*0.18;
        const double shiftY=radius*(random(seed+71)-0.5)*0.18;
        const auto vertex=[&](double lx,double ly,double z) {
            Vertex v;
            v.position=center+east*lx+north*ly+up*z;
            v.normal={0,0,0}; // Area-weighted actual triangle normals below.
            const auto radial=(v.position+mesh.anchorBodyMeters).Normalized();
            v.uv0={(std::atan2(radial.y,radial.x)+Pi)/(2*Pi),(Pi/2-std::asin(Clamp(radial.z,-1,1)))/Pi};
            // Preserve the existing chart scale; use UV2 to suppress soil detail
            // on exposed rock. UV3 is stable across camera / tile / density LOD.
            v.uv1={x+lx,y+ly+z};
            v.uv2={1,z}; v.uv3={random(seed+99),Clamp(radius/0.295,0,1)};
            mesh.vertices.push_back(v);
        };
        for(int ring=0;ring<rings;++ring) for(int i=0;i<sides;++i)
        {
            const double angle=rotation+2*Pi*(i+0.18*(random(seed+30+static_cast<unsigned>(i))-0.5))/sides;
            const double irregular=0.78+0.35*random(seed+20+static_cast<unsigned>(i));
            // Independent height chips and an offset upper crown break the
            // identical vertical pentagon edges and centered pointed cap.
            const double crown=ring==2?0.45+0.20*random(seed+40+static_cast<unsigned>(i)):1.0;
            const double r=radius*irregular*(ring==0?0.72:crown);
            const double z=height*(ring==0?-0.30:(ring==1?
                0.22+0.22*random(seed+50+static_cast<unsigned>(i)):
                0.83+0.08*std::cos(angle+rotation)+0.03*(random(seed+60+static_cast<unsigned>(i))-.5)));
            vertex(std::cos(angle)*r+(ring==2?shiftX:0),
                   std::sin(angle)*r*aspect+(ring==2?shiftY:0),z);
        }
        vertex(shiftX,shiftY,height);
        // Duplicate the shoulder only, so geometry normals retain a dry broken
        // edge between buried side and upper fracture, without flat triangle soup.
        const auto upperShoulder=first+sides*rings+1;
        if(variant==0) for(int i=0;i<sides;++i)
            mesh.vertices.push_back(mesh.vertices[static_cast<std::size_t>(first+sides+i)]);
        const auto face=[&](std::int32_t ia,std::int32_t ib,std::int32_t ic) {
            auto area=Vec3d::Cross(mesh.vertices[static_cast<std::size_t>(ib)].position-mesh.vertices[static_cast<std::size_t>(ia)].position,
                mesh.vertices[static_cast<std::size_t>(ic)].position-mesh.vertices[static_cast<std::size_t>(ia)].position);
            const auto centroid=(mesh.vertices[static_cast<std::size_t>(ia)].position+
                mesh.vertices[static_cast<std::size_t>(ib)].position+mesh.vertices[static_cast<std::size_t>(ic)].position)/3;
            // Convex local envelope gives a reliable outward test independent of
            // the normals we are constructing; Y reflection is done by adapter.
            if(Vec3d::Dot(area,centroid-(center+up*(height*0.12)))<0)
            { std::swap(ib,ic); area=-area; }
            mesh.indices.insert(mesh.indices.end(),{ia,ib,ic});
            mesh.vertices[static_cast<std::size_t>(ia)].normal+=area;
            mesh.vertices[static_cast<std::size_t>(ib)].normal+=area;
            mesh.vertices[static_cast<std::size_t>(ic)].normal+=area;
        };
        for(int i=0;i<sides;++i)
        {
            const int j=(i+1)%sides;
            face(first+i,first+j,first+sides+i);
            face(first+j,first+sides+j,first+sides+i);
            if(variant==0)
            {
                face(upperShoulder+i,upperShoulder+j,first+2*sides+i);
                face(upperShoulder+j,first+2*sides+j,first+2*sides+i);
            }
            face(first+(rings-1)*sides+i,first+(rings-1)*sides+j,first+rings*sides);
        }
        range.vertexCount=mesh.vertices.size()-range.firstVertex;
        range.indexCount=mesh.indices.size()-range.firstIndex;
        for(std::size_t i=range.firstVertex;i<mesh.vertices.size();++i)
        {
            auto& v=mesh.vertices[i]; v.normal=v.normal.Normalized(up);
            const auto along=(std::abs(Vec3d::Dot(east,v.normal))>.97?north:east);
            v.tangent=(along-v.normal*Vec3d::Dot(along,v.normal)).Normalized();
        }
        mesh.clasts.push_back(range);
        ++mesh.reconstructedClastCount;
        }
    }
}
Mesh BuildClastPatch(const Chart& chart, const Patch& patch, const HeightSampler& sample, const std::atomic_bool* cancel)
{
    // Ground is a temporary seating reference, never uploaded a second time.
    auto groundPatch=patch;
    groundPatch.level=0; // Clast level selects density, not the measured seating grid.
    auto ground=BuildPatch(chart,groundPatch,sample,cancel);
    if(!ground.complete) return ground;
    const auto groundVertices=ground.vertices.size(),groundIndices=ground.indices.size();
    AppendReconstructedClasts(ground,chart,groundPatch,cancel,true,patch.level==0);
    Mesh clasts;
    clasts.anchorBodyMeters=ground.anchorBodyMeters;
    clasts.sourceSamples=ground.sourceSamples;
    clasts.reconstructedClastCount=ground.reconstructedClastCount;
    clasts.clasts=std::move(ground.clasts);
    for(auto& c:clasts.clasts) { c.firstVertex-=groundVertices; c.firstIndex-=groundIndices; }
    clasts.complete=ground.complete;
    if(!clasts.complete) return clasts;
    clasts.vertices.assign(ground.vertices.begin()+static_cast<std::ptrdiff_t>(groundVertices),ground.vertices.end());
    clasts.indices.reserve(ground.indices.size()-groundIndices);
    for(auto i=groundIndices;i<ground.indices.size();++i)
        clasts.indices.push_back(ground.indices[i]-static_cast<std::int32_t>(groundVertices));
    clasts.surfaceIndexCount=clasts.indices.size();
    return clasts;
}
} // namespace star::terrain
