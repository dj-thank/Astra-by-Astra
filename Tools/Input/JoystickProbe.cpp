#include "Core/StarSdlJoystick.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <thread>

static std::string Json(const std::string& input) {
    std::string out = "\"";
    for (unsigned char c : input) {
        if (c == '\\' || c == '"') { out += '\\'; out += static_cast<char>(c); }
        else if (c >= 32) out += static_cast<char>(c);
    }
    return out + '"';
}
int main(int argc, char** argv) {
    using namespace star::input;
    int seconds = 0;
    if (argc == 3 && std::string(argv[1]) == "--seconds") seconds = std::clamp(std::atoi(argv[2]), 0, 60);
    else if (argc != 1) { std::cerr << "Usage: StarJoystickProbe [--seconds 0..60]\n"; return 2; }
    SdlJoystickBackend backend;
    if (!backend.Initialize()) { std::cerr << backend.LastError() << '\n'; return 1; }
    // WGI enumeration is asynchronous. Give a bounded discovery window even for a one-shot probe.
    for (int i = 0; i < 10; ++i) { SDL_UpdateJoysticks(); std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
    auto devices = backend.Enumerate();
    std::cout << "{\"kind\":\"enumeration\",\"backendPolicy\":\"SDL3_WGI_shared_no_DirectInput\",\"sdlVersion\":" << SDL_GetVersion() << ",\"devices\":[";
    bool first = true, targetPresent = false;
    for (const auto& d : devices) {
        if (!first) std::cout << ','; first = false;
        std::cout << "{\"name\":" << Json(d.Name) << ",\"guid\":" << Json(d.Guid)
          << ",\"vendorId\":" << d.VendorId << ",\"productId\":" << d.ProductId
          << ",\"axes\":" << d.NumAxes << ",\"buttons\":" << d.NumButtons << ",\"hats\":" << d.NumHats
          << ",\"wheel\":" << (d.IsWheel ? "true" : "false") << ",\"gamepad\":" << (d.IsGamepad ? "true" : "false")
          << ",\"virtual\":" << (d.IsVirtual ? "true" : "false") << ",\"flightEligible\":" << (d.Eligible ? "true" : "false") << '}';
        targetPresent |= MatchesSelection(d, Selection{}) && !d.IsVirtual;
    }
    std::cout << "],\"physicalThrustmasterFlightStickPresent\":" << (targetPresent ? "true" : "false") << "}\n";
    const auto start = std::chrono::steady_clock::now();
    do {
        const auto raw = backend.Poll();
        std::cout << "{\"kind\":\"raw\",\"elapsedMs\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()
          << ",\"connected\":" << (raw.Connected ? "true" : "false") << ",\"axisDataReady\":" << (raw.AxisDataReady ? "true" : "false") << ",\"axes\":[";
        for (int i = 0; i < raw.NumAxes; ++i) { if (i) std::cout << ','; std::cout << raw.Axes[i]; }
        std::cout << "],\"buttons\":[";
        for (int i = 0; i < raw.NumButtons; ++i) { if (i) std::cout << ','; std::cout << (raw.Buttons[i] ? 1 : 0); }
        std::cout << "],\"hats\":[";
        for (int i = 0; i < raw.NumHats; ++i) { if (i) std::cout << ','; std::cout << static_cast<int>(raw.Hats[i]); }
        std::cout << "]}\n";
        if (seconds == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } while (std::chrono::steady_clock::now()-start < std::chrono::seconds(seconds));
    return 0;
}
