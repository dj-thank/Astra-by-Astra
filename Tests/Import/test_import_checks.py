"""Contracts that can be verified without claiming an Unreal import passed."""
from copy import deepcopy
from pathlib import Path
import sys
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools/Unreal"))
import import_checks as checks


class RuntimeContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.art = checks.read_json(ROOT / "Art/Explorer/V2/art_manifest.json")

    def test_meters_to_cm_without_astronomical_frame_mirror(self):
        runtime = checks.runtime_assets(self.art)
        self.assertEqual(runtime["sockets"]["cockpitCameraCm"], [600.0, 0.0, 130.0])
        self.assertEqual(runtime["sockets"]["chaseCameraCm"], [-2400.0, 0.0, 900.0])
        port = next(p for p in runtime["shipParts"] if p["name"] == "SM_GearPort")
        self.assertLess(port["locationCm"][1], 0)
        self.assertEqual(port["asset"], "/Game/Star/Art/ExplorerV2/Parts/SM_GearPort.SM_GearPort")

    def test_only_three_articulated_assemblies_are_gear(self):
        runtime = checks.runtime_assets(self.art)
        self.assertEqual({p["name"] for p in runtime["shipParts"] if p["role"] == "gear"},
                         {"SM_GearNose", "SM_GearPort", "SM_GearStarboard"})
        self.assertTrue(all(p["role"] != "gear" for p in runtime["shipParts"] if "GearBays" in p["name"]))

    def test_center_flight_instrument_first_and_dimensions_remain_meters(self):
        displays = checks.runtime_assets(self.art)["instrumentDisplays"]
        self.assertEqual(len(displays), 3)
        self.assertEqual(displays[0]["locationCm"][1], 0)
        self.assertEqual(displays[0]["widthM"], .619)
        self.assertEqual(displays[0]["heightM"], .312)

    def test_duplicate_and_unbaked_meshes_rejected(self):
        art = deepcopy(self.art)
        art["model_parts"][1]["name"] = art["model_parts"][0]["name"]
        with self.assertRaises(ValueError):
            checks.runtime_assets(art)
        art = deepcopy(self.art)
        art["model_parts"][0]["geometry_rotation_and_scale_baked"] = False
        with self.assertRaises(ValueError):
            checks.runtime_assets(art)

    def test_nonfinite_locations_rejected(self):
        art = deepcopy(self.art)
        art["model_parts"][0]["location_m"][0] = float("nan")
        with self.assertRaises(ValueError):
            checks.runtime_assets(art)

    def test_manifest_path_cannot_escape_source_root(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            child = base / "allowed"
            child.mkdir()
            (base / "outside.wav").write_bytes(b"source")
            for value in ("../outside.wav", str(base / "outside.wav"), "C:/outside.wav", "..\\outside.wav"):
                with self.subTest(value=value), self.assertRaises(ValueError):
                    checks.source_path(child, value)


class WavContractTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "stereo.wav"
        with wave.open(str(self.path), "wb") as stream:
            stream.setnchannels(2)
            stream.setsampwidth(2)
            stream.setframerate(48000)
            stream.writeframes(b"\x00\x00\x00\x00" * 480)
        self.cue = {"id": "SFX_Cabin", "loop": True, "channels": 2, "sample_rate": 48000, "bit_depth": 16}

    def tearDown(self):
        self.directory.cleanup()

    def test_stereo_cabin_sfx_is_preserved(self):
        actual = checks.inspect_wav(self.path, self.cue)
        self.assertEqual(actual["channels"], 2)
        self.assertAlmostEqual(actual["duration_seconds"], .01)

    def test_channel_and_rate_mismatch_are_not_silently_converted(self):
        for key, value in (("channels", 1), ("sample_rate", 44100), ("bit_depth", 24)):
            cue = dict(self.cue, **{key: value})
            with self.subTest(key=key), self.assertRaises(ValueError):
                checks.inspect_wav(self.path, cue)

    def test_string_false_is_not_truthy_looping(self):
        with self.assertRaises(ValueError):
            checks.inspect_wav(self.path, dict(self.cue, loop="false"))


if __name__ == "__main__":
    unittest.main()
