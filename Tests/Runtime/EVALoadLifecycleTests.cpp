// Execute the actual extracted cleanup method. Possession/Destroy are recording
// host adapters, not Unreal lifecycle emulation or a packaged-game acceptance.
#include <cstddef>
#include <iostream>
#include <vector>
struct Pawn {
    int destroyed=0;
    std::vector<int>* events=nullptr;
    void Destroy(){++destroyed;if(events)events->push_back(4);}
};
struct ShipAdapter:Pawn {
    bool evaAudio=true;
    void SetEVAAudio(bool value){evaAudio=value;if(events)events->push_back(3);}
};
template<class T>struct TObjectPtr {
    T* value=nullptr;
    T* Get(){return value;}
    TObjectPtr& operator=(T* p){value=p;return *this;}
};
struct AStarPlayerController {
    ShipAdapter* Ship=nullptr;TObjectPtr<Pawn> EVAPawn;
    double EVAFootstepDistance=1.5;
    Pawn* possessed=nullptr;Pawn* view=nullptr;
    int handoffs=0;bool paused=true;
    std::vector<int> events;
    void Possess(Pawn* p){possessed=p;events.push_back(1);}
    void SetViewTarget(Pawn* p){view=p;events.push_back(2);}
    void ClearInputHandoff(){++handoffs;events.push_back(5);}
    void ClearEVAForLoad();
};
#include "eva-load-method.inc"
int main(){
    int checks=0,failures=0;
    auto check=[&](bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<label<<'\n';}};
    AStarPlayerController c;Pawn walker;ShipAdapter ship;
    c.Ship=&ship;c.EVAPawn=&walker;c.possessed=c.view=&walker;
    walker.events=ship.events=&c.events;
    c.ClearEVAForLoad();
    check(c.EVAPawn.Get()==nullptr,"stale walker reference cleared");
    check(c.possessed==&ship&&c.view==&ship,"ship possession and view restored");
    check(walker.destroyed==1,"old actor destroyed once");
    check(!ship.evaAudio&&c.EVAFootstepDistance==0,"EVA presentation reset");
    check(c.handoffs==1&&c.paused,"handoff clears transient input without resuming");
    check(c.events==std::vector<int>({1,2,3,4,5}),"possession and view released before actor destruction");
    c.ClearEVAForLoad();
    check(walker.destroyed==1&&c.EVAPawn.Get()==nullptr,"repeated load cannot destroy stale actor twice");
    Pawn replacement;replacement.events=&c.events;c.EVAPawn=&replacement;c.possessed=&replacement;
    c.ClearEVAForLoad();check(replacement.destroyed==1&&walker.destroyed==1,"later restored EVA has independent lifetime");
    c.Ship=nullptr;c.EVAPawn=&replacement;const auto calls=c.handoffs;
    c.ClearEVAForLoad();check(c.EVAPawn.Get()==&replacement&&replacement.destroyed==1&&c.handoffs==calls,"missing ship guard preserves current actor");
    std::cout<<checks<<" extracted EVA lifecycle checks; failures="<<failures<<" (host adapters, not Unreal)\n";
    return failures?1:0;
}
