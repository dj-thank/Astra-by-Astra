"""CPU reference invariants and optional Windows standalone HLSL compilation.

python Tools/Unreal/Materials/verify_materials.py --compile-hlsl

D3DCompile ps_5_0 verifies syntax of the exact shader bodies with Texture2DSample
macros and material-input stubs. It DOES NOT verify UE generated shader glue,
DX12/SM6, Unreal Python graph creation, pixels, cooking, or performance.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import math
import sys
import unittest

from create_materials import MATERIAL_SPECS, normalize_asset_paths, shader_source


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def add(a, b):
    return tuple(x+y for x, y in zip(a, b))


def scale(a, s):
    return tuple(x*s for x in a)


def norm(a):
    return scale(a, 1.0/math.sqrt(dot(a, a)))


def uv_to_rh(u, v):
    lon, lat = (u-0.5)*2*math.pi, (0.5-v)*math.pi
    return math.cos(lat)*math.cos(lon), math.cos(lat)*math.sin(lon), math.sin(lat)


def star_uv(ecliptic):
    x, y, z = ecliptic
    eps = math.radians(23.439291111)
    eq = (x, math.cos(eps)*y-math.sin(eps)*z, math.sin(eps)*y+math.cos(eps)*z)
    return ((math.pi-math.atan2(eq[1], eq[0]))/(2*math.pi)) % 1, math.acos(eq[2])/math.pi


def ray_sphere(ro, rd, radius):
    t = -dot(ro, rd)
    closest = add(ro, scale(rd, t))
    d = radius*radius-dot(closest, closest)
    if d < 0:
        return math.inf, -math.inf
    chord = math.sqrt(d)
    return t-chord, t+chord


def atmosphere_interval(ro, rd, radius=1.0, outer=1.015696):
    if math.sqrt(dot(ro, ro)) < radius-1e-5:
        return None
    entry, exit_ = ray_sphere(ro, rd, outer)
    ground, _ = ray_sphere(ro, rd, radius)
    entry = max(entry, 0.0)
    if ground >= 0:
        exit_ = min(exit_, ground)
    return (entry, exit_) if exit_ > entry else None


def sphere_visibility(p, sun, center, radius, angular_radius=0.00465):
    if radius <= 0:
        return 1.0
    to_center = add(center, scale(p, -1))
    along = dot(to_center, sun)
    if along <= 0:
        return 1.0
    distance = math.sqrt(dot(add(to_center, scale(sun, -along)), add(to_center, scale(sun, -along))))
    edge = max(along*max(angular_radius, 0), 1e-5)
    t = min(max((distance-(radius-edge))/(2*edge), 0), 1)
    return t*t*(3-2*t)


def ring_slab(tau, mu, mu0, same_side):
    if same_side:
        return mu0/(mu0+mu)*(1-math.exp(-min(tau*(1/mu0+1/mu), 80)))
    delta = 1/mu-1/mu0
    if abs(delta) < 0.001:
        return tau/mu*math.exp(-min(tau/mu, 80))
    return (math.exp(-min(tau/mu0, 80))-math.exp(-min(tau/mu, 80)))/(mu*delta)


def terrain_mask(n, direction, cap_cos, enabled, body_type):
    return 0.0 if (0.5 < body_type < 1.5 and enabled > 0.5
                   and max(-1, min(1, dot(n, norm(direction)))) > max(-1, min(1, cap_cos))) else 1.0


class MaterialInvariantTests(unittest.TestCase):
    def assert_vec(self, actual, expected, places=11):
        for a, b in zip(actual, expected):
            self.assertAlmostEqual(a, b, places=places)

    def test_sphere_cardinals_and_ue_handedness(self):
        for uv, rh in (((0.5, 0.5), (1, 0, 0)), ((0.75, 0.5), (0, 1, 0)),
                       ((0.25, 0.5), (0, -1, 0)), ((0.5, 0), (0, 0, 1))):
            actual = uv_to_rh(*uv)
            self.assert_vec(actual, rh)
            ue = actual[0], -actual[1], actual[2]
            self.assert_vec(ue, (rh[0], -rh[1], rh[2]))

    def test_sphere_longitude_seam_is_continuous(self):
        for v in (0, 0.2, 0.5, 0.9, 1):
            self.assert_vec(uv_to_rh(0, v), uv_to_rh(1, v))

    def test_star_reference_cardinals_not_mirrored(self):
        self.assert_vec(star_uv((1, 0, 0)), (0.5, 0.5))  # RA 0
        eps = math.radians(23.439291111)
        # Equatorial RA +90 transformed to ecliptic before shader rotation.
        self.assert_vec(star_uv((0, math.cos(eps), -math.sin(eps))), (0.25, 0.5))
        self.assert_vec(star_uv((0, -math.cos(eps), math.sin(eps))), (0.75, 0.5))
        self.assert_vec(star_uv((0, math.sin(eps), math.cos(eps))), (0.5, 0.0))

    def test_ray_sphere_front_back_inside_tangent_miss(self):
        self.assert_vec(ray_sphere((3, 0, 0), (-1, 0, 0), 1), (2, 4))
        self.assert_vec(ray_sphere((3, 0, 0), (1, 0, 0), 1), (-4, -2))
        self.assert_vec(ray_sphere((0, 0, 0), (1, 0, 0), 1), (-1, 1))
        self.assert_vec(ray_sphere((3, 1, 0), (-1, 0, 0), 1), (3, 3))
        self.assertEqual(ray_sphere((3, 1.001, 0), (-1, 0, 0), 1), (math.inf, -math.inf))

    def test_ray_sphere_distant_proxy_and_unit_invariance(self):
        self.assert_vec(ray_sphere((100000, 0.25, 0), (-1, 0, 0), 1),
                        (100000-math.sqrt(0.9375), 100000+math.sqrt(0.9375)), places=6)
        # Lighting normalized position is independent of rendered centimeter radius.
        camera_m, radius_m = (19113000, 0, 0), 6371000
        normalized = scale(camera_m, 1/radius_m)
        for rendered_radius in (100, 10000, 637100000):
            proxy_camera = scale(normalized, rendered_radius)
            self.assert_vec(scale(proxy_camera, 1/rendered_radius), normalized)

    def test_atmosphere_does_not_integrate_far_side_through_body(self):
        hit = atmosphere_interval((3, 0, 0), (-1, 0, 0))
        self.assertAlmostEqual(hit[1], 2)
        self.assertLess(hit[1]-hit[0], 0.016)
        limb = atmosphere_interval((3, 1.008, 0), (-1, 0, 0))
        self.assertGreater(limb[1]-limb[0], 0.1)

    def test_atmosphere_inside_outside_and_below_ground(self):
        above = atmosphere_interval((1.001, 0, 0), (1, 0, 0))
        self.assertAlmostEqual(above[0], 0)
        down = atmosphere_interval((1.001, 0, 0), (-1, 0, 0))
        self.assertAlmostEqual(down[1], 0.001)
        self.assertIsNone(atmosphere_interval((0.9, 0, 0), (1, 0, 0)))
        self.assertIsNone(atmosphere_interval((3, 0, 0), (1, 0, 0)))

    def test_eclipse_disabled_behind_full_and_soft_edge(self):
        p, l = (0, 0, 0), (1, 0, 0)
        self.assertEqual(sphere_visibility(p, l, (5, 0, 0), 0), 1)
        self.assertEqual(sphere_visibility(p, l, (-5, 0, 0), 1), 1)
        self.assertEqual(sphere_visibility(p, l, (5, 0, 0), 1), 0)
        self.assertEqual(sphere_visibility(p, l, (5, 2, 0), 1), 1)
        self.assertAlmostEqual(sphere_visibility(p, l, (5, 1, 0), 1), 0.5)

    def test_saturn_shadow_tracks_sun(self):
        self.assertEqual(sphere_visibility((-1.8, 0, 0), (1, 0, 0), (0, 0, 0), 1), 0)
        self.assertEqual(sphere_visibility((1.8, 0, 0), (1, 0, 0), (0, 0, 0), 1), 1)
        self.assertEqual(sphere_visibility((0, 1.8, 0), (0, -1, 0), (0, 0, 0), 1), 0)

    def test_ring_opacity_view_angle_and_slab_equal_angle_limit(self):
        tau = -math.log(1-0.4)
        self.assertAlmostEqual(1-math.exp(-tau), 0.4)
        self.assertGreater(1-math.exp(-tau/0.1), 0.99)
        equal = ring_slab(tau, 0.5, 0.5, False)
        for delta in (-0.0003, 0.0003):
            self.assertAlmostEqual(ring_slab(tau, 0.5+delta, 0.5, False), equal, places=3)
        for mu in (0.002, 0.1, 0.5, 1):
            for mu0 in (0.002, 0.1, 0.5, 1):
                for side in (True, False):
                    value = ring_slab(tau, mu, mu0, side)
                    self.assertTrue(math.isfinite(value))
                    self.assertGreaterEqual(value, 0)

    def test_material_inputs_reject_absent_imports(self):
        with self.assertRaises(ValueError):
            normalize_asset_paths({})
        paths = {key: "/Game/Test/"+key for key in
                 ("earth_day", "earth_night", "moon_albedo", "saturn_body", "saturn_rings", "stars")}
        self.assertEqual(normalize_asset_paths(paths), paths)

    def test_lunar_terrain_hole_inside_outside_and_boundary(self):
        direction = (1, 0, 0)
        cap = math.cos(math.radians(2))
        self.assertEqual(terrain_mask((1, 0, 0), direction, cap, 1, 1), 0)
        self.assertEqual(terrain_mask((0, 1, 0), direction, cap, 1, 1), 1)
        self.assertEqual(terrain_mask((-1, 0, 0), direction, cap, 1, 1), 1)
        edge = (cap, math.sin(math.radians(2)), 0)
        self.assertEqual(terrain_mask(edge, direction, cap, 1, 1), 1)
        inner = (math.cos(math.radians(1.9)), math.sin(math.radians(1.9)), 0)
        outer = (math.cos(math.radians(2.1)), math.sin(math.radians(2.1)), 0)
        self.assertEqual(terrain_mask(inner, direction, cap, 1, 1), 0)
        self.assertEqual(terrain_mask(outer, direction, cap, 1, 1), 1)

    def test_lunar_hole_disabled_and_non_moon_stay_opaque(self):
        for body_type in (0, 1, 2):
            self.assertEqual(terrain_mask((1, 0, 0), (1, 0, 0), 0.9, 0, body_type), 1)
        for body_type in (0, 2):
            self.assertEqual(terrain_mask((1, 0, 0), (1, 0, 0), 0.9, 1, body_type), 1)
        self.assertEqual(terrain_mask((1, 0, 0), (1, 0, 0), 1, 1, 1), 1)


def standalone_source(spec, normal=False):
    """Keep texture objects as function arguments, as UE Custom expression glue does."""
    filename = "SurfaceNormal.ush" if normal else spec["shader"]
    parameters = [("float2", "UV", "pin.UV")]
    if spec.get("detail_uv"):
        parameters.append(("float2", "DetailUV", "pin.DetailUV"))
    if spec.get("clast_uv"):
        parameters.extend((("float2", "ClastUV", "float2(saturate(pin.UV.x),pin.DetailUV.x)"),
                           ("float2", "ClastVariation", "pin.DetailUV")))
    if spec.get("camera_vector"):
        parameters.append(("float3", "ViewToCamera", "float3(0.0,0.0,1.0)"))
    if spec.get("face_sign"):
        parameters.append(("float", "FaceSign", "pin.Front ? 1.0 : -1.0"))
    if spec.get("eye_exposure"):
        parameters.append(("float", "EyeExposure", "EyeExposure"))
    for name in spec.get("scalars", {}):
        parameters.append(("float", name, name))
    for name in spec.get("vectors", {}):
        parameters.append(("float3", name, name))
    for name in spec.get("textures", {}):
        parameters.append(("Texture2D", name, name))
        parameters.append(("SamplerState", name+"Sampler", name+"Sampler"))
    if normal:
        parameters = [p for p in parameters if p[1] in {"DetailUV", "DetailScale", "NormalStrength", "MesoScale", "MesoStrength", "ClastUV", "ClastStyleStrength", "MesoBlendStrength"}]
        parameters.append(("float3", "DetailNormal", "float3(0.0,0.0,1.0)"))
        parameters.append(("float3", "MesoNormal", "float3(0.0,0.0,1.0)"))
        parameters.append(("float3", "MesoNormalAlternate", "float3(0.0,0.0,1.0)"))
    decl = []
    for kind, name, value in parameters:
        if value == name:
            decl.append(f"{kind} {name};")
    n = 3 if normal else spec["output"]
    lines = ["#define Texture2DSample(T,S,U) T.Sample(S,U)",
             "#define Texture2DSampleLevel(T,S,U,L) T.SampleLevel(S,U,L)", *decl,
             ("struct PixelInput { float4 Position : SV_Position; float2 UV : TEXCOORD0;"
              "float2 DetailUV : TEXCOORD1; bool Front : SV_IsFrontFace; };"),
             f"float{n} StarCustom("+", ".join(f"{k} {p}" for k, p, _ in parameters)+") {",
             shader_source(filename, spec.get("defines", ())), "}",
             "float4 main(PixelInput pin) : SV_Target {",
             f"float{n} value = StarCustom("+", ".join(v for _, _, v in parameters)+");",
             "return float4(value,1.0);" if n == 3 else "return value;", "}"]
    return "\n".join(lines)


def _blob_bytes(blob):
    if not blob:
        return b""
    vtable = ctypes.cast(blob, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
    get_ptr = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vtable[3])
    get_size = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vtable[4])
    return ctypes.string_at(get_ptr(blob), get_size(blob))


def _release(blob):
    if blob:
        vtable = ctypes.cast(blob, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
        ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(vtable[2])(blob)


def compile_hlsl():
    if sys.platform != "win32":
        raise RuntimeError("Standalone compilation requires Windows d3dcompiler_47.dll")
    compile_ = ctypes.WinDLL("d3dcompiler_47.dll").D3DCompile
    compile_.restype = ctypes.c_long
    compile_.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_void_p,
                        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint,
                        ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_void_p)]
    results = []
    inputs = [(name, spec, False) for name, spec in MATERIAL_SPECS.items()]
    inputs.append(("M_Sun", {"shader": "Sun.ush", "output": 3,
        "scalars": {"SunRadiance": 18000000.0, "EarthRadiusMeters": 6371000.0},
        "vectors": {name: (0.0,0.0,1.0) for name in (
            "CameraVector", "SurfaceNormal", "EarthCameraLocal", "EarthAxisX", "EarthAxisY", "EarthAxisZ")}}, False))
    inputs.append(("M_SurfaceNormal", MATERIAL_SPECS["M_Surface"], True))
    photo_spec=dict(MATERIAL_SPECS["M_Surface"],shader="SurfacePhoto.ush")
    inputs.append(("M_SurfacePhoto",photo_spec,False))
    for name, spec, normal in inputs:
        source = standalone_source(spec, normal).encode("utf-8")
        code, error = ctypes.c_void_p(), ctypes.c_void_p()
        # D3DCOMPILE_ENABLE_STRICTNESS; optimizer enabled. No GPU/device is created.
        status = compile_(source, len(source), name.encode("ascii"), None, None, b"main",
                          b"ps_5_0", 1 << 11, 0, ctypes.byref(code), ctypes.byref(error))
        try:
            result = {"shader": name, "profile": "ps_5_0", "pass": status >= 0,
                      "bytecode_bytes": len(_blob_bytes(code)),
                      "messages": _blob_bytes(error).decode("utf-8", errors="replace").strip()}
            results.append(result)
        finally:
            _release(code)
            _release(error)
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--compile-hlsl", action="store_true")
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(MaterialInvariantTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    compiled = compile_hlsl() if args.compile_hlsl else []
    if compiled:
        print(json.dumps({"standalone_hlsl": compiled,
                          "unreal_shader_compiled": False,
                          "unreal_python_executed": False}, indent=2))
    sys.exit(0 if result.wasSuccessful() and all(r["pass"] for r in compiled) else 1)
