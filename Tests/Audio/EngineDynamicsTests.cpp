#include "StarAudioRouting.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>
using namespace star::audio::routing;
static void Check(bool ok, const char* name) { if (!ok) { std::cerr << "FAIL " << name << '\n'; std::exit(1); } }
static void Run(State& state, double seconds) { for(int i=0;i<static_cast<int>(seconds*120);++i) state.Step(1.0/120); }
int main(int argc, char** argv)
{
    State state; Input in; in.masterVolume=1; state.SetInput(in); Run(state,2);
    double previous=state.Output().whineGain;
    for(double level:{0.25,0.5,1.0}) {
        in.throttle=level; state.SetInput(in); Run(state,4);
        Check(state.Output().whineGain>previous,"power ladder raises whine gain"); previous=state.Output().whineGain;
        Check(std::abs(state.Output().throttle-level)<0.001,"spool reaches demand");
    }
    in.speedMps=50*299792458.0; in.throttle=0; state.SetInput(in); state.Step(1.0/120);
    Check(state.Output().throttle>0.95,"release retains physical spool continuity"); Run(state,6);
    Check(state.Output().throttle<0.001,"high speed alone cannot sustain powered whine");
    State slow,fast; in.throttle=1; slow.SetInput(in);fast.SetInput(in);
    for(int i=0;i<60;++i) slow.Step(1.0/30);
    for(int i=0;i<480;++i) fast.Step(1.0/240);
    Check(std::abs(slow.Output().throttle-fast.Output().throttle)<1e-12,"spool frame-rate independence");
    State supplied; in.engineOutputIsSpooled=true; in.throttle=1; in.charge=1; in.braking=1; in.cruise=true;
    supplied.SetInput(in);Run(supplied,0.2);
    Check(supplied.Output().throttle>0.995,"already-spooled simulation telemetry is not spooled twice");
    Run(supplied,4); const auto& full=supplied.Output();
    double budget=full.whineGain+full.musicGain+EventPeakReservation()*full.eventMaster;
    for(std::size_t i=0;i<LayerCount;++i)budget+=full.assetLayers[i]+full.fallbackLayers[i];
    Check(budget<1.0,"correlated unity peak inputs retain combined engine/music/four-event headroom");
    for(double rate:{8000.,44100.,48000.,96000.,192000.}) {
        FallbackDSP dsp(rate); std::array<double,LayerCount> empty{};
        dsp.SetMix(empty,1,1,false); dsp.SetEngine(0.08,1,1,1);
        std::vector<float> samples(static_cast<std::size_t>(rate)*2);
        dsp.Render(samples.data(),samples.size()/2);
        double peak=0,energy=0,jump=0;
        for(std::size_t i=2;i<samples.size();i+=2) { peak=std::max(peak,std::abs(static_cast<double>(samples[i]))); energy+=samples[i]*samples[i]; jump=std::max(jump,std::abs(static_cast<double>(samples[i]-samples[i-2]))); }
        Check(energy>0.1,"complete asset bank retains audible continuous turbine");
        Check(peak<0.09 && jump<0.16,"finite bounded whine including low sample rates");
        dsp.SetMix(empty,0,0,true); dsp.SetEngine(0.08,1,1,1); dsp.Render(samples.data(),samples.size()/2);
        Check(std::all_of(samples.begin()+static_cast<std::ptrdiff_t>(rate*0.04)*2,samples.end(),[](float x){return x==0;}),"pause overrides engine gain and reaches exact silence");
        dsp.SetMix(empty,0,0,false); dsp.SetEngine(std::numeric_limits<double>::quiet_NaN(),0,0,0); dsp.Render(samples.data(),samples.size()/2);
        Check(std::all_of(samples.begin(),samples.end(),[](float x){return x==0;}),"invalid engine gain fails silent");
        dsp.SetMix(empty,0,1,false);dsp.Trigger(0,Cue::SpoolUp);dsp.Render(samples.data(),1000);
        dsp.Cancel(0);dsp.Render(samples.data(),samples.size()/2);
        Check(std::all_of(samples.begin()+static_cast<std::ptrdiff_t>(rate*0.04)*2,samples.end(),[](float x){return x==0;}),"opposite engine event cancellation fades to silence within 40ms");
        std::cout<<"rate="<<rate<<" peak="<<peak<<" max_sample_delta="<<jump<<'\n';
    }
    Check(EventCue("SpoolUp")==Cue::SpoolUp && EventCue("CruiseCharge")==Cue::CruiseCharge,"new transient mapping");
    // Deterministic production-DSP export with exact per-block routing for an asset-backed audition.
    if(argc==2) {
        const std::string prefix=argv[1];
        std::ofstream raw(prefix+".f32",std::ios::binary), csv(prefix+".csv");
        csv<<"time,demand,spool,charge,cruise,brake,pitch,whine";
        for(std::size_t i=0;i<LayerCount;++i)csv<<','<<Spec(static_cast<Cue>(i)).id;
        csv<<'\n'; State route; FallbackDSP dsp; Input input; input.masterVolume=1;
        for(std::size_t i=0;i<LayerCount;++i)route.SetAvailable(static_cast<Cue>(i),true);
        std::vector<float> block(800);
        for(int frame=0;frame<30*120;++frame) {
            const double t=frame/120.0;
            input.throttle=t<3?0:t<6?0.25:t<9?0.5:t<15?1:t<23?0.65:0;
            input.charge=t>=12&&t<15?(t-12)/3:0;input.cruise=t>=15&&t<23;
            input.braking=t>=23&&t<27?1:0;input.paused=t>=29;
            route.SetInput(input);route.Step(1.0/120);const Mix& mix=route.Output();
            dsp.SetMix(mix.fallbackLayers,mix.throttle,mix.eventMaster,input.paused);dsp.SetEngine(mix.whineGain,mix.charge,mix.cruise,mix.braking);dsp.Render(block.data(),400);
            raw.write(reinterpret_cast<const char*>(block.data()),static_cast<std::streamsize>(block.size()*sizeof(float)));
            csv<<t<<','<<input.throttle<<','<<mix.throttle<<','<<mix.charge<<','<<mix.cruise<<','<<mix.braking<<','<<mix.enginePitch<<','<<mix.whineGain;
            for(double gain:mix.assetLayers)csv<<','<<gain;
            csv<<'\n';
        }
        Check(static_cast<bool>(raw)&&static_cast<bool>(csv),"export files written");
    }
    std::cout<<"PASS engine dynamics; native DSP only, no device/game audition claimed\n";
}
