#pragma once

#include "Simulation/FlightSimulation.h"
#include <atomic>
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

// Portable geometry shared by runtime and the native real-DEM verification.
// All values here are body-fixed RH meters, except the explicit UV coordinates.
namespace star::terrain {
constexpr double ActiveAltitudeMeters = 300000.0;
constexpr unsigned MaxPatchCells = 64;
// Center offsets split some ring rectangles into additional bounded uploads.
// 16 core + at most5*18 dense-ring +6*8 horizon-ring patches =154.
constexpr unsigned MaxGroundPatches = 160;
constexpr unsigned MaxClastPatches = 25;
struct UV { double u = 0, v = 0; };
struct Rect {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool Contains(double x, double y, double margin = 0) const;
};
struct Chart {
    Vec3d radial, east, north;
    double radius = 1737400;
    static Chart At(Vec3d direction, double radius);
    Vec3d Direction(double x, double y) const;
    bool Project(Vec3d direction, double& x, double& y) const;
};
struct Patch {
    Rect rect;
    double step = 5;
    unsigned level = 0;
    // west,east,south,north: a finer edge conforms to this neighbor's posts.
    std::array<double,4> edgeStep{};
    unsigned skirtEdges = 15; // Only the plan's external perimeter needs skirts.
    bool operator==(const Patch& other) const;
};
struct Plan {
    Chart chart;
    Rect fine, outer;
    double fineStep = 5;
    Vec3d holeDirection;
    double holeAngle = 0;
    std::vector<Patch> patches;
    std::vector<Patch> clastPatches; // Independent 60m near-field visual tiles.
    bool Contains(Vec3d direction, bool fineOnly = false, double margin = 0) const;
};
Plan MakePlan(const Chart& chart, Vec3d shipDirection, double altitudeMeters,
              double terrainRangeMeters = 20000);
bool SamePlan(const Plan& a, const Plan& b);

struct Vertex {
    Vec3d position; // Relative to Mesh::anchorBodyMeters; never solar-system float.
    Vec3d normal;
    Vec3d tangent;
    UV uv0;        // Geographic, with integer U unwraps on seam triangles.
    UV uv1;        // Fixed body-chart tangent meters, retained across tile/LOD changes.
    UV uv2;        // Clasts: (1,seating height). Ground: (0,photo-camera ENU up meters).
    UV uv3;        // Clasts: (variation,radius/.295m). Ground: fixed photo-camera east,north meters.
};
struct ClastRange {
    std::size_t firstVertex = 0, vertexCount = 0, firstIndex = 0, indexCount = 0;
    Vec3d center, up; // Center is mesh-local and on the measured seating triangle.
    double radius = 0, height = 0;
    UV siteMeters;
    bool primary = true;
};
struct Mesh {
    Vec3d anchorBodyMeters;
    std::vector<Vertex> vertices;
    std::vector<std::int32_t> indices;
    std::size_t surfaceIndexCount = 0;
    std::size_t sourceSamples = 0;
    std::size_t reconstructedClastCount = 0;
    std::vector<ClastRange> clasts; // CPU provenance/ranges; never a collision mesh.
    bool complete = false;
};
using HeightSampler = std::function<double(double latitudeDegrees, double longitudeDegrees)>;
Mesh BuildPatch(const Chart& chart, const Patch& patch, const HeightSampler& sample,
                const std::atomic_bool* cancel = nullptr);
// Optional visual reconstruction at the Apollo17 tour site. No source heights,
// contact sampler, measured surface vertices or route are modified.
void AppendReconstructedClasts(Mesh& mesh, const Chart& chart, const Patch& patch,
                               const std::atomic_bool* cancel = nullptr, bool dense = false, bool companions = true);
// Reconstructed geometry only; underlying measured triangles are sampled for
// seating, then omitted from this independent no-collision/shadow-control mesh.
Mesh BuildClastPatch(const Chart& chart, const Patch& patch, const HeightSampler& sample,
                     const std::atomic_bool* cancel = nullptr);
// Y reflection changes winding. Positions/normals are reflected by the UE adapter;
// indices use UE clockwise front faces AFTER that reflection (cross dot normal < 0).
Vec3d UnrealLocalCentimeters(Vec3d bodyLocalMeters);
} // namespace star::terrain
