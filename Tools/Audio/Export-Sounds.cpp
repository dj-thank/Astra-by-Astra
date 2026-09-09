// Original STAR sound design. MIT License; no recordings or external services.
#include "StarAudioRouting.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
namespace {
void u16(std::ofstream& f, std::uint16_t v) { char b[]{char(v&255),char(v>>8)}; f.write(b,2); }
void u32(std::ofstream& f, std::uint32_t v) { u16(f,std::uint16_t(v));u16(f,std::uint16_t(v>>16)); }
void wav(const std::filesystem::path& p,const std::vector<float>& a) {
 std::ofstream f(p,std::ios::binary); if(!f)throw std::runtime_error("Cannot write audio");
 const auto n=std::uint32_t(a.size()*2);f.write("RIFF",4);u32(f,n+36);f.write("WAVEfmt ",8);
 u32(f,16);u16(f,1);u16(f,2);u32(f,48000);u32(f,192000);u16(f,4);u16(f,16);f.write("data",4);u32(f,n);
 for(float s:a){if(!std::isfinite(s)||std::abs(s)>0.9f)throw std::runtime_error("Invalid audio peak");u16(f,std::uint16_t(std::int16_t(std::lround(s*32767))));}
 if(!f)throw std::runtime_error("Audio write failed");
}
}
int main(int argc,char** argv){
 try{
  using namespace star::audio::routing;
  if(argc!=2)return 2;const auto out=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(out);
  for(std::size_t i=0;i<CueCount;++i){
   const auto cue=static_cast<Cue>(i);const auto& spec=Spec(cue);
   if(std::string(spec.id).rfind("BGM_",0)==0)continue;
   const bool loop=i<LayerCount;const double seconds=loop?12.0:spec.fallbackSeconds+0.1;
   const auto frames=std::size_t(seconds*48000);std::vector<float> samples(frames*2);
   FallbackDSP dsp(48000);std::array<double,LayerCount> layers{};if(loop)layers[i]=0.65;
   dsp.SetMix(layers,0.55,1.0,false);
   // Let the event-master ramp settle before triggering short clicks.
   std::vector<float> warm(4800*2);dsp.Render(warm.data(),4800);if(!loop)dsp.Trigger(0,cue);
   dsp.Render(samples.data(),frames);
   double peak=0;for(float s:samples)peak=std::max(peak,std::abs(double(s)));
   if(peak<1e-6)throw std::runtime_error("Silent cue");
   // Runtime already applies event gain; normalize reusable one-shots conservatively.
   const double scale=loop?1.0:0.60/peak;
   for(std::size_t n=0;n<frames;++n){const double fade=std::min({1.0,double(n)/240.0,double(frames-1-n)/240.0});for(int c=0;c<2;++c)samples[n*2+c]=float(samples[n*2+c]*scale*fade);}
   wav(out/(std::string(spec.id)+".wav"),samples);std::cout<<spec.id<<"\n";
  }
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
