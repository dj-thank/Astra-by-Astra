#pragma once

#include "FlightSimulation.h"

namespace star {
enum class ObservationRayStatus : std::uint8_t { Hit, Miss, Unresolved };

struct ObservationRayOptions {
    double maxDistanceMeters = 1.0e16;
    double distanceToleranceMeters = 0.05;
    std::uint32_t maxTerrainSamples = 512; // Hard ceiling: 65,536.
};

struct ObservationRayHit {
    ObservationRayStatus status = ObservationRayStatus::Unresolved;
    double distanceMeters = 0.0;
    Vec3d pointMeters;
    Vec3d radialSimulation;
    double heightMeters = 0.0;
    std::uint32_t terrainSamples = 0;
};

// All positions/directions are simulation-space J2000 meters, never UE floats.
// The direction is normalized internally. The sampler has FlightSimulation's
// interface, but missing/nonfinite/out-of-bounds heights are UNRESOLVED, not a
// conservative synthetic surface. Sample normals are unused.
//
// Requires a continuous radial height field and conservative body min/max and
// |dh / d datum-surface-distance| slope bounds. Searches near-to-far inside the
// outer terrain sphere; the vacuum distance does not consume sample budget.
// Hit requires a sampled zero, a verified maximum-height outer-sphere entry,
// or a sign-changing interval no longer than distanceToleranceMeters.
// pointMeters is the ray point at the inside end of that bracket (or analytic
// entry); radialSimulation/heightMeters also identify sampled terrain.
// An underground origin, unavailable data, budget exhaustion or an unprovable
// grazing contact returns Unresolved. Never replace Unresolved with a datum hit.
ObservationRayHit RaycastObservationSurface(
    const BodyDefinition& body, const Vec3d& originMeters, const Vec3d& direction,
    const TerrainSampler& terrain, const ObservationRayOptions& options = {});

// Uniform-brightness solar disk minus one opaque spherical occluder's disk.
// Angular radii use the exact sphere silhouette; their overlap uses planar
// angular disks (no limb darkening, refraction, terrain horizon, ring opacity or
// spherical-cap area correction). Appropriate for a small apparent Sun and
// distinct, nonintersecting astronomical bodies. Foreground ordering compares
// sphere entry distances along an overlapping ray, not center distances.
// Multiple occluders require separate combination by the caller;
// multiplying fractions double-counts
// overlapping silhouettes. Centers/radii are J2000 meters.
// Returns finite [0,1]. Invalid geometry or a viewer inside the Sun returns 1;
// a viewer inside the foreground opaque body returns 0.
double SolarDiskVisibleFraction(
    const Vec3d& viewer, const Vec3d& sunCenter, double sunRadiusMeters,
    const Vec3d& occluderCenter, double occluderRadiusMeters);
} // namespace star
