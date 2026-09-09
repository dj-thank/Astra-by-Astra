#include "StarAudioRouting.h"
#include <fstream>
#include <string>
#include <vector>
using namespace star::audio::routing;
int main(int argc,char**argv){if(argc!=2)return 1;const std::string prefix=argv[1];
 std::ofstream raw(prefix+".f32",std::ios::binary),csv(prefix+".csv");
 csv<<"time,spool,charge,cruise,brake,pitch,whine,event_master";for(std::size_t i=0;i<LayerCount;++i)csv<<','<<Spec(static_cast<Cue>(i)).id;csv<<'\n';
 State s;Input in;in.masterVolume=.7;in.cockpit=false;in.engineOutputIsSpooled=true;FallbackDSP dsp;std::vector<float> block(800);
 for(std::size_t i=0;i<7;++i)s.SetAvailable(static_cast<Cue>(i),true);
 for(int f=0;f<32*120;++f){double t=f/120.;in.throttle=t<3?0:t<6?.25:t<9?.5:t<15?1:t<23?.65:0;in.charge=t>=12&&t<15?(t-12)/3:0;in.cruise=t>=15&&t<23;in.braking=t>=23&&t<27?1:0;in.paused=t>=29&&t<30;
 s.SetInput(in);s.Step(1./120);const auto&m=s.Output();dsp.SetMix(m.fallbackLayers,m.throttle,m.eventMaster,in.paused);dsp.SetEngine(m.whineGain,m.charge,m.cruise,m.braking);dsp.Render(block.data(),400);
 raw.write(reinterpret_cast<const char*>(block.data()),static_cast<std::streamsize>(block.size()*sizeof(float)));csv<<t<<','<<m.throttle<<','<<m.charge<<','<<m.cruise<<','<<m.braking<<','<<m.enginePitch<<','<<m.whineGain<<','<<m.eventMaster;for(double g:m.assetLayers)csv<<','<<g;csv<<'\n';
 }return raw&&csv?0:2;
}
