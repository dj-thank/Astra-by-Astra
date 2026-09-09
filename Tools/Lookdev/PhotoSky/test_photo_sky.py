import unittest

from build import (ANCHORS, galactic_from_radec, photo_uv_from_galactic,
                   validate_rotation_and_anchors)


class PhotoSkyRegistrationTest(unittest.TestCase):
    def test_spice_matrix_anchor_solution(self):
        result = validate_rotation_and_anchors()
        self.assertEqual(set(result["anchors"]), set(ANCHORS))
        for anchor in result["anchors"].values():
            self.assertGreaterEqual(anchor["photoUv"][0], 0.0)
            self.assertLess(anchor["photoUv"][0], 1.0)
            self.assertGreaterEqual(anchor["photoUv"][1], 0.0)
            self.assertLessEqual(anchor["photoUv"][1], 1.0)

    def test_anchor_longitudes_wrap(self):
        longitude, latitude = galactic_from_radec(ANCHORS["LMC"]["raDeg"], ANCHORS["LMC"]["decDeg"])
        u, v = photo_uv_from_galactic(longitude, latitude)
        self.assertAlmostEqual(longitude, ANCHORS["LMC"]["expectedGalacticLDeg"], places=2)
        self.assertAlmostEqual(latitude, ANCHORS["LMC"]["expectedGalacticBDeg"], places=2)
        self.assertTrue(0.0 <= u < 1.0)
        self.assertTrue(0.0 <= v <= 1.0)


if __name__ == "__main__":
    unittest.main()
