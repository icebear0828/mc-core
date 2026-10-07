from __future__ import annotations
from pathlib import Path
import pytest
import sys

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

from bake_assets import bake_sekiro, SUPPORTED_TARGETS


import unittest
import tempfile
import shutil

class TestSekiroAssetBaking(unittest.TestCase):
    def test_sekiro_in_supported_targets(self):
        self.assertIn("sekiro", SUPPORTED_TARGETS)

    def test_bake_sekiro_stages_correct_assets(self):
        source_dir = Path(__file__).parent.parent / "assets" / "source"
        with tempfile.TemporaryDirectory() as tmp_dir:
            output_dir = Path(tmp_dir) / "cooked"

            success = bake_sekiro(source_dir, output_dir)
            self.assertTrue(success)

            sekiro_dir = output_dir / "sekiro"
            self.assertTrue(sekiro_dir.exists())

            parts_dir = sekiro_dir / "parts"
            map_dir = sekiro_dir / "map"
            self.assertTrue(parts_dir.exists())
            self.assertTrue(map_dir.exists())

            # Verify 12 Steve parts + texture are staged in parts
            staged_parts = list(parts_dir.glob("steve*.obj"))
            self.assertEqual(len(staged_parts), 12)
            self.assertTrue((parts_dir / "steve.png").exists())

            # Verify blocks & weapons are staged in map
            self.assertTrue((map_dir / "stone.obj").exists())
            self.assertTrue((map_dir / "diamond_sword.obj").exists())

            # Verify HUD atlas is staged in sekiro_dir
            self.assertTrue((sekiro_dir / "mc_hud_atlas.png").exists())


if __name__ == "__main__":
    unittest.main()
