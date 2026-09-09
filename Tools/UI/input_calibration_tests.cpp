#include "UI/StarInputCalibration.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace star::input;
using namespace star::input::calibration;
static int Checks = 0;
void Check(bool ok, const char* reason) { ++Checks; if (!ok) { std::cerr << "FAIL " << reason << '\n'; std::exit(1); } }
RawState ReadyRaw()
{
    RawState r; r.Connected = r.AxisDataReady = true; r.NumAxes = 4; r.NumButtons = 16; r.NumHats = 1; return r;
}
int main()
{
    StableCapture capture; int value = 123;
    Check(capture.Observe(0, 0.1, true, value) == CaptureResult::Invalid, "button must explicitly start capture");
    capture.Start();
    for (int i = 0; i < 5; ++i) Check(capture.Observe(100, 0.05, true, value) == CaptureResult::Waiting, "single sample or short hold is insufficient");
    for (int i = 0; i < 2; ++i) capture.Observe(100, 0.05, true, value);
    Check(capture.Observe(100, 0.05, true, value) == CaptureResult::Captured && value == 100, "stable 0.4 seconds captures signed raw units");
    capture.Start(); capture.Observe(-32768, 0.1, true, value);
    for (int i = 0; i < 4; ++i) capture.Observe(32767, 0.05, true, value);
    Check(capture.Active(), "moving across stroke resets stability window");
    capture.Cancel(); capture.Start();
    Check(capture.Observe(0, 0.05, false, value) == CaptureResult::Invalid && !capture.Active(), "disconnected or unfocused capture aborted");
    capture.Start(); Check(capture.Observe(0, 1, true, value) == CaptureResult::Invalid, "hitch cannot impersonate observed duration");
    capture.Start(); Check(capture.Observe(32768, 0.1, true, value) == CaptureResult::Invalid, "raw out of SDL range rejected");
    capture.Start(); Check(capture.Observe(0, std::numeric_limits<double>::quiet_NaN(), true, value) == CaptureResult::Invalid, "NaN elapsed time rejected");
    capture.Start(); CaptureResult timeout = CaptureResult::Waiting;
    for (int i = 0; i < 90; ++i) { timeout = capture.Observe(i % 2 == 0 ? -32768 : 32767, 0.1, true, value); if (timeout == CaptureResult::TimedOut) break; }
    Check(timeout == CaptureResult::TimedOut, "unstable capture has bounded timeout");
    Evidence proof; proof.Reset(0); proof.Record(Point::Minimum, 0); proof.Record(Point::Center, 0); proof.Record(Point::Maximum, 0);
    Check(!proof.Ready(), "three keypresses at unmoved axis do not calibrate");
    proof.Record(Point::Minimum, -12000); proof.Record(Point::Maximum, 12000);
    Check(!proof.Ready(), "short travel is insufficient");
    proof.Record(Point::Minimum, -32768); proof.Record(Point::Maximum, 32767);
    Check(proof.Ready(), "full measured stroke and center accepted");
    proof.Record(Point::Center, 32000); Check(!proof.Ready(), "center cannot be near endpoint");
    proof.Reset(1); Check(!proof.Ready(), "changing physical source discards evidence");

    Profile draft, output; auto raw = ReadyRaw(); std::array<Evidence, AxisCount> evidence;
    for (int i = 0; i < AxisCount; ++i) evidence[i].Reset(i);
    output.MappingConfirmed = false; output.Axes[0].Exponent = 2;
    Check(BuildConfirmedProfile(draft, evidence, raw, true, true, true, output) == Validation::NeedMeasurements, "explicit checkbox alone never confirms mapping");
    for (int i = 0; i < AxisCount; ++i)
    { evidence[i].Record(Point::Minimum, -32768); evidence[i].Record(Point::Center, 0); evidence[i].Record(Point::Maximum, 32767); }
    Check(BuildConfirmedProfile(draft, evidence, raw, true, true, false, output) == Validation::NeedConfirmation, "measurements alone never confirm user direction");
    Check(output.Axes[0].Exponent == 2 && !output.MappingConfirmed, "failure leaves output unchanged");
    Check(BuildConfirmedProfile(draft, evidence, raw, true, true, true, output) == Validation::Ready && output.MappingConfirmed, "valid measured user-confirmed mapping built atomically");
    raw.Connected = false; Check(BuildConfirmedProfile(draft, evidence, raw, true, true, true, output) == Validation::NoDevice, "save cannot use disconnected cached values");
    raw.Connected = true; raw.AxisDataReady = false;
    Check(BuildConfirmedProfile(draft, evidence, raw, true, true, true, output) == Validation::NoSamples, "enumeration alone rejected");
    raw.AxisDataReady = true;
    Check(BuildConfirmedProfile(draft, evidence, raw, false, true, true, output) == Validation::Unfocused, "unfocused confirmation rejected");
    Check(BuildConfirmedProfile(draft, evidence, raw, true, false, true, output) == Validation::ChangedDevice, "reconnected or different device invalidates session");
    draft.Axes[1].Index = 0; evidence[1].Source = 0;
    Check(BuildConfirmedProfile(draft, evidence, raw, true, true, true, output) == Validation::DuplicateAxis, "one physical axis cannot drive two flight controls");
    draft.Axes[1].Index = 1; evidence[1].Source = 1; draft.ButtonMap[1] = 0;
    Check(BuildConfirmedProfile(draft, evidence, raw, true, true, true, output) == Validation::DuplicateButton, "same physical press cannot invoke two actions");
    draft.ButtonMap[1] = -1;
    Check(BuildConfirmedProfile(draft, evidence, raw, true, true, true, output) == Validation::Ready, "unassigned logical buttons allowed");
    draft.Axes[0].Center = 10;
    Check(BuildConfirmedProfile(draft, evidence, raw, true, true, true, output) == Validation::NeedMeasurements, "edited calibration must match evidence");
    draft.Axes[0].Center = 0; draft.Axes[0].Exponent = std::numeric_limits<float>::infinity();
    Check(BuildConfirmedProfile(draft, evidence, raw, true, true, true, output) == Validation::InvalidProfile, "plugin validation is honored");
    draft.Axes[0].Exponent = 1; raw.NumAxes = 3;
    Check(BuildConfirmedProfile(draft, evidence, raw, true, true, true, output) == Validation::InvalidProfile, "profile must fit actual current device");
    raw = ReadyRaw();
    const auto serialized = SerializeProfile(output); Profile parsed; std::string error;
    Check(ParseProfile(serialized, parsed, error) && parsed.MappingConfirmed, "confirmed profile uses actual plugin serialization roundtrip");
    Check(NormalizeAxis(32767, parsed.Axes[3], true) == 0 && NormalizeAxis(-32768, parsed.Axes[3], true) == 1, "inverted throttle minimum and maximum use plugin units");

    ButtonListener listener; listener.Start(); raw.Buttons[3] = true;
    Check(listener.Observe(raw, 0.1) == -1 && listener.WaitingForRelease(), "held opener button is ignored");
    raw.Buttons[3] = false; Check(listener.Observe(raw, 0.1) == -1 && !listener.WaitingForRelease(), "all buttons release arms listener");
    raw.Buttons[7] = true;
    Check(listener.Observe(raw, 0.1) == 7 && !listener.Active(), "single subsequent press is captured once");
    Check(listener.Observe(raw, 0.1) == -1, "captured button is not duplicated");
    listener.Start(); raw.Buttons = {}; listener.Observe(raw, 0.1); raw.Buttons[2] = raw.Buttons[4] = true;
    Check(listener.Observe(raw, 0.1) == -1 && listener.WaitingForRelease(), "simultaneous presses are ambiguous");
    raw.Buttons[2] = false;
    Check(listener.Observe(raw, 0.1) == -1, "releasing only one ambiguous button is insufficient");
    raw.Buttons = {}; listener.Observe(raw, 0.1); raw.Buttons[4] = true;
    Check(listener.Observe(raw, 0.1) == 4, "fresh press after full release resolves ambiguity");
    listener.Start(); raw.Connected = false;
    Check(listener.Observe(raw, 0.1) == -1 && !listener.Active(), "disconnect aborts listening");
    raw = ReadyRaw(); listener.Start();
    for (int i = 0; i < 121; ++i) listener.Observe(raw, 0.1);
    Check(!listener.Active(), "listen timeout is bounded");
    std::cout << "PASS " << Checks << " input calibration checks\n";
}
