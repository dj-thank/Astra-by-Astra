#pragma once
#include "Simulation/FlightSimulation.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace star;

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
    RealData(const std::filesystem::path& directory,const std::filesystem::path& meta)
    {
        std::ifstream input(meta); input>>width>>height>>scale>>radius>>rw>>rh>>west>>east>>north>>south;
        Require(width>0&&height>0&&rw>0&&rh>0&&!input.fail(),"metadata parsed");
        globe=Read<std::int16_t>(directory/"moon_ldem_16_i16.bin",static_cast<std::size_t>(width)*height);
        region=Read<float>(directory/"apollo17_height_f32.bin",static_cast<std::size_t>(rw)*rh);
        mask=Read<unsigned char>(directory/"apollo17_valid_u8.bin",static_cast<std::size_t>(rw)*rh);
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
        const double global=Global(lat,lon);
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
};
}
