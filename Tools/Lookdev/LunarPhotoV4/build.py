"""Place actual Apollo 17 scan RGB in an ENU panoramic terrain projector.

CPU only. Source checkout is read-only; every write is under --output-root.
No new geometry, synthetic texture, inpainting, sharpening or de-light residual.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import time

import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import distance_transform_edt, gaussian_filter, gaussian_filter1d, map_coordinates, median_filter


TOP, BOTTOM = 25.0, -20.0
# Conservative far-mountain footlines in original 2340x2350 scans. These exclude
# visible nearby rocks, footprints, astronaut shadow, LM legs and LM shadow.
FOOT = {22493: 910, 22494: 830, 22495: 850, 22496: 760,
        22497: 760, 22498: 1580, 22500: 1500, 22502: 1620,
        22504: 1440, 22505: 1120, 22511: 790, 22513: 920,
        22514: 850, 22515: 690, 22517: 1040, 22518: 930, 22519: 1040}
# Full columns are excluded rather than reconstructing hidden terrain.
XRANGE = {22511: (650, 2280), 22514: (45, 2100), 22515: (45, 1700),
          22517: (1640, 2280), 22519: (420, 2280), 22493: (200, 2280)}
EXCLUDED = {22506: "sun flare", 22507: "direct Sun and flare",
            22508: "flare veil", 22509: "direct Sun and flare",
            22512: "flare veil"}


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def smooth(a, b, value):
    t = np.clip((value - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def rays_for(width, start, stop, height):
    az = (np.arange(width, dtype=np.float64) + .5) / width * 2 * np.pi
    el = np.deg2rad(TOP - (np.arange(start, stop) + .5) / height * (TOP - BOTTOM))
    ca, sa, ce, se = np.cos(az), np.sin(az), np.cos(el), np.sin(el)
    return np.stack([ce[:, None] * sa, ce[:, None] * ca,
                     np.broadcast_to(se[:, None], (stop-start, width))], axis=-1).astype(np.float32)


def image_sample(im, sx, sy):
    return np.stack([map_coordinates(im[..., k], [sy, sx], order=1, mode="nearest")
                     for k in range(3)], axis=-1).astype(np.float32)


def prepare_frames(source, cams, anchor):
    result = []
    for c in cams:
        frame = c["frame"]
        path = source / f"Content/Star/Art/LunarPanorama/Source/AS17-147-{frame}HR.jpg"
        im = np.asarray(Image.open(path).convert("RGB"))
        gray = median_filter(np.mean(im[::2, ::2], axis=-1).astype(np.float32), size=5)
        threshold = 12 if frame in (22505, 22511, 22513, 22514, 22515) else 26
        visible = gray[:810] > threshold
        skyline = np.argmax(visible, axis=0) * 2.0
        skyline[~np.any(visible, axis=0)] = 1800
        skyline = median_filter(skyline, size=13)
        # Vertical shifts of the camera bundle do not change original pixels.
        R = anchor @ np.array(c["R"])
        K = np.array(c["K"]) * 2
        K[2, 2] = 1
        x = np.arange(0, 2340, 2)
        p = np.stack([x, skyline, np.ones_like(x)], axis=1)
        rr = p @ np.linalg.inv(K).T @ R.T
        rr /= np.linalg.norm(rr, axis=1)[:, None]
        aza = np.rad2deg(np.arctan2(rr[:, 0], rr[:, 1])) % 360
        ele = np.rad2deg(np.arcsin(rr[:, 2]))
        left, right = XRANGE.get(frame, (45, 2280))
        good = (x > left + 25) & (x < right - 25) & (skyline > 35) & (skyline < FOOT.get(frame, 0)-40)
        shadow_confidence=None
        if frame in (22504,22505,22511,22513,22514,22515):
            # These frames contain actual deep photographic shadows or flare
            # veils. Preserve their lit RGB and suppress only the unresolved
            # dark part; never stretch it into a game-light shadow.
            # Its lighting and silhouette are unresolvable against the current
            # game light. Fade confidence; never brighten or replace the RGB.
            low=gaussian_filter(np.mean(im,axis=-1).astype(np.float32),sigma=35)
            # Keep only well-lit photographic terrain.  A low threshold here
            # would preserve the black valley as a false game surface.
            shadow_confidence=smooth(55,130,low).astype(np.float32)
        result.append(dict(frame=frame, image=im, R=R, K=K, skyline=skyline,shadowConfidence=shadow_confidence,
                           skylineAz=aza[good], skylineEl=ele[good],
                           safe=frame not in EXCLUDED and frame in FOOT,
                           left=left, right=right, foot=FOOT.get(frame, 0)))
    return result


def horizon_curve(frames, width):
    sums = np.zeros(width, np.float64)
    weights = np.zeros(width, np.float64)
    for f in frames:
        if not f["safe"]:
            continue
        az, el = f["skylineAz"], f["skylineEl"]
        if len(az) == 0:
            continue
        idx = np.floor(az / 360 * width).astype(int) % width
        np.add.at(sums, idx, el)
        np.add.at(weights, idx, 1)
    # Smooth only the sparse observed camera-registration support; no pixel fill.
    sm = gaussian_filter1d(sums, 4, mode="wrap")
    sw = gaussian_filter1d(weights, 4, mode="wrap")
    valid = sw > .025
    value = np.divide(sm, sw, out=np.zeros_like(sm), where=sw > 0)
    return value, valid


def project(frames, width, height, work):
    raw = np.memmap(work / "raw.rgb", mode="w+", dtype=np.uint8, shape=(height, width, 3))
    rgb = np.memmap(work / "safe.rgb", mode="w+", dtype=np.uint8, shape=(height, width, 3))
    alpha = np.memmap(work / "safe.alpha", mode="w+", dtype=np.uint8, shape=(height, width))
    support = np.memmap(work / "support.u16", mode="w+", dtype=np.uint16, shape=(height, width))
    for start in range(0, height, 96):
        stop = min(start+96, height)
        rays = rays_for(width, start, stop, height)
        shape = rays.shape[:2]
        rawsum = np.zeros((*shape, 3), np.float32)
        raww = np.zeros(shape, np.float32)
        colors = np.zeros((*shape, 3), np.float32)
        total = np.zeros(shape, np.float32)
        strongest = np.zeros(shape, np.float32)
        owner = np.zeros(shape, np.uint16)
        for f in frames:
            cam = rays @ f["R"]
            z = cam[..., 2]
            sx = (cam[..., 0] / np.maximum(z, 1e-8) * f["K"][0, 0] + f["K"][0, 2]).astype(np.float32)
            sy = (cam[..., 1] / np.maximum(z, 1e-8) * f["K"][1, 1] + f["K"][1, 2]).astype(np.float32)
            valid = (z > 0) & (sx > 30) & (sx < 2310) & (sy > 30) & (sy < 2300)
            yy, xx = np.nonzero(valid)
            if not len(yy):
                continue
            u, v = sx[valid], sy[valid]
            col = image_sample(f["image"], u, v)
            edge = np.minimum.reduce([u-30, 2310-u, v-30, 2300-v])
            weight = smooth(0, 320, edge) ** 3
            rawsum[valid] += col * weight[:, None]
            raww[valid] += weight
            if not f["safe"]:
                continue
            sky = np.interp(u, np.arange(0, 2340, 2), f["skyline"])
            mask = smooth(f["left"], f["left"]+90, u) * (1-smooth(f["right"]-90, f["right"], u))
            mask *= smooth(sky+1, sky+6, v) * (1-smooth(f["foot"]-100, f["foot"], v))
            w = weight * mask
            colors[valid] += col * w[:, None]
            total[valid] += w
            prior = strongest[valid]
            replace = w > prior
            owner[yy[replace], xx[replace]] = f["frame"]
            strongest[valid] = np.maximum(prior, w)
        raw[start:stop] = (rawsum / np.maximum(raww[..., None], 1e-7)).clip(0, 255).astype(np.uint8)
        rgb[start:stop] = (colors / np.maximum(total[..., None], 1e-7)).clip(0, 255).astype(np.uint8)
        alpha[start:stop] = (np.clip(total, 0, 1) * 255).astype(np.uint8)
        support[start:stop] = owner
        print(f"projected rows {stop}/{height}", flush=True)
    for a in (raw, rgb, alpha, support):
        a.flush()
    return raw, rgb, alpha, support


def intervals(mask):
    result = []
    edges = np.diff(np.r_[False, mask, False].astype(int))
    for a, b in zip(np.flatnonzero(edges == 1), np.flatnonzero(edges == -1)):
        result.append([round(a / len(mask)*360, 3), round(b/len(mask)*360, 3)])
    return result


def register(frames, measured, width, height, out, work):
    az = (np.arange(width) + .5) / width * 360
    dem = np.interp(az, measured["az"], measured["elevation"], period=360)
    distance = np.interp(az, measured["az"], measured["distance"], period=360)
    curves = []
    for f in frames:
        if not f["safe"] or len(f["skylineAz"]) < 4:
            continue
        center = np.degrees(np.arctan2(f["R"][0,2], f["R"][1,2])) % 360
        fa = (f["skylineAz"]-center+180)%360-180+center
        target = (az-center+180)%360-180+center
        order = np.argsort(fa)
        ph = np.interp(target, fa[order], f["skylineEl"][order])
        delta = ph-dem
        local_slope=np.abs(np.gradient(gaussian_filter1d(delta,width/360*.18,mode='wrap')))/(360/width)
        stable=local_slope<.65
        if f['frame'] in (22504,22505):
            # Threshold skyline detection drops through unlit ridge pixels.
            # Do not use the resulting 5-degree correction as a terrain warp.
            stable &= abs(delta)<1.5
        enabled = (target >= min(fa)) & (target <= max(fa)) & (abs(delta)<9) & (distance>1800) & stable
        fade = smooth(min(fa), min(fa)+1.0, target) * (1-smooth(max(fa)-1.0,max(fa),target)) * enabled
        # Feather into unobserved azimuth gaps from the supported side only.
        edge_pixels=distance_transform_edt(np.tile(enabled,3))[width:2*width]
        fade*=smooth(0,width/360*3.0,edge_pixels)
        curves.append((f,delta,fade))
    rgba = np.memmap(work / "registered.rgba", mode="w+", dtype=np.uint8, shape=(height, width, 4))
    ids = np.memmap(work / "registered.u16", mode="w+", dtype=np.uint16, shape=(height, width))
    for start in range(0, height, 96):
        stop = min(start+96, height)
        y = np.broadcast_to(np.arange(start, stop)[:, None], (stop-start, width)).astype(np.float32)
        el = TOP-(y+.5)/height*(TOP-BOTTOM)
        sumc = np.zeros((*y.shape,3),np.float32)
        sumw = np.zeros(y.shape,np.float32)
        strongest = np.zeros(y.shape,np.float32)
        owner = np.zeros(y.shape,np.uint16)
        for f,delta,fade in curves:
            E = np.deg2rad(el+delta[None,:])
            A = np.deg2rad(az)[None,:]
            rays = np.stack([np.cos(E)*np.sin(A), np.cos(E)*np.cos(A), np.sin(E)],axis=-1).astype(np.float32)
            cam = rays @ f["R"]
            z = cam[...,2]
            sx = (cam[...,0]/np.maximum(z,1e-8)*f["K"][0,0]+f["K"][0,2]).astype(np.float32)
            sy = (cam[...,1]/np.maximum(z,1e-8)*f["K"][1,1]+f["K"][1,2]).astype(np.float32)
            valid=(z>0)&(sx>f["left"])&(sx<f["right"])&(sy>30)&(sy<f["foot"])&(fade[None,:]>0)
            yy,xx=np.nonzero(valid)
            if not len(yy):
                continue
            u,v=sx[valid],sy[valid]
            sky=np.interp(u,np.arange(0,2340,2),f["skyline"])
            weight=smooth(f["left"],f["left"]+250,u)*(1-smooth(f["right"]-250,f["right"],u))
            weight=weight**3*smooth(sky+1,sky+5,v)*(1-smooth(f["foot"]-100,f["foot"],v))*fade[xx]
            if f['shadowConfidence'] is not None:
                weight*=map_coordinates(f['shadowConfidence'],[v,u],order=1,mode='nearest')
            col=image_sample(f["image"],u,v)
            sumc[valid]+=col*weight[:,None]
            sumw[valid]+=weight
            replace=weight>strongest[valid]
            owner[yy[replace],xx[replace]]=f["frame"]
            strongest[valid]=np.maximum(strongest[valid],weight)
        rgba[start:stop,:,:3]=(sumc/np.maximum(sumw[...,None],1e-7)).clip(0,255).astype(np.uint8)
        aa=np.clip(sumw,0,1)*smooth(-.15,.3,el)*smooth(-.03,.05,dem[None,:]-el)
        rgba[start:stop,:,3]=(aa*255).clip(0,255).astype(np.uint8)
        ids[start:stop]=owner
    rgba.flush()
    ids.flush()
    Image.fromarray(np.asarray(rgba)).save(out / "apollo17_far_hills_photo_rgba_16k.png", compress_level=4)
    Image.fromarray(np.asarray(rgba[..., 3])).save(out / "apollo17_far_hills_coverage_16k.png")
    Image.fromarray(np.asarray(ids)).save(out / "apollo17_far_hills_source_ids_16k.png")
    return rgba, dict(azimuthDegrees=az[::16].round(5).tolist(),
                      measuredSkylineElevationDegrees=dem[::16].round(5).tolist(),
                      measuredHorizonDistanceMeters=distance[::16].round(3).tolist(),
                      cameraVerticalShifts=[dict(frame=f['frame'],sourceMinusMeasuredElevationDegrees=delta[::16].round(5).tolist(),valid=(fade[::16]>0).tolist()) for f,delta,fade in curves])


def previews(raw, rgba, out, width, height):
    review = out / "Review"
    review.mkdir(exist_ok=True)
    Image.fromarray(np.asarray(raw)).resize((4096, height*4096//width), Image.Resampling.LANCZOS).save(review / "all_azimuth_original_photo.jpg", quality=94)
    rgb = np.asarray(rgba[..., :3])
    a = np.asarray(rgba[..., 3], dtype=np.float32)/255
    yy, xx = np.indices((height, width))
    check = (28 + ((xx//64 + yy//64) % 2)*16).astype(np.float32)
    comp = (rgb*a[..., None] + check[..., None]*(1-a[..., None])).clip(0, 255).astype(np.uint8)
    Image.fromarray(comp).resize((4096, height*4096//width), Image.Resampling.LANCZOS).save(review / "registered_far_hills_coverage.jpg", quality=94)
    horizon=read_json(out/'registration_curve.json')
    for name, az in [("north", 345), ("southwest", 245), ("east_missing", 90),('north_0_fov85',0),('east_90_fov85',90)]:
        w, h = 1920, 1080
        hfov = np.deg2rad(85 if 'fov85' in name else 65)
        px = ((np.arange(w)+.5)/w*2-1)*np.tan(hfov/2)
        py = (1-(np.arange(h)+.5)/h*2)*np.tan(hfov/2)*h/w
        X, Y = np.meshgrid(px, py)
        azimuth = np.deg2rad(az) + np.arctan2(X, 1)
        elev = np.arctan2(Y, np.sqrt(1+X*X)) + np.deg2rad(0 if 'fov85' in name else 4)
        u = (np.rad2deg(azimuth)%360)/360*width-.5
        v = (TOP-np.rad2deg(elev))/(TOP-BOTTOM)*height-.5
        full = np.stack([map_coordinates(raw[..., k], [v, u], order=1, mode="grid-wrap") for k in range(3)], axis=-1)
        Image.fromarray(full).save(review / f"{name}_photo_view.jpg", quality=96)
        layer = np.stack([map_coordinates(rgba[..., k], [v, u], order=1, mode="grid-wrap") for k in range(4)],axis=-1)
        layer[(v<0)|(v>=height),3]=0
        mask=layer[...,3:4].astype(np.float32)/255
        horizon_el=np.interp(np.rad2deg(azimuth)%360,horizon['azimuthDegrees'],horizon['measuredSkylineElevationDegrees'],period=360)
        # Show missing photo as neutral measured-horizon surface, not fake sky.
        backdrop=np.where(np.rad2deg(elev)[...,None]<=horizon_el[...,None],58,8).astype(np.float32)
        candidate=(layer[...,:3]*mask+backdrop*(1-mask)).clip(0,255).astype(np.uint8)
        Image.fromarray(candidate).save(review / f"{name}_registered_layer.jpg",quality=96)
    return review


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, default=Path.cwd())
    parser.add_argument("--width", type=int, default=16384)
    args = parser.parse_args()
    source, root = args.source_root.resolve(), args.output_root.resolve()
    out = root / "Content/Star/Art/LunarPhotoV4"
    work = root / "work/photo-v04"
    out.mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)
    print("PID", os.getpid(), "started", time.time(), flush=True)
    cams = read_json(source / "Data/lunar_panorama_full_cameras.json")
    registration = read_json(source / "Data/lunar_panorama_full_region_registration.json")
    anchor_cam = next(c for c in cams if c["frame"] == 22500)
    anchor_reg = next(c for c in registration["cameras"] if c["frame"] == 22500)
    anchor = np.array(anchor_reg["cameraToEastNorthUp"]) @ np.array(anchor_cam["R"]).T
    width, height = args.width, args.width//8
    frames = prepare_frames(source, cams, anchor)
    if (work / "raw.rgb").exists() and (work / "raw.rgb").stat().st_size == width*height*3:
        raw = np.memmap(work / "raw.rgb",mode="r",dtype=np.uint8,shape=(height,width,3))
    else:
        raw, _, _, _ = project(frames, width, height, work)
    measured_path = source / "Content/Star/Art/LunarPanorama/FullRegion/dem_horizon_full.npz"
    measured = np.load(measured_path)
    rgba, curve = register(frames, measured, width, height, out, work)
    write_json(out / "registration_curve.json", curve)
    previews(raw, rgba, out, width, height)
    sources = read_json(source / "Data/lunar_panorama_sources.json")
    records = []
    for s in sources["sources"]:
        actual = sha(source / s["path"])
        if actual != s["sha256"]:
            raise RuntimeError(f"Source bytes changed: {s['id']}")
        records.append(s)
    col_coverage = np.max(rgba[..., 3], axis=0) > 128
    recipe = dict(schemaVersion=1, status="LOCAL_PHOTO_DISPLAY_CANDIDATE_NOT_UE_VALIDATED",
                  primaryTexture="Content/Star/Art/LunarPhotoV4/apollo17_far_hills_photo_rgba_16k.png",
                  pixels=[width, height], angularDegreesPerPixel=360/width,
                  colorSpace="Original NASA scan RGB interpreted sRGB; straight, linear alpha. Baked sunlight, not albedo.",
                  processing=["Bilinear photo reprojection with narrow feathered overlap", "No image generation, inpaint, de-light, luminance residual, detail synthesis or new geometry", "Column-wise angular skyline registration to existing DEM; photographic colors retained", "Scan reseau marks retained in RGB; not claimed as rocks or terrain"],
                  registration=dict(kind="2D angular vertical skyline transfer, not surveyed photogrammetry", anchorCamera22500=anchor.tolist(), curvePath="Content/Star/Art/LunarPhotoV4/registration_curve.json", measuredHorizonSha256=sha(measured_path), maxAllowedVerticalShiftDegrees=9,maxShiftSlopeDegreesPerDegree=.65,shadowConfidenceFrames=[22504,22505,22511,22513,22514,22515],shadowedFrameMaxShiftDegrees=1.5,gapFadeDegrees=3, limitation="Horizon alignment is construction, not an independent fit validation; mountain footlines and hidden surfaces are not solved. Shadowed ridge dropouts are excluded, not stretched."),
                  projector=dict(cameraOrigin=registration["sourceCamera"], axes="East, North, Up; meters; lunar MOON_ME tangent frame, never ecliptic J2000 directly", cameraHeightAboveGroundMeters=1.6,
                                 u="frac(atan2(E,N)/(2*pi))", v="(25-degrees(atan2(U,sqrt(E*E+N*N))))/45", elevationBoundsDegrees=[25,-20],
                                 groundHeightAboveReferenceSphereMeters=-2626.984130859375,
                                 coordinateInput="Fixed source-camera-to-DEM-surface vector in ENU. E/N are exact spherical ENU, U includes lunar curvature and source ground-height datum; U -= 1.6. Do not use current eye ray.",
                                 wrapping="U wrap, V clamp; mask V outside [0,1]. At azimuth 0, U=0; east=0.25, south=0.5, west=0.75.",
                                 tangentApproximation="If UV1 is projected chart meters, convert projected easting/northing and spherical height to ENU first. Treating chart N as ENU N and dropping curvature moves hills.",
                                 minimumSurfaceRangeMeters=1800, surfaceRangeFadeMeters=[1500,2200],
                                 fullStrengthWalkRadiusMeters=30, walkFadeRadiusMeters=[30,100],
                                 fullStrengthEyeHeightAboveGroundMeters=[1,2.2], heightFadeMetersAboveGround=[2.2,8],
                                 highAltitudeUse=False, arbitraryTwoKilometerWalkCoverage=False,
                                 parallax="Only existing DEM gives geometry parallax. Photo occlusions/disocclusions and exact NNE source offset unmeasured. 30m is a conservative artistic preview bound, not field-validated."),
                  alpha=dict(meaning="Usable observed far-hill RGB after source footline, sky, flare, foreground-object, missing-skyline and near-horizon range exclusions; feathered confidence, not opacity of physical dust", independentCoverageTexture="Content/Star/Art/LunarPhotoV4/apollo17_far_hills_coverage_16k.png", sourceIdTexture="Content/Star/Art/LunarPhotoV4/apollo17_far_hills_source_ids_16k.png", sourceIdEncoding="uint16 original frame number; dominant photographic contributor before alpha exclusion; zero no support", complete360PhotoCoverage=False,
                             usableAzimuthRangesDegrees=intervals(col_coverage), missingAzimuthRangesDegrees=intervals(~col_coverage),
                             excludedFullFrames=EXCLUDED, sourceFootlineOriginalPixels=FOOT, sourceValidColumnsOriginalPixels=XRANGE,
                             exclusions="LM body/legs and LM shadow, Schmitt shadow, near soil/rocks, direct Sun/flare, source borders/sky, unknown terrain behind foreground objects; no replacement invented"),
                  lighting=dict(photoSequence=sources["sourceSequence"], currentGameSunElevationDegrees=29.393, lightingMatched=False, recommendedDisplay="Blend full linear-decoded photographic RGB as emissive/photographic radiance into the final surface result. Do not multiply a second direct-Sun BRDF into it. Keep unphotographed surface under ordinary lighting.", exposure="Photographic scan is uncalibrated; one bounded uniform display gain may be compared by root. Preserve original local contrast."),
                  sources=records, rights=sources["rights"], newDownloadsBytes=0,
                  runtimeMinimumChange="One Texture2D RGBA + fixed ENU projector input on existing DEM material; lerp(current final shaded radiance, linear(photoRGB)*uniformExposure, photoAlpha*rangeFade*walkFade*heightFade). Use ordinary terrain outside alpha; never place a whole camera frame in UI.",
                  acceptanceRemaining=["Root UE texture import with correct sRGB/alpha", "Existing DEM projection and emission/exposure comparison", "Default EVA view and 30m shifted walk readback", "Photographic gap transitions and horizon seams inspected in game"])
    recipe["artifacts"] = [dict(path=str(p.relative_to(root)).replace("\\", "/"), bytes=p.stat().st_size, sha256=sha(p)) for p in out.rglob("*") if p.is_file()]
    write_json(root / "Data/lunar_photo_display_v04.json", recipe)
    print(json.dumps(dict(usableDegrees=float(np.mean(col_coverage)*360),missing=recipe["alpha"]["missingAzimuthRangesDegrees"],assetBytes=sum(x["bytes"] for x in recipe["artifacts"])),indent=2),flush=True)


if __name__ == "__main__":
    main()
