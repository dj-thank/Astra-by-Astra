#include "Exploration/StarExplorationCore.h"
#include <iostream>
#include <limits>
#include <cstdlib>
using namespace star::exploration;
static int Checks = 0;
void Check(bool pass, const char* message) { ++Checks; if (!pass) { std::cerr << "FAIL " << message << '\n'; std::exit(1); } }
Sample Earth(std::uint64_t sequence = 1)
{
    Sample s; s.body = Body::Earth; s.sequence = sequence; s.deltaSeconds = 0.1;
    s.altitudeM = 400000; s.speedMps = 7700; s.sunIllumination = 0.8;
    s.viewingBody = true; s.scanning = true; return s;
}
void Hold(Tracker& t, Sample& s, int samples)
{
    for (int i = 0; i < samples; ++i) { ++s.sequence; t.Observe(s); }
}
int main()
{
    Tracker t; auto s = Earth();
    t.Observe(s); const double initial = t.Progress(0);
    for (int i = 0; i < 1000; ++i) t.Observe(s);
    Check(t.Progress(0) == initial, "duplicate samples cannot accumulate");
    s.sequence = 0; t.Observe(s); Check(t.Progress(0) == initial, "zero sequence rejected");
    s.sequence = 2; s.scanning = false; t.Observe(s);
    Check(t.Progress(0) == 0, "scan release resets incomplete continuous hold");
    s.scanning = true; s.viewingBody = false; Hold(t, s, 200);
    Check(!t.Complete(0), "button alone cannot complete Earth observation");
    s.viewingBody = true; s.speedMps = 12000.01; Hold(t, s, 200);
    Check(t.Progress(0) == 0, "cruise fly-by does not count");
    s.speedMps = 7700; s.altitudeM = 99999; Hold(t, s, 200);
    Check(t.Progress(0) == 0, "surface or atmosphere Earth observation rejected");
    s.altitudeM = 400000; Hold(t, s, 119); Check(!t.Complete(0), "full hold required");
    ++s.sequence; const auto event = t.Observe(s);
    Check(event == 1 && t.Complete(0), "Earth daylight completes once at threshold");
    Check(t.Observe(s) == 0, "repeated completion sample has no new event");
    s.scanning = false; Hold(t, s, 1); Check(t.Complete(0), "completed discovery retained after leaving");
    s.scanning = true; s.sunIllumination = -0.8; Hold(t, s, 120);
    Check(t.Complete(1), "Earth night must use unlit observed surface");
    s.body = Body::Moon; s.altitudeM = 100000; s.speedMps = 1600; Hold(t, s, 100);
    Check(t.Complete(2), "Moon orbital scan completes");
    s.landed = true; s.altitudeM = 0; s.speedMps = 0; s.latitudeDeg = 0; s.longitudeDeg = 0; Hold(t, s, 100);
    Check(!t.Complete(3), "wrong lunar landing site rejected");
    s.latitudeDeg = 20.1908; s.longitudeDeg = 30.7717; s.verticalSpeedMps = -1; Hold(t, s, 100);
    Check(!t.Complete(3), "unstable touchdown rejected");
    s.verticalSpeedMps = 0; Hold(t, s, 80); Check(t.Complete(3), "stable Taurus Littrow landing recorded");
    s.body = Body::Saturn; s.landed = false; s.altitudeM = 80000000; s.speedMps = 20000; s.ringRadiusM = 100000000;
    s.viewingBody = true; s.viewingRings = false; Hold(t, s, 150);
    Check(!t.Complete(4), "ship ring radius without actual ring view rejected");
    s.viewingRings = true; s.ringRadiusM = 150000000; Hold(t, s, 150);
    Check(!t.Complete(4), "observation outside main ring region rejected");
    s.ringRadiusM = 100000000; Hold(t, s, 120);
    Check(t.Complete(4) && !t.Complete(5), "lit ring scan cannot impersonate planet shadow");
    s.observedPointInPlanetShadow = true; Hold(t, s, 120); Check(t.Complete(5), "geometrically shadowed ring observation completes");
    t.TargetSelected(); t.ViewChanged();
    Check(t.State().tutorialMask == TutorialAll, "tutorial requires recorded movement, target, view, valid scan and landing");
    Tracker restored; Check(restored.Restore(t.State()), "valid all-objective state restores");
    for (std::size_t i = 0; i < ObjectiveCount; ++i) Check(restored.Complete(i), "discovery retained after restore");
    auto corrupt = t.State(); corrupt.seconds[0] = std::numeric_limits<double>::quiet_NaN();
    Check(!restored.Restore(corrupt) && restored.Complete(0), "NaN restore rejected atomically");
    corrupt = t.State(); corrupt.seconds[1] = -1; Check(!restored.Restore(corrupt), "negative duration rejected");
    corrupt = t.State(); corrupt.seconds[1] = 13; Check(!restored.Restore(corrupt), "excess duration rejected");
    corrupt = t.State(); corrupt.tutorialMask = 32; Check(!restored.Restore(corrupt), "unknown tutorial bits rejected");
    corrupt = t.State(); corrupt.discoveries[5].observedPointInPlanetShadow = false;
    Check(!restored.Restore(corrupt), "completed record without qualifying evidence rejected");
    Tracker hitch; auto h = Earth(); h.deltaSeconds = 60; hitch.Observe(h);
    Check(hitch.Progress(0) == 0, "large unobserved time jump rejected");
    h.deltaSeconds = 0.5; ++h.sequence; hitch.Observe(h);
    Check(std::abs(hitch.Progress(0) * 12 - 0.25) < 1e-9, "hitch contribution capped");
    h.speedMps = std::numeric_limits<double>::infinity(); ++h.sequence; hitch.Observe(h);
    Check(std::abs(hitch.Progress(0) * 12 - 0.25) < 1e-9, "invalid input cannot add progress");
    Tracker tutorial; tutorial.TargetSelected(); tutorial.ViewChanged();
    Check(tutorial.State().tutorialMask == 6, "UI actions do not imply takeoff, scan or landing");
    auto partial = Earth(); Hold(tutorial, partial, 17);
    Tracker partialRestore; Check(partialRestore.Restore(tutorial.State()), "partial progress restores");
    Check(partialRestore.Progress(0) == tutorial.Progress(0), "partial duration preserved exactly");
    partial.sequence = 1; partialRestore.Observe(partial);
    Check(partialRestore.Progress(0) > tutorial.Progress(0), "new runtime sequence accepted after load");
    std::cout << "PASS " << Checks << " exploration checks\n";
}
