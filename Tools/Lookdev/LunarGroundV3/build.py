"""Build an estimated mesoscale residual from an actual Apollo17 ground photograph.

Source pixels are retained separately. Single-image illumination removal and
normal inference are uncertain; neither height nor geographic placement is known.
No UE process is started. Pass the exact downloaded NASA/ALSJ photograph.
"""
from pathlib import Path
import argparse
import hashlib
import json
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter, minimum_filter

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "Content/Star/Art/LunarGroundV3"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, values):
    Image.fromarray(np.uint8(np.rint(np.clip(values, 0, 1)*255))).save(path)


def build(source):
    OUT.mkdir(parents=True, exist_ok=True)
    original = Image.open(source).convert("RGB")
    if original.size != (2340, 2350):
        raise ValueError("Expected unresized AS17-147-22501HR.jpg (2340x2350)")
    box = (980, 450, 2004, 1474)
    crop = original.crop(box)
    crop.save(OUT / "AS17-147-22501_observed_crop.png")
    rgb = np.asarray(crop, dtype=np.float64)/255
    gray = rgb @ np.array([.2126, .7152, .0722])
    linear = np.where(gray <= .04045, gray/12.92, ((gray+.055)/1.055)**2.4)
    log = np.log(np.maximum(linear, .003))
    residual = log-gaussian_filter(log, 24)
    # Exclude strong shadows/highlights rather than baking them as reflectance.
    confidence = np.clip((gray-.14)/.16, 0, 1)*np.clip((.88-gray)/.18, 0, 1)
    confidence *= 1-np.clip((np.abs(residual)-.28)/.40, 0, 1)
    confidence = minimum_filter(confidence, size=7)
    # Reseau cross centers were visually located in this NASA scan. Reject their
    # thin horizontal/vertical arms in the chosen crop; do not reconstruct pixels.
    yy, xx = np.mgrid[box[1]:box[3], box[0]:box[2]]
    for cx, cy in ((1180,710), (1635,708), (1183,1167), (1638,1164)):
        cross = ((np.abs(xx-cx)<9)&(np.abs(yy-cy)<100)) | ((np.abs(yy-cy)<9)&(np.abs(xx-cx)<100))
        confidence[cross] = 0
    confidence = gaussian_filter(confidence, 3)
    y, x = np.mgrid[:1024, :1024]
    border = np.minimum.reduce([x, y, 1023-x, 1023-y])/52
    border = np.clip(border, 0, 1)
    border = border*border*(3-2*border)
    modulation = np.clip(residual*1.25, -.45, .45)*confidence*border
    # 2m square is an ART SCALE estimate; the oblique scan is not an orthophoto.
    # Tangent slopes come from a smoothed photo residual, not measured relief.
    relief = gaussian_filter(modulation, 2.0)*.008
    dy, dx = np.gradient(relief, 2.0/1024)
    slope = np.maximum(np.sqrt(dx*dx+dy*dy)/.22, 1)
    dx /= slope; dy /= slope
    normal = np.stack([-dx, dy, np.ones_like(dx)], -1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    save(OUT/"apollo17_meso_linear.png", np.stack([.5+.5*modulation, .5+.10*modulation, np.full_like(gray,.5)], -1))
    save(OUT/"apollo17_meso_normal_dx.png", normal*.5+.5)
    metadata = {
        "status": "SOURCE_ASSET_ONLY_GPU_REVIEW_PENDING",
        "source": {"image": "AS17-147-22501", "fileSha256": sha(source),
                   "page": "https://apollojournals.org/alsj/a17/images17.html#22501",
                   "credit": "NASA; original-film scan processing Kipp Teague / Apollo Lunar Surface Journal",
                   "observationDate": "1972-12-12", "cropPixels": list(box), "sourcePixels": list(original.size)},
        "classification": "photo-backed visual analog at Apollo17; NOT a georegistered patch or measured relief",
        "tileMeters": 2.0, "scaleEvidence": "Uncalibrated art estimate from oblique ground photograph; factor-of-two or larger uncertainty. No ruler/camera pose solution.",
        "photometry": "Display-coded grayscale -> estimated linear -> log high-pass sigma24px -> shadow/highlight/reseau rejection -> bounded residual. Not calibrated albedo; residual light direction may remain.",
        "normal": "Single image-gradient artistic slope estimate, capped 0.22; decoded UE normal sample required; no displacement/collision.",
        "roughness": "Artistic dusty surface variation, not measured BRDF",
        "registration": "Stable terrain UV1 tangent meters; source patch repeats as analog. Actual photo clast positions are not recovered.",
        "geometry": {"type": "unobserved reconstructed clasts", "siteLatLon": [20.1908,30.7717],
                     "radiusMeters": 96, "placement": "deterministic fixed lunar site; random seed1701972; 4m jittered cells, .27 occupancy",
                     "landing": "clast size fades smoothly from zero at12m to full at22m; no collision changes",
                     "size": "5.5-29.5cm horizontal radius before edge fade; inferred, not calibrated to scan"},
        "artifacts": {p.name: sha(p) for p in OUT.glob("*.png")}}
    (OUT/"provenance.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({"output": str(OUT), "modulation_std": float(modulation.std()), "normal_slope_max": float(np.max(np.sqrt(dx*dx+dy*dy))) }))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    build(parser.parse_args().source)
