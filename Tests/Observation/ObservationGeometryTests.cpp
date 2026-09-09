#include "ObservationGeometry.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace star;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Near(double actual, double expected, double tolerance, const char* message) {
    Check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
BodyDefinition Moon(double minHeight = -9000.0, double maxHeight = 11000.0) {
    BodyDefinition body;
    body.id = "Moon";
    body.radiusMeters = 1737400.0;
    body.landable = true;
    body.terrainMinHeightMeters = minHeight;
    body.terrainMaxHeightMeters = maxHeight;
    body.terrainMaxSlope = 4.0;
    return body;
}
TerrainSampler Constant(double height) {
    return [height](const BodyDefinition&, const Vec3d& radial, TerrainSample& value) {
        value = {height, radial}; return true;
    };
}
void BelowDatumCrater() {
    const BodyDefinition moon = Moon();
    const Vec3d cockpit{0, 0, moon.radiusMeters - 2500.0 + 4.0};
    const auto hit = RaycastObservationSurface(moon, cockpit, {0, 0, -17}, Constant(-2500.0));
    Check(hit.status == ObservationRayStatus::Hit, "Below-datum cockpit sees crater floor");
    Near(hit.distanceMeters, 4.0, 0.05, "Crater hit is four meters away, not lunar far side");
    Check(hit.radialSimulation.z > 0.999999, "Crater observation remains on near hemisphere");
    Near(hit.heightMeters, -2500.0, 0.0, "Observation retains measured negative height");
    Check(hit.terrainSamples < 100, "Near-floor search remains bounded");
    const auto away = RaycastObservationSurface(moon, cockpit, {0, 0, 1}, Constant(-2500.0));
    Check(away.status == ObservationRayStatus::Miss, "Upward crater ray does not hit datum sphere");
}
void PositivePeakAndFarCoordinates() {
    BodyDefinition moon = Moon();
    moon.centerMeters = {1.434e12, -2.315e11, 7.3e10};
    const Vec3d cockpit = moon.centerMeters + Vec3d{moon.radiusMeters + 9004.0, 0, 0};
    const auto peak = RaycastObservationSurface(moon, cockpit, {-1, 0, 0}, Constant(9000.0));
    Check(peak.status == ObservationRayStatus::Hit, "Positive-height peak is raycast");
    Near(peak.distanceMeters, 4.0, 0.05, "Peak hit is not a nine-kilometer datum drop");
    Near((peak.pointMeters - cockpit).Length(), peak.distanceMeters, 0.001,
        "World point remains consistent at astronomical coordinates");
    Near(peak.radialSimulation.x, 1.0, 1e-12, "Body-relative radial survives world translation");

    const auto far = RaycastObservationSurface(moon,
        moon.centerMeters + Vec3d{1.0e12, 0, 0}, {-1, 0, 0}, Constant(-2500.0));
    Check(far.status == ObservationRayStatus::Hit, "Trillion-meter approach reaches actual DEM");
    Near(far.distanceMeters, 1.0e12 - (moon.radiusMeters - 2500.0), 0.051,
        "Distant ray returns metric distance");
    Check(far.terrainSamples < 100, "Vacuum travel does not consume distance divided by step samples");
}
void CraterWall() {
    BodyDefinition body = Moon(-200, 100);
    body.radiusMeters = 1000.0;
    body.terrainMaxSlope = 5.0;
    const auto height = [](const Vec3d& n) {
        const double angle = std::atan2(std::hypot(n.x, n.y), n.z);
        return -200.0 + 300.0 * std::min(1.0, (angle / 0.12) * (angle / 0.12));
    };
    const TerrainSampler bowl = [&](const BodyDefinition&, const Vec3d& n, TerrainSample& value) {
        value = {height(n), n}; return true;
    };
    const auto clearance = [&](double distance) {
        const Vec3d relative{distance, 0, 810};
        return relative.Length() - body.radiusMeters - height(relative.Normalized());
    };
    double low = 0.0, high = 80.0;
    Check(clearance(low) > 0 && clearance(high) < 0, "Independent crater wall bracket");
    for (int iteration = 0; iteration < 60; ++iteration) {
        const double middle = 0.5 * (low + high);
        if (clearance(middle) > 0) low = middle; else high = middle;
    }
    const auto hit = RaycastObservationSurface(body, {0, 0, 810}, {1, 0, 0}, bowl);
    Check(hit.status == ObservationRayStatus::Hit, "Horizontal below-datum ray reaches crater wall");
    Near(hit.distanceMeters, high, 0.05, "First variable-height wall intersection agrees with independent root");
    Check(hit.distanceMeters < 80 && hit.heightMeters < 0, "Wall does not use datum or far-side fallback");
}
void GrazingAndFiniteRange() {
    BodyDefinition body = Moon(0, 0);
    body.radiusMeters = 1000;
    body.terrainMaxSlope = 0;
    const auto tangent = RaycastObservationSurface(body, {-2000, 1000, 0}, {1, 0, 0}, Constant(0));
    Check(tangent.status == ObservationRayStatus::Hit, "Exact tangent has an actual sampled zero");
    Near(tangent.distanceMeters, 2000, 1e-8, "Tangent distance");
    const auto outside = RaycastObservationSurface(body, {-2000, 1000.001, 0}, {1, 0, 0}, {});
    Check(outside.status == ObservationRayStatus::Miss && outside.terrainSamples == 0,
        "Outer-bound grazing miss requires no data");
    body.terrainMaxHeightMeters = 100;
    const auto grazing = RaycastObservationSurface(body, {-2000, 1000.001, 0}, {1, 0, 0}, Constant(0));
    Check(grazing.status == ObservationRayStatus::Miss, "Near-tangent positive clearance is not a phantom hit");
    const auto nearTangent = RaycastObservationSurface(body, {-2000, 999.99, 0}, {1, 0, 0}, Constant(0));
    Check(nearTangent.status == ObservationRayStatus::Hit, "Shallow grazing intersection is detected");
    Near(nearTangent.distanceMeters, 2000.0 - std::sqrt(1000.0 * 1000.0 - 999.99 * 999.99),
        0.05, "Shallow grazing ray returns first hit");
    ObservationRayOptions options;
    options.maxDistanceMeters = 99;
    const auto shortRay = RaycastObservationSurface(body, {1100, 0, 0}, {-1, 0, 0}, Constant(0), options);
    Check(shortRay.status == ObservationRayStatus::Miss, "Finite ray ends before terrain");
    const auto behind = RaycastObservationSurface(body, {1200, 0, 0}, {1, 0, 0}, {});
    Check(behind.status == ObservationRayStatus::Miss, "Body entirely behind ray is a miss");
}
void RotatedSphericalSurfaces() {
    BodyDefinition body = Moon(25, 25);
    body.radiusMeters = 1000;
    for (int index = 1; index <= 100; ++index) {
        const Vec3d radial = Vec3d{std::cos(index * 0.73), std::sin(index * 0.37),
            std::cos(index * 0.19)}.Normalized();
        const auto hit = RaycastObservationSurface(body, radial * 2025, -radial, Constant(25));
        Check(hit.status == ObservationRayStatus::Hit, "Rotated constant-height sphere retains analytic boundary contact");
        Near(hit.distanceMeters, 1000, 0.05, "Rotated sphere first intersection distance");
    }
}
void UncertaintyNeverBecomesObservation() {
    const auto body = Moon();
    const Vec3d start{0, 0, body.radiusMeters + 100};
    ObservationRayOptions budget;
    budget.maxTerrainSamples = 1;
    const auto limited = RaycastObservationSurface(body, start, {0, 0, -1}, Constant(-2500), budget);
    Check(limited.status == ObservationRayStatus::Unresolved && limited.terrainSamples == 1,
        "Budget exhaustion remains unresolved");
    Check(RaycastObservationSurface(body, start, {0, 0, -1}, {}).status == ObservationRayStatus::Unresolved,
        "Missing sampler never supplies a phantom max-height hit");
    const TerrainSampler missing = [](const BodyDefinition&, const Vec3d&, TerrainSample&) { return false; };
    Check(RaycastObservationSurface(body, start, {0, 0, -1}, missing).status == ObservationRayStatus::Unresolved,
        "Missing height remains unresolved");
    Check(RaycastObservationSurface(body, start, {0, 0, -1}, Constant(20000)).status == ObservationRayStatus::Unresolved,
        "Height outside supplied bounds is unresolved");
    Check(RaycastObservationSurface(body, start, {0, 0, -1},
        Constant(std::numeric_limits<double>::quiet_NaN())).status == ObservationRayStatus::Unresolved,
        "Nonfinite height is unresolved");
    Check(RaycastObservationSurface(body, {0, 0, body.radiusMeters - 2600}, {0, 0, -1},
        Constant(-2500)).status == ObservationRayStatus::Unresolved,
        "Underground camera does not observe far side");
    Check(RaycastObservationSurface(body, start, {}, Constant(0)).status == ObservationRayStatus::Unresolved,
        "Zero direction is invalid rather than an arbitrary default ray");
}
Vec3d AngularPosition(double distance, double angle) {
    return {distance * std::cos(angle), distance * std::sin(angle), 0};
}
void EclipseCases() {
    const Vec3d viewer;
    const Vec3d sun{10000, 0, 0};
    Near(SolarDiskVisibleFraction(viewer, sun, 100, {-1000, 0, 0}, 50), 1, 0,
        "Occluder behind viewer leaves full day");
    Near(SolarDiskVisibleFraction(viewer, sun, 100, {20000, 0, 0}, 500), 1, 0,
        "Body behind Sun does not eclipse");
    Near(SolarDiskVisibleFraction(viewer, sun, 100, {1000, 0, 0}, 50), 0, 0,
        "Aligned foreground body fully eclipses Sun");
    Near(SolarDiskVisibleFraction(viewer, sun, 100, {0, 1000, 0}, 50), 1, 0,
        "Off-axis body leaves full day");
    const double sunAngle = std::asin(0.01);
    const double halfAngleRadius = 1000.0 * std::sin(sunAngle * 0.5);
    Near(SolarDiskVisibleFraction(viewer, sun, 100, {1000, 0, 0}, halfAngleRadius), 0.75, 1e-12,
        "Annular disk covers one quarter of angular area");
    const double equalRadius = 1000.0 * std::sin(sunAngle);
    const double expectedVisible = 1.0 - (2.0 * Pi / 3.0 - std::sqrt(3.0) / 2.0) / Pi;
    Near(SolarDiskVisibleFraction(viewer, sun, 100, AngularPosition(1000, sunAngle), equalRadius),
        expectedVisible, 1e-12, "Equal disks separated by one radius have known analytic overlap");
    Near(SolarDiskVisibleFraction(viewer, sun, 100, AngularPosition(1000, 2 * sunAngle), equalRadius),
        1.0, 1e-12, "Externally tangent disks have zero covered area");
    const double almostTangent = SolarDiskVisibleFraction(viewer, sun, 100,
        AngularPosition(1000, 2 * sunAngle * (1 - 1e-7)), equalRadius);
    Check(almostTangent < 1 && almostTangent > 0.999999, "Tiny grazing overlap remains finite and visible");
}
void EclipseSmallAnglesAndLocalHorizon() {
    const double angle = 1e-9;
    const double expectedVisible = 1.0 - (2.0 * Pi / 3.0 - std::sqrt(3.0) / 2.0) / Pi;
    Near(SolarDiskVisibleFraction({}, {1e12, 0, 0}, 1e12 * std::sin(angle),
        AngularPosition(1e9, angle), 1e9 * std::sin(angle)), expectedVisible, 1e-12,
        "Atan2 preserves tiny angular separation that acos would round to zero");
    const Vec3d shift{1.4e12, -2e11, 7e10};
    const double baseline = SolarDiskVisibleFraction({}, {1.5e11, 0, 0}, 6.957e8,
        {3.84e8, 1.3e6, 0}, 1.7374e6);
    Near(SolarDiskVisibleFraction(shift, shift + Vec3d{1.5e11, 0, 0}, 6.957e8,
        shift + Vec3d{3.84e8, 1.3e6, 0}, 1.7374e6), baseline, 1e-12,
        "Solar fraction is translation invariant at astronomical world coordinates");
    // At the surface, the opaque body subtends a hemisphere. Its center may
    // lie behind the center solar ray while still covering part of the disk.
    const double sunAngle = std::asin(0.00465);
    const double horizon = SolarDiskVisibleFraction({}, {1e8, 0, 0}, 465000,
        AngularPosition(1000, Pi / 2), 1000);
    Check(horizon > 0.499 && horizon < 0.501, "Local horizon cuts approximately half the solar disk");
    const double justAbove = SolarDiskVisibleFraction({}, {1e8, 0, 0}, 465000,
        AngularPosition(1000, Pi / 2 + sunAngle * 0.5), 1000);
    Check(justAbove > horizon && justAbove < 1, "Body-center behind viewer may still cover lower solar limb");
    double previous = 0;
    for (int step = 0; step <= 200; ++step) {
        const double separation = Pi / 2 - sunAngle + 2 * sunAngle * step / 200;
        const double fraction = SolarDiskVisibleFraction({}, {1e8, 0, 0}, 465000,
            AngularPosition(1000, separation), 1000);
        Check(std::isfinite(fraction) && fraction >= 0 && fraction <= 1, "Finite clamped eclipse fraction");
        Check(fraction + 1e-10 >= previous, "Sunrise visibility varies monotonically");
        previous = fraction;
    }
    Near(SolarDiskVisibleFraction({}, {1e8, 0, 0}, 465000, {}, 1000), 0, 0,
        "Viewer inside opaque sphere has no direct solar disk");
    Near(SolarDiskVisibleFraction({}, {}, 1, {1000, 0, 0}, 10), 1, 0,
        "Degenerate Sun geometry returns finite documented fallback");
}
void EclipseSurfaceDepthOrdering() {
    // Both bodies are disjoint and the viewer is outside both. The foreground
    // limb is closer than the Sun although its SPHERE CENTER is ten times farther.
    const Vec3d sun{10, 0, 0};
    const Vec3d foreground{5, 100, 0};
    const double visible = SolarDiskVisibleFraction({}, sun, 0.01, foreground, 100);
    // Independent reviewer ray/sphere grid: 502,605 solar samples, 0.500793
    // visible. Tolerance allows that grid's boundary error and angular-disk
    // versus exact spherical-silhouette area, but rejects the old result 1.
    Near(visible, 0.500793, 0.002, "Farther sphere center may have a foreground eclipsing limb");
    Check(visible > 0.49 && visible < 0.51, "Foreground giant body covers approximately half the Sun");
    Near(SolarDiskVisibleFraction({}, sun, 0.01, {20, 100, 0}, 100), 1, 0,
        "Same angular limb configuration behind Sun does not eclipse");
}
} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"below-datum crater", BelowDatumCrater},
        {"positive peak and far coordinates", PositivePeakAndFarCoordinates},
        {"actual variable-height crater wall", CraterWall},
        {"grazing and finite range", GrazingAndFiniteRange},
        {"rotated spherical surfaces", RotatedSphericalSurfaces},
        {"uncertainty never becomes observation", UncertaintyNeverBecomesObservation},
        {"analytic eclipse cases", EclipseCases},
        {"small-angle eclipse and local horizon", EclipseSmallAnglesAndLocalHorizon},
        {"eclipse surface depth ordering", EclipseSurfaceDepthOrdering}
    };
    unsigned failed = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) {
            ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    }
    std::cout << (tests.size() - failed) << '/' << tests.size()
              << " native observation geometry groups passed (CPU only).\n";
    return failed == 0 ? 0 : 1;
}
