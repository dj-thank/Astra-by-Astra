#include "StarAudioRouting.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace star::audio::routing;
static void Check(bool b,const char* m){if(!b){std::cerr<<"FAIL "<<m<<'\n';std::exit(1);}}
static void Run(State& s,double seconds){for(int i=0;i<static_cast<int>(seconds*120);++i)s.Step(1./120);}
static double Beds(const Mix& m){double x=m.whineGain;for(std::size_t i=0;i<LayerCount;++i)x+=m.assetLayers[i]+m.fallbackLayers[i];return x;}
int main(){
 State s;Input in;in.masterVolume=1;in.throttle=1;in.engineOutputIsSpooled=true;s.SetInput(in);Run(s,3);double interior=Beds(s.Output());
 in.cockpit=false;s.SetInput(in);Run(s,3);Check(std::abs(Beds(s.Output())-interior)<1e-4,"external camera preserves interior monitor");
 in.interiorMonitor=false;s.SetInput(in);Check(s.TryEvent(Cue::SpoolUp,1)<0,"opt-out rejects new events before fade finishes");Run(s,6);Check(Beds(s.Output())<1e-12,"opt-out exterior has no vacuum machinery");Check(s.TryEvent(Cue::SpoolUp,1)<0,"vacuum rejects machinery events");
 in.eva=true;s.SetInput(in);Run(s,3);Check(s.Output().fallbackLayers[7]>0.06,"EVA internal fan fallback");Check(s.Output().fallbackLayers[8]>0.03,"EVA cooling fallback");Check(s.Output().whineGain<1e-12,"no ship engine in suit");
 Check(s.TryEvent(Cue::Footstep1,.5)>=0,"conducted footstep allowed in EVA");Check(s.TryEvent(Cue::SpoolUp,1)<0,"EVA rejects engine transitions");Check(s.TryEvent(Cue::RCS,1)<0,"EVA rejects ship RCS");
 for(auto cue:{Cue::SuitFan,Cue::SuitCooling}) { s.SetAvailable(cue,true); }
 Run(s,.5);Check(s.Output().fallbackLayers[7]==0&&s.Output().assetLayers[7]>0,"late fan load replaces only fallback");
 in.paused=true;s.SetInput(in);Check(Beds(s.Output())==0,"pause immediately clears output");Check(s.TryEvent(Cue::ExitHatch,2)<0,"paused hatch discarded");
 in.paused=false;in.eva=false;in.interiorMonitor=true;s.SetInput(in);Run(s,4);Check(s.TryEvent(Cue::Footstep1,.5)<0,"walking disabled outside EVA");Check(s.Output().assetLayers[7]<1e-12,"no suit fan after EVA");Check(s.TryEvent(Cue::ThrusterPulse,.3)<0&&s.TryEvent(Cue::SuitValve,.7)<0,"suit-only actions disabled outside EVA");
 in.masterVolume=0;s.SetInput(in);Check(s.TryEvent(Cue::EnterHatch,2)<0&&Beds(s.Output())==0,"mute clears beds and hatch events");
 Check(EventCue("ExitHatch")==Cue::ExitHatch&&EventCue("EnterHatch")==Cue::EnterHatch&&EventCue("Footstep")==Cue::Footstep1,"root EVA event names stable");
 // Exercise all new fallbacks then cancel before resume; none may become a ghost event.
 for(int id=static_cast<int>(Cue::Footstep1);id<static_cast<int>(Cue::Count);++id){
  FallbackDSP dsp;std::array<double,LayerCount> empty{};std::vector<float> x(48000*2);
  dsp.SetMix(empty,0,1,false);dsp.Trigger(0,static_cast<Cue>(id));dsp.Render(x.data(),4800);
  Check(std::any_of(x.begin(),x.begin()+9600,[](float v){return std::abs(v)>.001f;}),"new cue has bounded nonempty fallback");
  dsp.SetMix(empty,0,0,true);dsp.SetMix(empty,0,1,false);dsp.Render(x.data(),48000);
  Check(std::all_of(x.begin()+4800,x.end(),[](float v){return v==0;}),"pause-resume before callback does not resurrect event");
 }
 // All output combinations retain correlated peak headroom, including mode transitions.
 in={};in.masterVolume=1;in.throttle=1;in.charge=1;in.braking=1;in.cruise=true;
 for(int frame=0;frame<2400;++frame){in.eva=(frame/91)%2==1;s.SetInput(in);s.Step(1./120);const auto&m=s.Output();Check(Beds(m)+m.musicGain+EventPeakReservation()*m.eventMaster<=.820001,"adaptive gain reserves worst-case peak budget");}
 // Reference master .5 remains below the budget, so unrelated sound gains stay unchanged.
 State levels; Input ref; ref.masterVolume=.5;ref.throttle=1;ref.engineOutputIsSpooled=true;
 levels.SetInput(ref);Run(levels,4);const auto ship=levels.Output();
 Check(std::abs(ship.fallbackLayers[0]-.0225)<1e-9&&std::abs(ship.musicGain-.0325)<1e-9,"cabin and music are unchanged at reference master");
 Check(std::abs(ship.fallbackLayers[6]-.09)<1e-9,"full main engine bed has +6dB presence");
 Check(std::abs(ship.whineGain-.078)<1e-9,"main turbine has +6dB presence");
 ref.eva=true;levels.SetInput(ref);Run(levels,5);const auto eva=levels.Output();
 const double nineDb=std::pow(10.,9./20.);
 Check(std::abs(eva.fallbackLayers[7]-.0325*nineDb)<1e-8&&std::abs(eva.fallbackLayers[8]-.0175*nineDb)<1e-8,"suit beds have +9dB presence");
 Check(std::abs(Spec(Cue::Footstep1).eventGain-.12*nineDb)<1e-7,"boot contact has +9dB presence");
 Check(std::abs(Spec(Cue::ExitHatch).eventGain-.12)<1e-7&&std::abs(Spec(Cue::RCS).eventGain-.08)<1e-7,"hatch and RCS gains unchanged");
 Check(levels.TryEvent(Cue::Footstep1,.36)==0&&levels.TryEvent(Cue::Footstep2,.36)<0,"louder boot variants cannot overlap");
 Check(levels.TryEvent(Cue::Collision,1)==1&&levels.TryEvent(Cue::Landing,1)==2&&levels.TryEvent(Cue::ExitHatch,1)==3,"four loudest independent event groups fit");
 const double simultaneous=Spec(Cue::Footstep1).eventGain+Spec(Cue::Collision).eventGain+Spec(Cue::Landing).eventGain+Spec(Cue::ExitHatch).eventGain;
 Check(std::abs(EventPeakReservation()-simultaneous)<1e-7,"peak reservation comes from actual independent cue gains");
 Check(levels.TryEvent(Cue::Scan,1)<0,"fifth event still capped after gain adjustment");
 ref.paused=true;levels.SetInput(ref);Check(Beds(levels.Output())==0&&levels.TryEvent(Cue::Footstep1,.36)<0,"boosted events discarded on pause");
 State loud;Input all;all.masterVolume=1;all.eva=true;loud.SetInput(all);Run(loud,4);
 const auto loudMix=loud.Output();FallbackDSP boosted;std::vector<float> output(48000*2);
 boosted.SetMix(loudMix.fallbackLayers,0,loudMix.eventMaster,false);
 for(std::size_t slot=0;slot<4;++slot){const Cue cues[]{Cue::Footstep1,Cue::Collision,Cue::Landing,Cue::ExitHatch};boosted.Trigger(slot,cues[slot]);}
 boosted.Render(output.data(),48000);
 double actualPeak=0;for(float sample:output){Check(std::isfinite(sample)&&std::abs(sample)<.82,"boosted production fallback remains finite with four event groups");actualPeak=std::max(actualPeak,std::abs(static_cast<double>(sample)));}
 Check(actualPeak>.02,"boosted production fallback is nonempty");
 boosted.SetMix(loudMix.fallbackLayers,0,0,true);boosted.Render(output.data(),48000);
 Check(std::all_of(output.begin()+4800,output.end(),[](float v){return v==0;}),"boosted production fallback pause settles to exact silence");
 std::cout<<"boosted_EVA_native_peak="<<actualPeak<<'\n';
 std::cout<<"presence main=2 EVA="<<nineDb<<" event_reservation="<<EventPeakReservation()<<'\n';
 std::cout<<"PASS listener, EVA, 16 fallbacks, late load, pause/resume and adaptive peak budget\n";
}

