#!/usr/bin/env python3
"""Offline raster regressions: production adapter + real libtiff, NOT Unreal.

Requires C++17, pkg-config and libtiff development files. Generates synthetic
TIFFs and mocks HTTP/UE services; never downloads observational data. Every run
gets a fresh directory under work/raster-integration; no saves are read/deleted.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="clang++")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--adapter-only", action="store_true",
                        help="Run only the production-adapter integration suite")
    parser.add_argument("--source-root", type=Path,
                        help="Alternate verified source tree (requires --adapter-only)")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source_root = args.source_root.resolve() if args.source_root else root
    if args.source_root and not args.adapter_only:
        parser.error("Use --adapter-only with --source-root for baseline comparison.")
    compiler = shutil.which(args.compiler)
    pkg_config = shutil.which("pkg-config")
    if not compiler or not pkg_config:
        parser.error("A C++17 compiler and pkg-config are required (no automatic installation).")
    try:
        tiff_version = subprocess.check_output(
            [pkg_config, "--modversion", "libtiff-4"], text=True).strip()
        tiff_flags = shlex.split(subprocess.check_output(
            [pkg_config, "--cflags", "--libs", "libtiff-4"], text=True))
        compiler_version = subprocess.check_output([compiler, "--version"], text=True)
    except (OSError, subprocess.CalledProcessError) as error:
        parser.error(f"Compiler and libtiff development files are required: {error}")
    targets = [("adapter", "RemoteRasterIntegrationTests.cpp")]
    if not args.adapter_only:
        targets += [("seek-policy", "RangeSeekPolicyTests.cpp"),
                    ("raster-policy", "RasterReadPolicyTests.cpp")]
    for path in [source_root / "Source/Star/Terrain/StarRemoteRaster.cpp",
                 *(root / "Tests/Terrain" / filename for _, filename in targets)]:
        if not path.is_file():
            parser.error(f"Missing source file: {path}")

    work = root / "work/raster-integration"
    work.mkdir(parents=True, exist_ok=True)
    run_dir = Path(tempfile.mkdtemp(prefix="run-", dir=work))
    flags = ["-std=c++17", "-O1", "-g", "-Wall", "-Wextra",
             "-Wno-missing-field-initializers", "-DTIFF_DISABLE_DEPRECATED"]
    # Baseline sources may have pre-existing warnings; still execute their tests.
    # Current-source acceptance builds always treat warnings as errors.
    if not args.source_root:
        flags += ["-Werror"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined,float-cast-overflow",
                  "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    environment = os.environ.copy()
    if args.sanitize:
        environment["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    result = {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "source_root": str(source_root), "compiler": compiler,
        "compiler_version": compiler_version, "libtiff": tiff_version,
        "sanitize": args.sanitize,
        "scope": "Actual raster adapter + UE/HTTP test doubles + system libtiff; NOT Unreal Engine",
        "sanitizer_scope": "Project code and test harness, not the prebuilt system libtiff shared library",
        "suites": [], "result": "not_run",
    }
    try:
        for name, filename in targets:
            executable = run_dir / name
            command = [compiler, *flags, "-I", str(root / "Tests/Terrain/UnrealShim"),
                       "-I", str(source_root / "Source/Star"),
                       str(root / "Tests/Terrain" / filename)]
            if name == "adapter":
                command += tiff_flags
            command += ["-o", str(executable)]
            suite = {"name": name, "build_command": command, "result": "not_run"}
            result["suites"].append(suite)
            with (run_dir / f"{name}-build.log").open("w", encoding="utf-8") as log:
                built = subprocess.run(command, cwd=root, stdout=log,
                                       stderr=subprocess.STDOUT, timeout=180)
            suite["build_exit_code"] = built.returncode
            if built.returncode:
                suite["result"] = "build_failed"
                print((run_dir / f"{name}-build.log").read_text(), end="")
                continue
            test_command = [str(executable)]
            if name == "adapter":
                test_command += [str(run_dir / "fixtures")]
            suite["test_command"] = test_command
            with (run_dir / f"{name}-tests.log").open("w", encoding="utf-8") as log:
                tested = subprocess.run(test_command, cwd=root, env=environment,
                                        stdout=log, stderr=subprocess.STDOUT, timeout=180)
            output = (run_dir / f"{name}-tests.log").read_text()
            print(f"=== {name} ===\n{output}", end="")
            suite["test_exit_code"] = tested.returncode
            match = re.search(r"^(?:RESULT )?(\d+) (cases|checks), (\d+) failures$",
                              output, re.MULTILINE)
            if match:
                suite.update(count=int(match[1]), unit=match[2], failures=int(match[3]))
            suite["result"] = ("passed" if tested.returncode == 0 and match and
                               int(match[3]) == 0 else "failed")
        result["result"] = ("passed" if all(s["result"] == "passed" for s in result["suites"])
                            else "failed")
        return 0 if result["result"] == "passed" else 1
    except (OSError, subprocess.TimeoutExpired) as error:
        result.update(result="error", error=str(error))
        print(f"ERROR: {error}", file=sys.stderr)
        return 2
    finally:
        (run_dir / "results.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(f"Evidence: {run_dir}")


if __name__ == "__main__":
    raise SystemExit(main())
