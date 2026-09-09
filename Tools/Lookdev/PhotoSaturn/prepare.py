"""Build the Saturn photo candidate from the downloaded PDS Cassini RGB FITS.

This is an offline, deterministic authoring step. It does not launch Unreal,
download data, or modify the source FITS. The source map is an RGB display
map in a float32 FITS container. Only rows explicitly containing data are
counted as observed; blank polar/ring-obscured rows are filled with a
latitudinal median and are reported through a separate binary mask.
"""

from __future__ import annotations

from pathlib import Path
import hashlib
import json
import re
from typing import Any

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
SOURCE_RELATIVE = Path(
    "work/photo-pass/research/saturn_coverage/global_maps/data_derived/"
    "Cassini_ISS_RGB_Saturn_global_color_map_original.fits"
)
LABEL_RELATIVE = SOURCE_RELATIVE.with_suffix(".xml")
INFO_RELATIVE = Path(
    "work/photo-pass/research/saturn_coverage/global_maps/image_information/"
    "Saturn_ISS_RGB_global_map_inf-a.txt"
)
ARCHIVE_RELATIVE = Path("work/photo-pass/research/saturn_coverage/co_iss_global-maps.tar.gz")
PCK_CASSINI_RELATIVE = Path("work/photo-pass/research/saturn_coverage/cpck10Jan2005.tpc")
PCK_GENERIC_RELATIVE = Path("work/photo-pass/research/saturn_coverage/pck00011.tpc")

OUTPUT_BODY = ROOT / "Content/Star/Art/PhotoSaturn/saturn_pds_rgb_body_4k.png"
OUTPUT_MASK = ROOT / "Content/Star/Art/PhotoSaturn/saturn_pds_observed_mask_4k.png"
LEGACY_FILES = (
    ROOT / "Content/Star/Art/PhotoSaturn/saturn_cassini_body_4k.png",
    ROOT / "Content/Star/Art/PhotoSaturn/saturn_cassini_photo_weight_4k.png",
    ROOT / "Content/Star/Art/PhotoSaturn/saturn_cassini_rings_rgba.png",
    ROOT / "Content/Star/Art/PhotoSaturn/saturn_cassini_zonal_profile.png",
)
SOURCE_URL = (
    "https://atmos.nmsu.edu/PDS/data/PDS4/co_iss_global-maps/"
    "data_derived/Cassini_ISS_RGB_Saturn_global_color_map_original.fits"
)
LABEL_URL = (
    "https://atmos.nmsu.edu/PDS/data/PDS4/co_iss_global-maps/"
    "data_derived/Cassini_ISS_RGB_Saturn_global_color_map_original.xml"
)
INFO_URL = (
    "https://atmos.nmsu.edu/PDS/data/PDS4/co_iss_global-maps/"
    "data_derived/Image_Information/Saturn_ISS_RGB_global_map_inf-a.txt"
)
ARCHIVE_URL = "https://atmos.nmsu.edu/PDS/data/PDS4/co_iss_global-maps.tar.gz"
PDS_DOI = "10.17189/rkkb-6y30"
FIT_HEADER_BYTES = 2880
RAW_SHAPE = (3, 1801, 3601)
OUTPUT_SIZE = (4096, 2048)
EXPECTED_FITS_SHA256 = "32A62ECA9B82255C7DD763BA0752CEE1664AE73D5EC4D5D5260227DEECD422AF"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def find_dependency(relative: Path) -> Path:
    """Find a dependency in the canonical checkout when running in a worktree."""
    for parent in (ROOT, *ROOT.parents):
        candidate = parent / relative
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"dependency not found: {relative}")


def parse_fits_header(path: Path) -> tuple[dict[str, Any], int]:
    """Read the single FITS header without requiring astropy."""
    blocks: list[bytes] = []
    with path.open("rb") as handle:
        while True:
            block = handle.read(FIT_HEADER_BYTES)
            if len(block) != FIT_HEADER_BYTES:
                raise ValueError("truncated FITS header")
            blocks.append(block)
            if any(
                block[offset : offset + 80].startswith(b"END")
                for offset in range(0, FIT_HEADER_BYTES, 80)
            ):
                break

    values: dict[str, Any] = {}
    for block in blocks:
        for offset in range(0, FIT_HEADER_BYTES, 80):
            card = block[offset : offset + 80]
            key = card[:8].decode("ascii", "replace").strip()
            if key == "END":
                return values, len(blocks) * FIT_HEADER_BYTES
            if card[8:10] != b"= ":
                continue
            raw = card[10:80].decode("ascii", "replace")
            if raw.startswith("'"):
                end_quote = raw.find("'", 1)
                value_text = raw[1:end_quote] if end_quote > 0 else raw[1:].strip()
            else:
                value_text = raw.split("/", 1)[0].strip()
            if value_text == "T":
                values[key] = True
            elif value_text == "F":
                values[key] = False
            else:
                try:
                    number = float(value_text)
                    values[key] = int(number) if number.is_integer() else number
                except ValueError:
                    values[key] = value_text
    raise ValueError("FITS END card not found")


def read_fits_rgb(path: Path) -> tuple[np.ndarray, dict[str, Any], int]:
    header, header_bytes = parse_fits_header(path)
    required = {"BITPIX", "NAXIS", "NAXIS1", "NAXIS2", "NAXIS3"}
    missing = required.difference(header)
    if missing:
        raise ValueError(f"missing FITS header keys: {sorted(missing)}")
    shape = (int(header["NAXIS3"]), int(header["NAXIS2"]), int(header["NAXIS1"]))
    if shape != RAW_SHAPE:
        raise ValueError(f"unexpected FITS shape: {shape}")
    if int(header["BITPIX"]) != -32 or int(header["NAXIS"]) != 3:
        raise ValueError(f"unsupported FITS type: BITPIX={header['BITPIX']} NAXIS={header['NAXIS']}")
    expected = int(np.prod(shape))
    values = np.fromfile(path, dtype=">f4", count=expected, offset=header_bytes)
    if values.size != expected:
        raise ValueError(f"truncated FITS data: expected {expected}, got {values.size}")
    scale = float(header.get("BSCALE", 1.0))
    zero = float(header.get("BZERO", 0.0))
    return values.reshape(shape).astype(np.float32) * scale + zero, header, header_bytes


def srgb_to_linear(values: np.ndarray) -> np.ndarray:
    values = np.asarray(values, dtype=np.float32)
    return np.where(values <= 0.04045, values / 12.92, ((values + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(values: np.ndarray) -> np.ndarray:
    values = np.clip(np.asarray(values, dtype=np.float32), 0.0, 1.0)
    return np.where(values <= 0.0031308, values * 12.92, 1.055 * values ** (1.0 / 2.4) - 0.055)


def fill_unobserved_rows(rgb_linear: np.ndarray, observed: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Fill missing pixels with a conservative latitude-only profile."""
    height = rgb_linear.shape[0]
    row_profile = np.full((height, 3), np.nan, dtype=np.float32)
    for row in range(height):
        if np.any(observed[row]):
            row_profile[row] = np.median(rgb_linear[row, observed[row]], axis=0)
    known = np.flatnonzero(np.isfinite(row_profile[:, 0]))
    if known.size == 0:
        raise ValueError("FITS contains no observed RGB rows")
    profile = np.empty_like(row_profile)
    rows = np.arange(height)
    for channel in range(3):
        profile[:, channel] = np.interp(rows, known, row_profile[known, channel])
    filled = rgb_linear.copy()
    missing_rows, missing_cols = np.where(~observed)
    filled[missing_rows, missing_cols] = profile[missing_rows]
    return filled, profile


def canonicalize_periodic_endpoint(rgb_linear: np.ndarray) -> np.ndarray:
    """Repair released-FITS edge samples before the periodic half-roll."""
    result = rgb_linear.copy()
    # The source includes both west=360 and west=0. The first and
    # penultimate columns carry a narrow released-map seam artifact in some
    # rows. Replace those boundary samples with the adjacent interior
    # average, then copy it to the duplicate endpoint. No observed mask
    # values are changed by this color-only repair.
    period_width = result.shape[1] - 1
    target = 0.5 * (result[:, 1] + result[:, period_width - 2])
    result[:, 0] = target
    result[:, period_width - 1] = target
    result[:, -1] = target
    return result


def resize_linear_rgb(rgb_linear: np.ndarray, size: tuple[int, int]) -> np.ndarray:
    """Resize each linear channel with 16-bit staging to avoid gamma blur."""
    channels: list[np.ndarray] = []
    for channel in range(3):
        encoded = np.uint16(np.rint(np.clip(rgb_linear[:, :, channel], 0.0, 1.0) * 65535.0))
        resized = Image.fromarray(encoded).resize(size, Image.Resampling.LANCZOS)
        channels.append(np.asarray(resized, dtype=np.float32) / 65535.0)
    result = np.stack(channels, axis=-1)
    endpoint = 0.5 * (result[:, 0] + result[:, -1])
    result[:, 0] = endpoint
    result[:, -1] = endpoint
    return result


def intervals(values: np.ndarray, latitudes: np.ndarray) -> list[dict[str, Any]]:
    changes = np.flatnonzero(values[1:] != values[:-1]) + 1
    bounds = np.concatenate(([0], changes, [values.size]))
    result: list[dict[str, Any]] = []
    for start, stop in zip(bounds[:-1], bounds[1:]):
        result.append(
            {
                "row_start": int(start),
                "row_end": int(stop - 1),
                "latitude_start_deg": float(latitudes[start]),
                "latitude_end_deg": float(latitudes[stop - 1]),
                "observed": bool(values[start]),
            }
        )
    return result


def artifact_record(path: Path) -> dict[str, Any]:
    record = {
        "path": path.relative_to(ROOT).as_posix(),
        "sha256": sha256(path),
        "bytes": path.stat().st_size,
    }
    try:
        record["dimensions"] = list(Image.open(path).size)
    except (OSError, ValueError):
        pass
    return record


def source_record(
    identifier: str,
    path: Path,
    source_url: str,
    relative: Path,
    **extra: Any,
) -> dict[str, Any]:
    return {
        "id": identifier,
        "source_url": source_url,
        "path": relative.as_posix(),
        "bytes": path.stat().st_size,
        "sha256": sha256(path),
        **extra,
    }


def main() -> None:
    source_path = find_dependency(SOURCE_RELATIVE)
    label_path = find_dependency(LABEL_RELATIVE)
    info_path = find_dependency(INFO_RELATIVE)
    archive_path = find_dependency(ARCHIVE_RELATIVE)
    pck_cassini = find_dependency(PCK_CASSINI_RELATIVE)
    pck_generic = find_dependency(PCK_GENERIC_RELATIVE)
    data, header, header_bytes = read_fits_rgb(source_path)

    if not np.all(np.isfinite(data)):
        raise ValueError("FITS contains non-finite values")
    if not np.allclose(data, np.rint(data), atol=0.0):
        raise ValueError("FITS RGB values are not byte-equivalent integers")
    source_bytes = np.uint8(np.clip(np.rint(data), 0, 255))
    observed = np.all(source_bytes > 0, axis=0)
    zero_triplets = np.all(source_bytes == 0, axis=0)
    if not np.all(observed | zero_triplets):
        raise ValueError("FITS has partial-zero RGB cells; mask rule would be ambiguous")
    xml_text = label_path.read_text(encoding="utf-8")
    invalid_match = re.search(r"<invalid_constant>([-+0-9.]+)</invalid_constant>", xml_text)
    if invalid_match is None:
        raise ValueError("PDS XML invalid_constant is missing")
    declared_invalid = float(invalid_match.group(1))
    special_invalid_count = int(np.count_nonzero(np.isclose(data, declared_invalid)))

    # FITS NAXIS1 is the fastest axis (Line/longitude), NAXIS2 is Sample
    # (latitude), and NAXIS3 is Band. The label's positive-west longitude
    # runs 360 -> 0. Runtime uses the same zero meridian with east-positive
    # longitude, so east=wrap(-west). Drop the duplicate west=0 endpoint,
    # roll by half the 3600-column period (west=180 starts at u=0), and
    # append that first column to close the UE Wrap seam.
    rgb_source = srgb_to_linear(source_bytes.transpose(1, 2, 0).astype(np.float32) / 255.0)
    lat_pds = np.linspace(-90.0, 90.0, RAW_SHAPE[1], dtype=np.float32)
    west_lon = np.linspace(360.0, 0.0, RAW_SHAPE[2], dtype=np.float32)
    runtime_east_lon = ((-west_lon + 180.0) % 360.0) - 180.0
    filled, profile = fill_unobserved_rows(rgb_source, observed)
    source_endpoint_max = int(
        np.max(np.abs(source_bytes[:, :, 0].astype(int) - source_bytes[:, :, -1].astype(int)))
    )
    filled = canonicalize_periodic_endpoint(filled)
    runtime_latitude = filled[::-1]
    runtime_observed_latitude = observed[::-1]
    period_width = RAW_SHAPE[2] - 1
    half_period = period_width // 2
    runtime_linear = np.concatenate(
        (
            runtime_latitude[:, half_period:period_width],
            runtime_latitude[:, :half_period],
            runtime_latitude[:, half_period : half_period + 1],
        ),
        axis=1,
    )
    runtime_observed = np.concatenate(
        (
            runtime_observed_latitude[:, half_period:period_width],
            runtime_observed_latitude[:, :half_period],
            runtime_observed_latitude[:, half_period : half_period + 1],
        ),
        axis=1,
    )
    body_linear = resize_linear_rgb(runtime_linear, OUTPUT_SIZE)
    body_image = Image.fromarray(
        np.uint8(np.rint(np.clip(linear_to_srgb(body_linear), 0.0, 1.0) * 255.0))
    )
    OUTPUT_BODY.parent.mkdir(parents=True, exist_ok=True)
    body_image.save(OUTPUT_BODY, format="PNG")
    mask_image = Image.fromarray(np.uint8(runtime_observed) * 255).resize(
        OUTPUT_SIZE, Image.Resampling.NEAREST
    )
    mask_image.save(OUTPUT_MASK, format="PNG")

    previous_path = ROOT / "Data/photo_saturn_candidate.json"
    previous: dict[str, Any] = {}
    if previous_path.exists():
        try:
            previous = json.loads(previous_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            previous = {}
    legacy_records = [artifact_record(path) for path in LEGACY_FILES if path.exists()]
    legacy_weight = float(
        previous.get("validation", {}).get(
            "legacy_direct_photo_weight_nonzero_fraction",
            previous.get("validation", {}).get(
                "direct_photo_weight_nonzero_fraction", 0.18730854988098145
            ),
        )
    )

    source_files = [
        source_record(
            "pds_cassini_rgb_global_fits",
            source_path,
            SOURCE_URL,
            SOURCE_RELATIVE,
            observation_start="2011-08-11T03:05:09.695Z",
            observation_stop="2011-08-11T11:40:11.412Z",
            product_level="Derived",
            instrument="Cassini ISS RGB global color map",
            doi=PDS_DOI,
        ),
        source_record(
            "pds_cassini_rgb_global_label",
            label_path,
            LABEL_URL,
            LABEL_RELATIVE,
            label_format="PDS4 XML",
        ),
        source_record(
            "pds_cassini_rgb_image_information",
            info_path,
            INFO_URL,
            INFO_RELATIVE,
            label_format="ASCII source-image table",
        ),
        source_record(
            "pds_cassini_global_maps_archive",
            archive_path,
            ARCHIVE_URL,
            ARCHIVE_RELATIVE,
            archive_sha256="B92655E58734F365CFFEB9841A7BB6A049F6278D100058B4C36A7BEA5CD97374",
        ),
    ]

    source_fraction = float(np.mean(observed))
    output_mask = np.asarray(mask_image, dtype=np.uint8)
    body_pixels = np.asarray(body_image, dtype=np.uint8)
    validation = {
        "input_sha256_verified": sha256(source_path).upper() == EXPECTED_FITS_SHA256,
        "fits_header_bytes": header_bytes,
        "fits_shape_band_sample_line": list(RAW_SHAPE),
        "fits_bitpix": int(header["BITPIX"]),
        "fits_bscale": float(header.get("BSCALE", 1.0)),
        "fits_bzero": float(header.get("BZERO", 0.0)),
        "xml_invalid_constant": declared_invalid,
        "xml_invalid_value_count": special_invalid_count,
        "source_zero_triplet_fraction": float(np.mean(zero_triplets)),
        "source_endpoint_max_8bit": source_endpoint_max,
        "source_observed_fraction": source_fraction,
        "source_unobserved_fraction": 1.0 - source_fraction,
        "output_observed_fraction": float(np.mean(output_mask > 0)),
        "output_unobserved_fraction": float(np.mean(output_mask == 0)),
        "body_dimensions": list(OUTPUT_SIZE),
        "body_longitude_seam_max_8bit": int(
            np.max(np.abs(body_pixels[:, 0].astype(int) - body_pixels[:, -1].astype(int)))
        ),
        "body_periodic_join_max_8bit": int(
            np.max(
                np.abs(
                    body_pixels[:, OUTPUT_SIZE[0] // 2 - 1].astype(int)
                    - body_pixels[:, OUTPUT_SIZE[0] // 2].astype(int)
                )
            )
        ),
        "mask_binary": bool(set(np.unique(output_mask).tolist()).issubset({0, 255})),
        "legacy_direct_photo_weight_nonzero_fraction": legacy_weight,
    }
    metadata = {
        "schema_version": 2,
        "candidate_id": "cassini_pds_rgb_global_4k_v2",
        "state": "CANDIDATE_PENDING_ROOT_UE_MULTIVIEW",
        "sources": source_files,
        "source_integrity": {
            "fits_sha256": sha256(source_path),
            "fits_sha256_expected": EXPECTED_FITS_SHA256,
            "header": {
                key: header[key]
                for key in (
                    "SIMPLE",
                    "BITPIX",
                    "NAXIS",
                    "NAXIS1",
                    "NAXIS2",
                    "NAXIS3",
                    "LAT_C",
                    "LON_W",
                    "RESOLUTI",
                    "PHASE",
                    "SUB_S_LA",
                    "SUB_C_LA",
                    "DATE-STA",
                    "DATE-STO",
                )
                if key in header
            },
            "byte_order": "IEEE754MSBSingle (big-endian float32), FITS 3.0",
            "axis_index_order": "Last Index Fastest; physical [Band, Sample, Line]",
            "invalid_constant_from_xml": declared_invalid,
            "invalid_constant_present_in_fits": special_invalid_count,
            "zero_triplets_are_unobserved": True,
            "coordinate_samples": [
                {
                    "source_column": int(column),
                    "pds_west_deg": float(west_lon[column]),
                    "runtime_east_deg": float(runtime_east_lon[column]),
                    "runtime_u": float((runtime_east_lon[column] + 180.0) / 360.0),
                }
                for column in (0, 900, 1800, 2700, 3600)
            ],
        },
        "body": {
            "path": "Content/Star/Art/PhotoSaturn/saturn_pds_rgb_body_4k.png",
            "dimensions": list(OUTPUT_SIZE),
            "projection": "PDS equirectangular / plate carree",
            "uv": "u=(east_positive_longitude_degrees+180)/360, v=(90-planetocentric_latitude_degrees)/180",
            "coordinate_convention": {
                "latitude": "planetocentric degrees, runtime v=0 north to v=1 south",
                "pds_latitude": "line/sample row 0=-90, row 1800=+90; runtime vertically flips this order",
                "pds_longitude": "Positive West; line/sample column 0=360, column 3600=0",
                "runtime_longitude": "east-positive shader longitude = wrap(-PDS west longitude); west 0/360 maps to runtime 0",
                "central_meridian": "PDS west 0/360 degrees is runtime 0 degrees at u=0.5; PDS west 180 degrees is runtime -180/+180 at u=0/1",
                "periodic_reindex": "replace the released FITS boundary samples with the adjacent interior average, drop the duplicate endpoint, roll the 3600-column period by 1800 columns so west=180 starts at u=0, then append the first column",
                "seam": "periodic west=180 endpoint is duplicated after reindex and equalized after 4K resize",
            },
            "shape": {
                "equatorial_radius_km": 60268,
                "polar_radius_km": 54364,
                "axes_units": "km",
                "datum": "Saturn 1-bar oblate axes from NAIF BODY699_RADII",
                "pck_sources": [
                    {
                        "id": "cassini_pck_2005",
                        "path": PCK_CASSINI_RELATIVE.as_posix(),
                        "sha256": sha256(pck_cassini),
                    },
                    {
                        "id": "generic_pck_00011",
                        "path": PCK_GENERIC_RELATIVE.as_posix(),
                        "sha256": sha256(pck_generic),
                    },
                ],
            },
            "observed_mask": {
                "path": "Content/Star/Art/PhotoSaturn/saturn_pds_observed_mask_4k.png",
                "encoding": "binary L8: 255=all three RGB bands nonzero/observed, 0=unobserved or blank",
                "source_resolution_fraction": source_fraction,
                "source_intervals": intervals(np.any(observed, axis=1), lat_pds),
                "runtime_use": "audit/coverage visualization only; do not multiply body albedo in the material",
            },
            "infill": {
                "method": "linear interpolation of per-latitude median observed RGB in linear light; nearest observed profile at polar caps",
                "synthetic_detail": False,
                "unobserved_pixels_are_not_claimed_as_observed": True,
            },
            "photometry": {
                "source_values": "display RGB byte-equivalent values in a float32 FITS container, 0..255",
                "output_color_space": "sRGB encoded RGB8 PNG; UE sRGB=true",
                "processing": "decode source sRGB to linear for boundary repair, periodic reindex, infill and resize, then encode sRGB; no tone mapping or contrast enhancement",
                "units": "display RGB; approximately 161 km/pixel in source map",
                "scientific_limit": "Derived atmosphere map assembled from multiple Cassini ISS observations; not a static surface albedo or calibrated reflectance field",
            },
            "xml_scale_reconciliation": {
                "declared_pixel_scale": "0.1 pixel/deg (label)",
                "used_scale": "10.0 pixel/deg from 3601 samples over 360 degrees including both endpoints",
                "reason": "FITS dimensions and LAT_C/LON_W coordinate strings are internally consistent; the XML scale value is treated as an inverse/label inconsistency and is not used for resampling",
            },
        },
        "rings": previous.get(
            "rings",
            {
                "path": "Content/Star/Art/PhotoSaturn/saturn_cassini_rings_rgba.png",
                "status": "preserved_from_legacy_candidate",
            },
        ),
        "legacy_candidate": {
            "state": previous.get("state", "CANDIDATE_PENDING_ROOT_UE_MULTIVIEW"),
            "body_path": "Content/Star/Art/PhotoSaturn/saturn_cassini_body_4k.png",
            "photo_weight_path": "Content/Star/Art/PhotoSaturn/saturn_cassini_photo_weight_4k.png",
            "direct_photo_weight_nonzero_fraction": legacy_weight,
            "artifacts_preserved": legacy_records,
            "note": "Original PIA11141/PIA05421 candidate files remain unchanged; this PDS candidate is a separate asset.",
        },
        "validation": validation,
        "credits": {
            "primary": "NASA/JPL/Caltech Cassini ISS; PDS Atmospheres Node",
            "shape": "NASA/JPL NAIF",
            "archive": "PDS CO-ISS Global Maps, DOI " + PDS_DOI,
            "policy": "https://www.jpl.nasa.gov/jpl-image-use-policy",
            "credit_text": "Courtesy NASA/JPL-Caltech/Cassini ISS; PDS CO-ISS Global Maps; Saturn axes from NASA/JPL NAIF",
        },
        "artifacts": [
            artifact_record(OUTPUT_BODY),
            artifact_record(OUTPUT_MASK),
        ],
        "gate_ceiling": {
            "SOURCE_CHECKS_PASS": "CPU FITS/hash/UV/coverage/output checks are runnable by this script",
            "DEVICE_PASS": "UNVERIFIED: physical display/device not exercised",
            "PROVIDER_PASS": "NOT_APPLICABLE",
            "PUBLIC_PASS": "UNVERIFIED: no publication performed",
            "VISUAL_REVIEW": "UNVERIFIED: visual/runtime review required",
        },
    }
    previous_path.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(validation, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
