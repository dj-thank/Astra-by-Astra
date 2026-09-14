#include "Core/StarInputCore.h"
#include "UI/StarInputCalibration.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace star::input;
namespace {
int checks=0, failures=0;
void Check(bool ok,const char* name) { ++checks; if(!ok) { ++failures; std::cerr<<"FAIL "<<name<<'\n'; } }
bool Silent(const ControlFrame& f) {
    if(f.Yaw!=0||f.Pitch!=0||f.Roll!=0||f.Throttle!=0||f.LookX!=0||f.LookY!=0)return false;
    for(bool b:f.Buttons)if(b)return false;
    return true;
}
RawState Neutral() {
    RawState r;r.Connected=r.AxisDataReady=true;r.NumAxes=4;r.NumButtons=16;r.NumHats=1;r.Axes[3]=32767;return r;
}
}
int main() {
    Profile good;std::string error;
    Check(ValidateProfile(good,error),"default profile valid");
    const auto raw=Neutral();
    for(int kind=0;kind<7;++kind) {
        auto p=good;
        switch(kind) {
        case 0:p.Axes[0].Deadzone=std::numeric_limits<float>::quiet_NaN();break;
        case 1:p.Axes[3].Exponent=std::numeric_limits<float>::infinity();break;
        case 2:p.Axes[0].Minimum=std::numeric_limits<int>::min();p.Axes[0].Maximum=std::numeric_limits<int>::max();break;
        case 3:p.Axes[3].Deadzone=1;break;
        case 4:p.SafeThrottle=std::numeric_limits<float>::quiet_NaN();break;
        case 5:p.NeutralHoldSeconds=-1;break;
        default:p.Axes[3].Center=p.Axes[3].Minimum;break;
        }
        Check(!ValidateProfile(p,error),"malformed profile rejected");
        Check(Silent(MapControls(raw,p)),"malformed profile maps to silence");
        SafetyInterlock interlock;
        for(int i=0;i<10;++i) Check(Silent(interlock.Update(raw,p,true,true,.1f)),"invalid interlock emits no actions");
        Check(!interlock.IsArmed(),"invalid profile cannot arm");
    }
    for(float dt:{0.0f,-.1f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        SafetyInterlock interlock;
        for(int i=0;i<5;++i) interlock.Update(raw,good,true,true,.1f);
        Check(interlock.IsArmed(),"valid neutral arms");
        auto moved=raw;moved.Axes[0]=32767;moved.Buttons[0]=true;
        Check(Silent(interlock.Update(moved,good,true,true,dt)),"invalid clock silences armed controls");
        Check(!interlock.IsArmed(),"invalid clock resets arming");
        for(int i=0;i<5;++i)interlock.Update(raw,good,true,true,.1f);
        Check(interlock.IsArmed(),"valid clock recovers after invalid frame");
        Check(interlock.Update(moved,good,true,true,.016f).Yaw>.99f,"valid resumed input works");
    }
    AxisBinding a;a.Minimum=std::numeric_limits<int>::min();a.Maximum=std::numeric_limits<int>::max();
    Check(NormalizeAxis(32767,a,true)==0,"direct invalid calibration is bounded before subtraction");
    a=AxisBinding{};a.Deadzone=std::numeric_limits<float>::quiet_NaN();
    Check(NormalizeAxis(12345,a,false)==0,"direct invalid curve emits zero");
    // Exhaustively test every hardware sample for valid axis monotonicity and finite output.
    float previous=-1;
    for(int value=-32768;value<=32767;++value) {
        a=AxisBinding{};
        const float mapped=NormalizeAxis(static_cast<std::int16_t>(value),a,false);
        Check(std::isfinite(mapped)&&mapped>=previous&&mapped>=-1&&mapped<=1,"valid 16-bit axis monotone and bounded");
        previous=mapped;
    }
    Profile parsed;Check(ParseProfile(SerializeProfile(good),parsed,error),"existing format round trip");
    std::cout<<checks<<" input robustness checks; failures="<<failures<<" (pure core; no SDL/device claim)\n";
    return failures?1:0;
}
