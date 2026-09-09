"""Dedicated dedicated editor entry point for the integrated v0.2 content pass."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import apply_audio_v2
import apply_photo_pass

apply_audio_v2.run()
apply_photo_pass.run()
