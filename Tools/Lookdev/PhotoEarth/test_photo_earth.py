import unittest

from build import EXPECTED_OLD_8K_SHA256, EXPECTED_RAW_SHA256, OLD_8K, RAW, sha256


class PhotoEarthSourceTest(unittest.TestCase):
    def test_pinned_inputs(self):
        self.assertEqual(sha256(RAW), EXPECTED_RAW_SHA256)
        self.assertEqual(sha256(OLD_8K), EXPECTED_OLD_8K_SHA256)


if __name__ == "__main__":
    unittest.main()
