# Offline raster adapter regression tests

These tests compile the actual `Source/Star/Terrain/StarRemoteRaster.cpp` against
small Unreal/HTTP test doubles and the real system libtiff. They are **not an
Unreal Engine build**, a real HTTP backend test, or packaged-game acceptance.

## Run

On Linux, install a C++17 compiler, Python 3.10+, `pkg-config`, and libtiff
development headers/library using the environment's package manager. The runner
does not install software and the tests make no network requests.

```sh
python3 Tools/Testing/run_raster_integration.py --compiler g++
python3 Tools/Testing/run_raster_integration.py --compiler clang++ --sanitize
```

Each invocation creates a fresh directory under `work/raster-integration/`.
It records compiler commands/versions, exit codes, test counts and logs in
`results.json` and `*.log`. It never reads or deletes user saves or observational
sources. Synthetic TIFF fixtures and compiled executables remain under `work/`.

The default runner executes three suites:

| Suite | Scope |
|---|---|
| RemoteRasterIntegrationTests | 43 named cases exercising the actual adapter and libtiff |
| RangeSeekPolicyTests | 100,048 deterministic boundary/property assertions |
| RasterReadPolicyTests | Existing 40,590 raster/range policy assertions |

The integration suite covers failure-state preservation, retry/error clearing,
post-decode cancellation/deadlines, unsigned/float32 sample support, unsupported
signed/float16/orientation rejection, disk-size prechecks, bounded compressed
block retention, seek/read errors, and HTTP response/cancellation handling. Valid
edge tiles, multi-byte/big-endian data, offline cached reads, truncated-cache
recovery and optional disk-write failures are retained as positive controls.

## Red/green comparison

Use a separately verified source tree containing the baseline adapter and its
headers, without modifying the working checkout:

```sh
python3 Tools/Testing/run_raster_integration.py --compiler clang++ --sanitize \
  --adapter-only --source-root /path/to/verified-baseline
```

Baseline comparison allows compiler warnings so existing warnings do not prevent
execution of the regression cases. Normal acceptance builds use `-Werror`.
The same test cases and synthetic data generator run against either source tree.

The original adapter at PR #1 HEAD
`309992e6e6cef8ba1eabadd778fbc11df530a2b2` failed 27 of the 43 cases;
the updated adapter passed all 43 under GCC and Clang sanitizers in the supplied
local evidence. Cases overlap and are not a count of independent bugs.

## Test-double and instrumentation boundaries

`UnrealShim` supplies only APIs needed by the adapter. Its filesystem calls use
real temporary files, while requests, timing, cancellation and diagnostics are
controlled test doubles. The cache-key hash is deterministic but is not Unreal's
MD5 implementation. The final-tile hook runs **after the real TIFF decoder** and
then changes cancellation/time to make completion races reproducible.

`TIFF_DISABLE_DEPRECATED` avoids clashes between libtiff's obsolete integer
aliases and UE-style aliases in the standalone harness. ASan/UBSan and
float-cast-overflow instrumentation cover project code and the harness, not the
prebuilt system libtiff shared library. No claim about the safety of all libtiff
internals is made.

## Remaining acceptance gates

- Build with the real Unreal Engine SDK, then exercise the packaged Windows game.
- Check actual HTTP cancellation/deadlines, request lifetime and backend buffering.
  Avoiding a second oversized copy does not cap the initial HTTP receive buffer.
- Same-size remote changes, ETag consistency, disk corruption and adversarial
  concurrent cache replacement are not solved by size checks alone.
- The 32 MiB limit covers resident compressed range blocks, not total process
  memory. A previous image, pending image, tile scratch and backend buffers can
  coexist. Decode resource accounting needs real-engine profiling.
- Signed/float16 and non-top-left rasters are rejected, not converted. Confirm
  fallbacks for any additional source products before adopting them.
- Run the existing full native suites plus graphics, audio, input and VR checks.

The added workflow runs the offline adapter/policy tests only. Its preparation
must not be described as an executed GitHub Actions run or full-game validation.
