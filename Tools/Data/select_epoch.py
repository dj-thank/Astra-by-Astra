"""Select a sunlit Apollo 17 start epoch from four real JPL Horizons queries.

Use work/venv/Scripts/python.exe Tools/Data/select_epoch.py --acquire once.
Then --verify checks the committed record offline, including all source vectors,
kernel hashes and SPICE rotations. --rebuild replays the recorded raw responses.
Does not modify the shared manifest, earlier provenance, or other data scripts.
"""
from __future__ import annotations

import argparse
import copy
import csv
import datetime as dt
import hashlib
import json
import re
from pathlib import Path

import numpy as np
import requests
import spiceypy as spice

from acquire import ROOT, digest, save_json
from horizons import PARAMS

WORK = ROOT / "work/epoch-selection"
BODIES = ROOT / "Content/Star/Data/bodies.json"
RECORD = ROOT / "Data/epoch_selection.json"
BASELINE = WORK / "bodies_2026-09-06_original.json"
INITIAL_EPOCH = "2026-09-06T00:00:00Z"
START = "2026-09-07 00:00:00"
STOP = "2026-10-06 00:00:00"
LATITUDE, LONGITUDE = 20.1908, 30.7717
API = "https://ssd.jpl.nasa.gov/api/horizons.api"
IDS = {"sun": 10, "moon": 301, "earth": 399, "saturn": 699}
MONTHS = {name: i + 1 for i, name in enumerate(
    ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"])}


def text_digest(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def parse_response(item: dict) -> dict:
    text = item["rawResponseUtf8"]
    assert text_digest(text) == item["rawResponseSha256"]
    assert len(text.encode("utf-8")) == item["rawResponseBytes"]
    payload = json.loads(text)
    result = payload.get("result", "")
    if payload.get("error") or "$$SOE" not in result:
        raise RuntimeError("Horizons did not return a valid vector ephemeris")
    for expected in ["Ecliptic of J2000.0", "KM-S", "Solar System Barycenter"]:
        assert expected in result, f"Unexpected vector frame/units/origin: {expected}"
    assert f"({item['horizonsId']})" in result.split("Center body name:")[0]
    rows = {}
    for row in csv.reader(result.split("$$SOE")[1].split("$$EOE")[0].strip().splitlines()):
        parts = re.search(r"(\d{4})-([A-Za-z]{3})-(\d{2}) (\d{2}):(\d{2}):(\d{2})", row[1]).groups()
        stamp = dt.datetime(int(parts[0]), MONTHS[parts[1]], *map(int, parts[2:]), tzinfo=dt.timezone.utc)
        epoch = stamp.strftime("%Y-%m-%dT%H:%M:%SZ")
        rows[epoch] = {"julianDateUt": float(row[0]), "positionMeters": [float(x) * 1000 for x in row[2:5]],
                       "velocityMetersPerSecond": [float(x) * 1000 for x in row[5:8]], "sourceCsvRow": row}
    return rows


def query(ident: str, epoch: str | None = None) -> dict:
    path = WORK / f"horizons_{ident}.json"
    start = START if epoch is None else epoch.replace("T", " ").removesuffix("Z")
    stop = STOP if epoch is None else (dt.datetime.fromisoformat(epoch.replace("Z", "+00:00"))
                                    + dt.timedelta(minutes=1)).strftime("%Y-%m-%d %H:%M:%S")
    params = {**PARAMS, "COMMAND": f"'{IDS[ident]}'", "START_TIME": f"'{start}'", "STOP_TIME": f"'{stop}'",
              "STEP_SIZE": "'12 h'" if epoch is None else "'1 min'"}
    if path.exists():
        item = json.loads(path.read_text(encoding="utf-8"))
        assert item["requestParameters"] == params, "Cached query differs; retained for inspection"
        parse_response(item)
        return item
    attempt = WORK / f"horizons_{ident}.attempt.json"
    if attempt.exists():
        raise RuntimeError("Prior query attempt is unresolved; no automatic retry beyond four-body query budget")
    save_json(attempt, {"body": ident, "requestParameters": params,
                        "attemptedAt": dt.datetime.now(dt.timezone.utc).isoformat()})
    print("QUERY", ident, start, stop, flush=True)
    response = requests.get(API, params=params, timeout=(15, 60))
    response.raise_for_status()
    raw = response.content.decode("utf-8")
    item = {"horizonsId": str(IDS[ident]), "sourceUrl": response.url, "requestParameters": params,
            "retrievedAt": dt.datetime.now(dt.timezone.utc).isoformat(),
            "rawResponseUtf8": raw, "rawResponseSha256": text_digest(raw), "rawResponseBytes": len(response.content)}
    save_json(path, item)
    parse_response(item)
    return item


def load_kernels(baseline: dict) -> None:
    spice.kclear()
    for item in baseline["orientationSources"]:
        path = ROOT / item["path"]
        assert digest(path) == item["sha256"], "Orientation kernel changed"
        spice.furnsh(str(path))


def normal() -> np.ndarray:
    lat, lon = np.radians([LATITUDE, LONGITUDE])
    return np.array([np.cos(lat) * np.cos(lon), np.cos(lat) * np.sin(lon), np.sin(lat)])


def solar_angles(epoch: str, sun: list, moon: list) -> dict:
    rotation = spice.pxform("MOON_ME", "ECLIPJ2000", spice.str2et(epoch))
    up = rotation @ normal()
    direction = np.array(sun) - np.array(moon)
    unit_direction = direction / np.linalg.norm(direction)
    surface_direction = direction - 1_737_400 * up
    surface_direction /= np.linalg.norm(surface_direction)
    lat, lon = np.radians([LATITUDE, LONGITUDE])
    east = rotation @ np.array([-np.sin(lon), np.cos(lon), 0])
    north = rotation @ np.array([-np.sin(lat) * np.cos(lon), -np.sin(lat) * np.sin(lon), np.cos(lat)])
    return {"sunElevationDegrees": float(np.degrees(np.arcsin(np.clip(np.dot(up, unit_direction), -1, 1)))),
            "surfaceSunElevationDegrees": float(np.degrees(np.arcsin(np.clip(np.dot(up, surface_direction), -1, 1)))),
            "sunAzimuthDegreesEastOfNorth": float(np.degrees(np.arctan2(np.dot(east, unit_direction), np.dot(north, unit_direction))) % 360)}


def choose(sources: dict) -> tuple[str, list]:
    sun, moon = parse_response(sources["sun"]), parse_response(sources["moon"])
    assert sun.keys() == moon.keys()
    candidates = [{"epoch": epoch, **solar_angles(epoch, sun[epoch]["positionMeters"], moon[epoch]["positionMeters"])}
                  for epoch in sorted(sun)]
    eligible = [item for previous, item in zip(candidates, candidates[1:])
                if 20 <= item["sunElevationDegrees"] <= 40
                and item["sunElevationDegrees"] > previous["sunElevationDegrees"]]
    assert eligible, "No rising-sun candidate within the allowed date and elevation range"
    selected = min(eligible, key=lambda item: (abs(item["sunElevationDegrees"] - 30), item["epoch"]))
    return selected["epoch"], candidates


def new_bodies(baseline: dict, sources: dict, epoch: str) -> dict:
    data = copy.deepcopy(baseline)
    data["epoch"] = epoch
    et = spice.str2et(epoch)
    for body in data["bodies"]:
        row = parse_response(sources[body["id"]])[epoch]
        for field in ["positionMeters", "velocityMetersPerSecond"]:
            body[field] = row[field]
        rotation = spice.pxform(body["bodyFixedFrame"], "ECLIPJ2000", et)
        assert np.allclose(rotation.T @ rotation, np.eye(3), atol=1e-13)
        assert abs(np.linalg.det(rotation) - 1) < 1e-13
        body.update(epoch=epoch, orientationEpoch=epoch, bodyFixedToEclipticJ2000=rotation.tolist(),
                    northPoleEclipticJ2000=rotation[:, 2].tolist())
    data["orientationCalculation"] = {"library": "SpiceyPy " + spice.__version__, "toolkit": spice.tkvrsn("TOOLKIT"),
                                      "ephemerisTimeTdbSecondsPastJ2000": et,
                                      "operation": "pxform(bodyFixedFrame,ECLIPJ2000,ET)"}
    return data


def acquire() -> None:
    WORK.mkdir(parents=True, exist_ok=True)
    if not BASELINE.exists():
        assert json.loads(BODIES.read_text(encoding="utf-8"))["epoch"] == INITIAL_EPOCH
        BASELINE.write_bytes(BODIES.read_bytes())
    raw_baseline = BASELINE.read_text(encoding="utf-8")
    baseline = json.loads(raw_baseline)
    assert baseline["epoch"] == INITIAL_EPOCH
    load_kernels(baseline)
    sources = {ident: query(ident) for ident in ["sun", "moon"]}
    epoch, candidates = choose(sources)
    print("SELECTED", epoch, next(item for item in candidates if item["epoch"] == epoch), flush=True)
    for ident in ["earth", "saturn"]:
        sources[ident] = query(ident, epoch)
    original = {body["id"]: body for body in baseline["bodies"]}
    record = {
        "schemaVersion": 1,
        "purpose": "Set the initial scenario to real morning illumination for the Apollo 17 valley/landing scene.",
        "site": {"latitudeDegrees": LATITUDE, "longitudeDegreesEast": LONGITUDE, "frame": "MOON_ME", "referenceRadiusMeters": 1737400},
        "selection": {"rangeUtc": [START, STOP], "candidateStepHours": 12, "desiredElevationDegrees": [20, 40],
                      "preferredElevationDegrees": 30,
                      "rule": "Among 12-hour real-vector samples with rising solar elevation in [20,40], choose closest to 30 degrees; earliest epoch breaks ties.",
                      "selectedEpoch": epoch, "officialBodyQueryCount": 4,
                      "candidateSamples": candidates},
        "before": {"epoch": INITIAL_EPOCH, "backupPath": BASELINE.relative_to(ROOT).as_posix(),
                   "originalBodiesSha256": text_digest(raw_baseline), "originalBodiesUtf8": raw_baseline,
                   **solar_angles(INITIAL_EPOCH, original["sun"]["positionMeters"], original["moon"]["positionMeters"])},
        "after": next(item for item in candidates if item["epoch"] == epoch),
        "horizonsResponses": sources,
        "orientationSources": baseline["orientationSources"],
        "coordinates": {
            "vectors": "JPL Horizons geometric ICRF / ecliptic J2000, solar-system barycenter; KM-S converted to double-precision meters and meters/second.",
            "times": "All selected vectors and rotations use the same UTC epoch; Horizons TIME_TYPE=UT. SPICE str2et converts UTC to TDB seconds since J2000 with naif0012.tls.",
            "rotations": "SPICE pxform(bodyFixedFrame,ECLIPJ2000,ET); MOON_ME from DE421 lunar orientation, IAU_SUN/IAU_EARTH/IAU_SATURN from pck00011.tpc; row-major matrix multiplying column vectors.",
            "sunElevation": "degrees(asin(dot(R_MOON_ME_to_ECLIPJ2000 * [cos(lat)cos(lon),cos(lat)sin(lon),sin(lat)], normalize(SunPosition-MoonPosition))))",
            "surfaceSunElevation": "Same geometric direction evaluated at MoonPosition + 1737400 * rotated radial unit normal.",
        },
        "documentation": ["https://ssd-api.jpl.nasa.gov/doc/horizons.html", "https://naif.jpl.nasa.gov/pub/naif/generic_kernels/fk/satellites/moon_080317.tf"],
        "processing": "Tools/Data/select_epoch.py --acquire; --rebuild uses embedded exact raw responses and hash-pinned local kernels without network.",
        "limitations": ["Solar elevation is above the spherical radial tangent plane; local slopes, terrain horizon occlusion and solar angular radius are not included.",
                        "This historical/future ephemeris selection sets a game scenario, not the workstation clock or live weather.",
                        "Horizons current translational solution is combined with the existing DE421 lunar orientation; kernel and ephemeris model limits remain explicit.",
                        "IAU_EARTH is an approximate spin model rather than high-precision ITRF.",
                        "Source data checks do not establish packaged-game brightness, shadows, appearance or physical joystick behavior."],
    }
    save_json(RECORD, record)
    rebuild()


def rebuild(write: bool = True) -> None:
    record = json.loads(RECORD.read_text(encoding="utf-8"))
    assert text_digest(record["before"]["originalBodiesUtf8"]) == record["before"]["originalBodiesSha256"]
    baseline = json.loads(record["before"]["originalBodiesUtf8"])
    load_kernels(baseline)
    epoch, candidates = choose(record["horizonsResponses"])
    assert epoch == record["selection"]["selectedEpoch"]
    assert candidates == record["selection"]["candidateSamples"]
    data = new_bodies(baseline, record["horizonsResponses"], epoch)
    data["provenance"] = [{"path": RECORD.relative_to(ROOT).as_posix(), "sha256": digest(RECORD),
                           "sourceJsonPointer": f"/horizonsResponses/{ident}/rawResponseUtf8",
                           "sourceResponseSha256": item["rawResponseSha256"], "requestParameters": item["requestParameters"],
                           "sourceUrl": item["sourceUrl"], "observationDate": epoch,
                           "sourceUnits": "KM-S; converted to meters and meters/second"}
                          for ident, item in record["horizonsResponses"].items()]
    if write:
        save_json(BODIES, data)
    else:
        assert json.loads(BODIES.read_text(encoding="utf-8")) == data, "Runtime data differs from exact recorded source replay"
    bodies = {body["id"]: body for body in data["bodies"]}
    angles = solar_angles(epoch, bodies["sun"]["positionMeters"], bodies["moon"]["positionMeters"])
    assert 20 <= angles["surfaceSunElevationDegrees"] <= 40
    assert all(body["epoch"] == body["orientationEpoch"] == epoch for body in bodies.values())
    distance = lambda a, b: float(np.linalg.norm(np.array(bodies[a]["positionMeters"]) - bodies[b]["positionMeters"]))
    assert 350e6 < distance("earth", "moon") < 410e6
    assert 145e9 < distance("earth", "sun") < 153e9
    for item in data["provenance"] + data["orientationSources"]:
        assert digest(ROOT / item["path"]) == item["sha256"]
    print(json.dumps({"verified": True, "epoch": epoch, "bodiesSha256": digest(BODIES),
                      "epochSelectionSha256": digest(RECORD), **angles,
                      "earthMoonDistanceMeters": distance("earth", "moon"), "earthSunDistanceMeters": distance("earth", "sun")}, indent=2), flush=True)
    spice.kclear()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--acquire", action="store_true")
    action.add_argument("--rebuild", action="store_true")
    action.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    if args.acquire:
        acquire()
    else:
        rebuild(write=args.rebuild)
