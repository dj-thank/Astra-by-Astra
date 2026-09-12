#include "SolarVisual.h"
#include "SolarLighting.h"
#include <cassert>
#include <iostream>
#include <limits>
int main(){
 using namespace star::solarvisual;
 assert(Evaluate(.5,1800000000).surface==0);
 assert(Evaluate(1,1800000000).surface==0);
 assert(Evaluate(5,1800000000).surface==1);
 assert(Evaluate(3,1800000000).plasma==0);
 assert(Evaluate(8,1800000000).plasma==1);
 assert(Evaluate(20,1800000000,false).surface==0);
 assert(Evaluate(std::numeric_limits<double>::quiet_NaN(),1).plasma==0);
 for(double angle:{1.,3.,5.,8.}) {
  auto a=Evaluate(angle-1e-6,100),b=Evaluate(angle+1e-6,100);
  assert(std::abs(a.surface-b.surface)<1e-5&&std::abs(a.plasma-b.plasma)<1e-5);
 }
 for(int period:{30,60,90}){
  const auto before=Evaluate(8,3599.99999),after=Evaluate(8,3600.00001);
  assert(std::abs(std::sin(before.seconds*2*star::Pi/period)-std::sin(after.seconds*2*star::Pi/period))<1e-5);
 }
 const auto saved=Evaluate(8,1800000000.125),loaded=Evaluate(8,1800000000.125);
 assert(saved.seconds==loaded.seconds);assert(Evaluate(8,-1).seconds==3599);
 const auto nearSun=star::ObserveSun({8*695700000.,0,0},{0,0,0},695700000.);
 const auto farSun=star::ObserveSun({star::AstronomicalUnitMeters,0,0},{0,0,0},695700000.);
 assert(std::abs(nearSun.diskLuminance/farSun.diskLuminance-1)<1e-12);
 std::cout<<"Solar visual: thresholds, continuous handoffs, clock wrap, reload determinism, finite fallback, unchanged photometric reference PASS\n";
}
