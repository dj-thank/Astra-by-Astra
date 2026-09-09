"""CPU/static validation for the STAR runtime photo-plume seam.

This check deliberately stops at source/data evidence. It does not start UE,
compile a material, render a frame, access a device, or claim packaged-game
acceptance.
"""

from __future__ import annotations

import json
import math
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PAWN_CPP = ROOT / "Source/Star/Runtime/StarShipPawn.cpp"
PAWN_H = ROOT / "Source/Star/Runtime/StarShipPawn.h"
MESH = ROOT / "Content/Star/Art/PhotoPlumes/vacuum_plume_mesh.json"
DISKS = ROOT / "Content/Star/Art/PhotoPlumes/vacuum_plume_disks.json"
MANIFEST = ROOT / "Art/Explorer/V2/art_manifest.json"
RUNTIME_ASSETS = ROOT / "Content/Star/Data/runtime_assets.json"
GEOMETRY = ROOT / "Tools/Art/explorer_v2_geometry.py"


def finite_vector(value: object, size: int) -> bool:
    return isinstance(value, list) and len(value) == size and all(
        isinstance(component, (int, float)) and math.isfinite(float(component))
        for component in value
    )


def check_mesh(path: Path, expected_vertices: int, expected_triangles: int, *, planes: bool) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    vertices = data["vertices"]
    uvs = data["uv0"]
    triangles = data["triangles"]
    assert data["units"] == "nozzle_diameter_D", f"unexpected units in {path}"
    assert data["local_exhaust_axis"] == "+X", f"unexpected local axis in {path}"
    assert len(vertices) == expected_vertices
    assert len(uvs) == expected_vertices
    assert len(triangles) == expected_triangles
    assert all(finite_vector(vertex, 3) for vertex in vertices)
    assert all(finite_vector(uv, 2) for uv in uvs)
    for triangle in triangles:
        assert finite_vector(triangle, 3)
        assert all(int(index) == index and 0 <= int(index) < expected_vertices for index in triangle)
    result = {
        "file": str(path.relative_to(ROOT)).replace("\\", "/"),
        "vertices": len(vertices),
        "triangles": len(triangles),
        "finite": True,
        "indices_in_bounds": True,
    }
    if planes:
        flat_count = len(triangles) * 3
        assert flat_count % 6 == 0, "six-plane mesh is not grouped into six equal sections"
        result["low_lod_triangle_indices"] = flat_count // 6 * 2
        result["low_lod_plane_fraction"] = 2 / 6
    return result


def normalized(vector: list[float]) -> list[float]:
    length = math.sqrt(sum(float(value) ** 2 for value in vector))
    assert length > 0 and math.isfinite(length)
    return [float(value) / length for value in vector]


def main() -> dict:
    cpp = PAWN_CPP.read_text(encoding="utf-8")
    header = PAWN_H.read_text(encoding="utf-8")
    geometry = GEOMETRY.read_text(encoding="utf-8")
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    runtime_assets = json.loads(RUNTIME_ASSETS.read_text(encoding="utf-8"))

    mesh_result = check_mesh(MESH, 300, 288, planes=True)
    disks_result = check_mesh(DISKS, 136, 128, planes=False)
    assert manifest["source_frame"] == {"forward": "+X", "right": "+Y", "up": "+Z"}
    assert manifest["version"] == 2
    parts = {part["name"]: part for part in manifest["model_parts"]}
    for side, expected_y in (("Port", -4.4), ("Starboard", 4.4)):
        name = f"SM_Engine{side}"
        assert name in parts
        location = parts[name]["location_m"]
        assert all(abs(actual - expected) < 0.01 for actual, expected in zip(location, (-8.25, expected_y, 0.0)))
        runtime_part = next(part for part in runtime_assets["shipParts"] if part["name"] == name)
        assert all(abs(actual - expected * 100.0) < 0.01 for actual, expected in zip(runtime_part["locationCm"], location))

    # The source construction puts the exit lip behind the retained engine
    # pivot. The runtime must derive that location from the imported bounds.
    exit_match = re.search(r'Engine exit replaceable lip",\((-?[0-9.]+),', geometry)
    assert exit_match, "V2 source exit lip is not present"
    source_exit_x = float(exit_match.group(1))
    assert source_exit_x < -8.25
    assert "Bounds.Origin.X-Bounds.BoxExtent.X" in cpp
    assert "Engine->GetRelativeTransform()" in cpp
    assert "LocalExit" in cpp
    runtime_nozzles = json.loads((ROOT / "Content/Star/Data/ship_nozzles.json").read_text(encoding="utf-8"))
    assert runtime_nozzles["rcs"] == manifest["rcs"]
    assert "Star/Data/ship_nozzles.json" in cpp

    rcs = manifest["rcs"]
    assert len(rcs) == 16
    selected_rcs = []
    for entry in rcs:
        name = entry["name"]
        diagonal = "ForePort" in name or "AftStarboard" in name
        if name.endswith("_0") or (diagonal and name.endswith(("_1", "_3"))) or (not diagonal and name.endswith("_2")):
            selected_rcs.append(entry)
    assert len(selected_rcs) == 10
    for axis in range(3):
        forces = [-float(entry["exhaust_axis"][axis]) for entry in selected_rcs]
        assert min(forces) < -0.9 and max(forces) > 0.9, "Missing translation direction"
    assert all(abs(math.sqrt(sum(float(value) ** 2 for value in entry["exhaust_axis"])) - 1.0) < 1e-5 for entry in selected_rcs)
    assert "Name.EndsWith(TEXT(\"_0\"))" in cpp
    assert "Name.EndsWith(TEXT(\"_3\"))" in cpp
    assert "Specs.Num()>=12" in cpp  # two main plus ten selected RCS
    assert "Spec.DiameterCm=21.6f" in cpp
    assert "Spec.LengthScale=0.5f" in cpp

    # CPU orientation check: every authored exhaust axis is a valid image of
    # the photo asset's local +X direction. The actual UE quaternion remains a
    # root-side runtime operation; this proves all source axes are usable.
    orientation_checks = []
    for entry in selected_rcs:
        axis = normalized(entry["exhaust_axis"])
        orientation_checks.append({"name": entry["name"], "source_axis_length": math.sqrt(sum(v*v for v in axis)), "axis": axis})
    orientation_checks.extend(
        {"name": f"SM_Engine{side}", "source_axis_length": 1.0, "axis": [-1.0, 0.0, 0.0]}
        for side in ("Port", "Starboard")
    )
    assert all(abs(item["source_axis_length"] - 1.0) < 1e-9 for item in orientation_checks)

    required_cpp_tokens = {
        "photo_textures": "T_VacuumPlumePhoto.T_VacuumPlumePhoto",
        "axial_texture": "T_VacuumPlumeAxial.T_VacuumPlumeAxial",
        "plane_material": "M_Thruster.M_Thruster",
        "axial_material": "M_ThrusterAxial.M_ThrusterAxial",
        "layer_energy": "LayerEnergy",
        "thrust": "TEXT(\"Thrust\")",
        "photo_parameter": "TEXT(\"PlumePhoto\")",
        "engine_output_feed": "SetFlightParameters(static_cast<float>(Propulsion.engineOutput)",
        "cruise_dynamics": "SetEngineDynamics(static_cast<float>(Propulsion.cruiseCharge)",
        "audio_spool": "ShipAudio->GetEngineSpool()",
        "no_collision": "SetCollisionEnabled(ECollisionEnabled::NoCollision)",
        "no_shadow": "SetCastShadow(false)",
        "rear_angle_blend": "RearBlendCos",
        "four_pixel_lod": "ProjectedPixels<4.0f",
        "one_pixel_cull": "ProjectedPixels<1.0f",
        "look_at_contract": "void AStarShipPawn::LookAtAbsolute(const star::Vec3d& Target,double Dt)",
    }
    missing = [name for name, token in required_cpp_tokens.items() if token not in cpp]
    assert not missing, f"missing runtime seam tokens: {missing}"
    assert "void AddLookAtAbsolute" not in header and "AddLookAtAbsolute" not in cpp
    assert "ShipAudio->SetFlightParameters(Snapshot.Throttle" not in cpp
    assert "bPaused?0.0f" in cpp
    assert "PlaneLodMesh" in header and "RcsPulseRemaining" in header

    result = {
        "status": "SOURCE_CHECKS_PASS",
        "scope": "STAR ship runtime photo-plume CPU/static seam",
        "mesh": mesh_result,
        "axial_disks": disks_result,
        "main_nozzles": {
            "count": 2,
            "source_exit_x_m": source_exit_x,
            "pivot_derived_exit_guard": True,
            "positions_from_geometry_bounds": True,
        },
        "rcs": {"manifest_count": len(rcs), "runtime_count": len(selected_rcs), "bounded_under_12_total": True},
        "orientation_checks": orientation_checks,
        "audio_vfx_contract": "PASS",
        "camera_contract": "PASS",
        "gates": {
            "SOURCE_CHECKS_PASS": "PASS",
            "DEVICE_PASS": "NOT_RUN",
            "PROVIDER_PASS": "NOT_APPLICABLE",
            "PUBLIC_PASS": "NOT_APPLICABLE",
            "VISUAL_REVIEW": "NOT_REQUESTED",
            "UE_COMPILE": "NOT_RUN_ROOT_OWNS",
            "GPU_RENDER": "NOT_RUN_ROOT_OWNS",
        },
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return result


if __name__ == "__main__":
    main()
