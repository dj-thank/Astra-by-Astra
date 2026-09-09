#include "ObservationGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace star {
namespace {
double ClampUnit(double value) { return std::max(0.0, std::min(1.0, value)); }

double AngularSeparation(const Vec3d& a, const Vec3d& b) {
    return std::atan2(Vec3d::Cross(a, b).Length(), Vec3d::Dot(a, b));
}

double AngularRadius(double radius, double distance) {
    const double ratio = ClampUnit(radius / distance);
    return std::atan2(ratio, std::sqrt((1.0 - ratio) * (1.0 + ratio)));
}

double SphereEntryDistance(const Vec3d& centerFromViewer, double radius, const Vec3d& ray) {
    const double projection = Vec3d::Dot(centerFromViewer, ray);
    const double perpendicular = Vec3d::Cross(centerFromViewer, ray).Length();
    if (projection <= 0.0 || perpendicular > radius) return std::numeric_limits<double>::infinity();
    const double halfChord = std::sqrt((radius - perpendicular) * (radius + perpendicular));
    const double distance = centerFromViewer.Length();
    // Rationalized near root avoids subtracting almost equal distances at a
    // local horizon or for an observer resting on the spherical surface.
    return std::max(0.0, ((distance - radius) / (projection + halfChord)) * (distance + radius));
}

// theta - sin(theta)*cos(theta), retaining tiny near-tangent circular segments.
double CircularSegment(double theta) {
    if (theta < 0.01) {
        const double square = theta * theta;
        return theta * square * (2.0 / 3.0 + square * (-2.0 / 15.0 +
            square * (4.0 / 315.0 - square * (2.0 / 2835.0))));
    }
    return theta - std::sin(theta) * std::cos(theta);
}

struct RaySample {
    double along = 0.0; // Distance along ray from its closest point to body.
    Vec3d relative;
    Vec3d radial;
    double height = 0.0;
    double clearance = 0.0;
};

struct RayInterval { RaySample first, last; };
} // namespace

ObservationRayHit RaycastObservationSurface(
    const BodyDefinition& body, const Vec3d& originMeters, const Vec3d& direction,
    const TerrainSampler& terrain, const ObservationRayOptions& options) {
    ObservationRayHit result;
    const double innerRadius = body.radiusMeters + body.terrainMinHeightMeters;
    const double outerRadius = body.radiusMeters + body.terrainMaxHeightMeters;
    const double directionLength = direction.Length();
    if (!originMeters.IsFinite() || !direction.IsFinite() ||
        !body.centerMeters.IsFinite() || !std::isfinite(body.radiusMeters) ||
        body.radiusMeters <= 0.0 || !std::isfinite(innerRadius) ||
        !std::isfinite(outerRadius) || innerRadius <= 0.0 || outerRadius < innerRadius ||
        !std::isfinite(body.terrainMaxSlope) || body.terrainMaxSlope < 0.0 ||
        !std::isfinite(directionLength) || directionLength <= 0.0 ||
        !std::isfinite(options.maxDistanceMeters) || options.maxDistanceMeters < 0.0 ||
        !std::isfinite(options.distanceToleranceMeters) || options.distanceToleranceMeters <= 0.0)
        return result;

    const Vec3d ray = direction / directionLength;
    const Vec3d offset = originMeters - body.centerMeters;
    const double projection = Vec3d::Dot(offset, ray);
    // Reparametrize once into body-local coordinates. Subsequent samples never
    // add a tiny step to a solar-system-sized world coordinate or ray distance.
    const Vec3d nearest = offset - ray * projection;
    const double perpendicular = nearest.Length();
    if (!offset.IsFinite() || !std::isfinite(projection) || !std::isfinite(perpendicular))
        return result;
    const auto miss = [&result]() { result.status = ObservationRayStatus::Miss; return result; };
    if (perpendicular > outerRadius) return miss();
    const double outerHalf = std::sqrt((outerRadius - perpendicular) * (outerRadius + perpendicular));
    if (!std::isfinite(outerHalf)) return result;
    const double firstAlong = std::max(projection, -outerHalf);
    double lastAlong = std::min(projection + options.maxDistanceMeters, outerHalf);
    if (lastAlong < firstAlong) return miss();

    // Every valid radial surface encloses this inner sphere. The first inward
    // crossing must be reached before its entry, avoiding the radial singularity
    // and any erroneous far-side observation from a below-datum cockpit.
    if (perpendicular < innerRadius) {
        const double innerHalf = std::sqrt((innerRadius - perpendicular) * (innerRadius + perpendicular));
        if (firstAlong > -innerHalf && firstAlong < innerHalf) return result;
        if (firstAlong <= -innerHalf) lastAlong = std::min(lastAlong, -innerHalf);
    }

    const std::uint32_t budget = std::min(options.maxTerrainSamples, 65536u);
    const auto sample = [&](double along, RaySample& value) {
        if (!terrain || result.terrainSamples >= budget) return false;
        value.along = along;
        value.relative = nearest + ray * along;
        const double radius = value.relative.Length();
        if (!std::isfinite(radius) || radius <= 0.0) return false;
        value.radial = value.relative / radius;
        TerrainSample measured;
        ++result.terrainSamples;
        if (!terrain(body, value.radial, measured) || !std::isfinite(measured.heightMeters) ||
            measured.heightMeters < body.terrainMinHeightMeters ||
            measured.heightMeters > body.terrainMaxHeightMeters) return false;
        value.height = measured.heightMeters;
        value.clearance = (radius - body.radiusMeters) - value.height;
        return std::isfinite(value.clearance);
    };
    const auto hit = [&](const RaySample& value) {
        result.status = ObservationRayStatus::Hit;
        result.distanceMeters = std::max(0.0, value.along - projection);
        result.pointMeters = body.centerMeters + value.relative;
        result.radialSimulation = value.radial;
        result.heightMeters = value.height;
        return result;
    };
    RaySample first;
    if (!sample(firstAlong, first)) return result;
    // The outer entry is already an analytic sphere intersection. A measured
    // maximum height there establishes contact without subtracting rounded
    // radial lengths (which can have either sign for a rotated sphere).
    if (firstAlong == -outerHalf && first.height == body.terrainMaxHeightMeters) return hit(first);
    if (first.clearance < 0.0) return result;
    if (first.clearance == 0.0) return hit(first);
    if (firstAlong == lastAlong) return miss();
    RaySample last;
    if (!sample(lastAlong, last)) return result;

    std::vector<RayInterval> pending;
    pending.push_back({first, last});
    while (!pending.empty()) {
        const RayInterval interval = pending.back();
        pending.pop_back();
        const RaySample& a = interval.first;
        const RaySample& b = interval.last;
        const double width = b.along - a.along;
        if (a.clearance == 0.0) return hit(a);

        // Conservative height envelope from BOTH endpoints along the great
        // circle arc. This proves an interval empty without a uniform 5m walk.
        // Min radius is analytic and independent of the DEM samples.
        const double closestAlong = std::max(a.along, std::min(0.0, b.along));
        const double minRadius = (nearest + ray * closestAlong).Length();
        const double arc = AngularSeparation(a.radial, b.radial);
        const double variation = body.terrainMaxSlope * body.radiusMeters * arc;
        const double numericalMargin = 32.0 * std::numeric_limits<double>::epsilon() *
            std::max(1.0, outerRadius);
        if (!std::isfinite(variation) || std::abs(a.height - b.height) > variation + numericalMargin)
            return result; // Observed violation of the supplied slope bound.
        const double maxHeight = std::min(body.terrainMaxHeightMeters,
            0.5 * a.height + 0.5 * b.height + 0.5 * variation);
        if ((minRadius - body.radiusMeters) - maxHeight > numericalMargin) continue;

        // A near miss inside tolerance is not enough to claim a hit: preserve
        // uncertainty unless measured points bracket a continuous surface.
        if (width <= options.distanceToleranceMeters && a.clearance > 0.0 && b.clearance <= 0.0)
            return hit(b);
        const double middleAlong = a.along + width * 0.5;
        if (middleAlong <= a.along || middleAlong >= b.along) return result;
        RaySample middle;
        if (!sample(middleAlong, middle)) return result;
        pending.push_back({middle, b});
        pending.push_back({a, middle}); // LIFO, so certify nearer intervals first.
    }
    return miss();
}

double SolarDiskVisibleFraction(
    const Vec3d& viewer, const Vec3d& sunCenter, double sunRadiusMeters,
    const Vec3d& occluderCenter, double occluderRadiusMeters) {
    if (!viewer.IsFinite() || !sunCenter.IsFinite() || !occluderCenter.IsFinite() ||
        !std::isfinite(sunRadiusMeters) || sunRadiusMeters <= 0.0 ||
        !std::isfinite(occluderRadiusMeters) || occluderRadiusMeters <= 0.0) return 1.0;
    const Vec3d toSun = sunCenter - viewer;
    const Vec3d toOccluder = occluderCenter - viewer;
    const double sunDistance = toSun.Length();
    const double occluderDistance = toOccluder.Length();
    if (!std::isfinite(sunDistance) || !std::isfinite(occluderDistance) ||
        sunDistance <= sunRadiusMeters) return 1.0;
    const double surfaceRoundoff = 32.0 * std::numeric_limits<double>::epsilon() * occluderRadiusMeters;
    if (occluderRadiusMeters - occluderDistance > surfaceRoundoff) return 0.0;
    const double sunAngle = AngularRadius(sunRadiusMeters, sunDistance);
    const double occluderAngle = AngularRadius(occluderRadiusMeters, occluderDistance);
    const Vec3d sunDirection = toSun / sunDistance;
    const Vec3d occluderDirection = toOccluder / occluderDistance;
    const double separation = AngularSeparation(sunDirection, occluderDirection);
    if (separation >= sunAngle + occluderAngle) return 1.0;

    // A large sphere's foreground limb can be closer than the Sun while its
    // center is farther away. Test one ray strictly within BOTH angular disks.
    // Disjoint spheres cannot exchange depth ordering across their connected
    // overlap without intersecting, so this ray decides the whole overlap.
    Vec3d overlapRay = sunDirection;
    const Vec3d planeNormal = Vec3d::Cross(sunDirection, occluderDirection);
    const double planeLength = planeNormal.Length();
    if (planeLength > 0.0) {
        const double lower = std::max(0.0, separation - occluderAngle);
        const double upper = std::min(sunAngle, separation + occluderAngle);
        const double angle = 0.5 * lower + 0.5 * upper;
        const Vec3d towardOccluder = Vec3d::Cross(planeNormal / planeLength, sunDirection);
        overlapRay = (sunDirection * std::cos(angle) + towardOccluder * std::sin(angle)).Normalized();
    }
    const double sunEntry = SphereEntryDistance(toSun, sunRadiusMeters, overlapRay);
    const double occluderEntry = SphereEntryDistance(toOccluder, occluderRadiusMeters, overlapRay);
    if (!std::isfinite(sunEntry) || occluderEntry >= sunEntry) return 1.0;
    if (occluderAngle >= separation + sunAngle) return 0.0;
    if (sunAngle >= separation + occluderAngle) {
        const double ratio = occluderAngle / sunAngle;
        return ClampUnit(1.0 - ratio * ratio);
    }

    // Normalize the Sun to radius one. Factor d^2-r^2 to retain precision
    // near a large local body's limb; atan2 retains tiny disk separations.
    const double radius = occluderAngle / sunAngle;
    const double distance = separation / sunAngle;
    const double chord = ((distance - radius) * (distance + radius) + 1.0) / (2.0 * distance);
    const double halfChord = std::sqrt(std::max(0.0, (1.0 - chord) * (1.0 + chord)));
    const double sunSegment = std::atan2(halfChord, chord);
    const double occluderSegment = std::atan2(halfChord, distance - chord);
    const double covered = (CircularSegment(sunSegment) +
        radius * radius * CircularSegment(occluderSegment)) / Pi;
    return std::isfinite(covered) ? ClampUnit(1.0 - covered) : 1.0;
}
} // namespace star
