# STAR data delivery — 2026-09-06

**SOURCE_CHECKS_PASS**: 18 portable runtime artifacts, about398MB, acquired and processed from public NASA/JPL/PDS/LROC distribution. The game needs neither network access nor NASA credentials to use these files. This report does not claim packaged Unreal/gameplay/controller acceptance.

Actual acquisition and processing:

- Moon2025 color: 8192×4096 RGB16 PNG, byte-for-byte pixel equality to original sRGB TIFF. NASA's aesthetic adjustments/polar filling are retained and identified.
- Global lunar terrain: 5760×2880 signed int16LE, 0.5m units above1737400m, exact conversion from NASA unsigned encoding with its20000-count offset removed.
- Apollo17: native5m stereo terrain and5m orthophoto, pixel-aligned2800×2400 crop,14×12km,100% valid source terrain. Height range−2935.568m to−1135.930m. Source product is **LROC5m**, not USGS0.5m.
- Earth: actual September2004 BlueMarble daytime and2012 BlackMarble nighttime maps converted to8192×4096 in linear light. Night map retains source background and is not pure radiance/emission data.
- Stars: NASA DeepStarMaps2020 celestial8192×4096 linear half-float EXR preserved. J2000 equatorial/left-increasing RA mapping and ecliptic conversion are explicit.
- Saturn: NASA VTAD cube-atlas source reprojected through original mesh UVs to4096×2048 lat/lon; natural-color CassiniPIA05389 reference retained separately. Ring strip contains source color and UVIS-based normal-incidence alpha; full71007-row1km optical-depth profile retains detection limits and flags.
- JPL Horizons: actual geometric vectors for Sun10/Earth399/Moon301/Saturn699 at2026-09-06T00:00UTC, solar-system barycenter origin, eclipticJ2000, SI meters. Full API responses are retained in provenance.
- NAIF SPICE: actual body-to-ecliptic rotation matrices. Moon uses MOON_ME/DE421; Saturn's equatorial ring-plane normal is approximately[0.085505,0.462439,0.882519]. EarthIAU spin is explicitly approximate compared withITRF.
- NASA MCP1.0.14: actual localstdio `nasa_images` and `jpl_horizons` calls both returned real content. Image bytes and Horizons `$$SOE` data were observed. PublicDEMO_KEY only; package-lock pins the dependency graph.

Validation command: `work/venv/Scripts/python.exe Tools/Data/test_data.py` — **10 tests passed**. Covers every runtime artifact hash/metadata, exact source-to-runtime heights and MoonRGB16 pixels, dimension/count checks, geographic seam/pole interpolation and invalid-region fallback, Earth/Moon physical scale, orthonormal body matrices, Moon mean-Earth direction, Saturn ring tilt, and celestial orientation.

Visual inspection of the converted Saturn map found no unused white cube-atlas faces; Apollo17 image preserves the original photographed terrain, including source lighting/mosaic seams. No generated terrain or imagery replaces scientific observations.

Git payload verification additionally checked 26 runtime and ephemeris/orientation provenance blobs against their recorded SHA-256 values. Runtime JSON/CSV uses LF. Acquired provenance retains its original bytes and source formatting, including PDS fixed-width whitespace; its local attributes disable newline rewriting and whitespace lint.

Known gaps/limits: the previously403-denied USGS Apollo17 landing page and Wiley DOI were not retried. Official LROC distribution supplied the usable5m alternative. NASA's Saturn visualization asset does not specify source observation dates or absolute longitude registration. Ring scattering/view-dependent opacity, finer terrain, Unreal import/compression, runtime streaming and packaged performance require integration verification.

One stale NASA Photojournal JPEG link returned404; the documented NASA Images API supplied the current original asset URL. A gzip transfer-length check in the downloader was corrected because HTTPContent-Length described compressed transfer bytes; decoded file bytes were separately hashed and retained. The source catalog now pins17 actual sourceSHA256 values.
