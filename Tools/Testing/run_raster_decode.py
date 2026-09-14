#!/usr/bin/env python3
"""Test the production raster decoder against real libtiff and local TIFF fixtures.

Requires pkg-config and libtiff development headers. No downloaded data or UE.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
from extract_method import extract


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = root / 'work' / ('raster-decode-sanitized' if args.sanitize else 'raster-decode')
    out.mkdir(parents=True, exist_ok=True)
    source = (root / 'Source/Star/Terrain/StarRemoteRaster.cpp').read_text(encoding='utf-8')
    methods = [extract(source, 'toff_t Seek('), extract(source, 'bool FStarRemoteRaster::Load(')]
    (out / 'raster-load-method.inc').write_text('\n\n'.join(methods) + '\n', encoding='utf-8')
    try:
        flags = subprocess.run(['pkg-config', '--cflags', '--libs', 'libtiff-4'],
                               text=True, capture_output=True, check=True, timeout=30)
        version = subprocess.run(['pkg-config', '--modversion', 'libtiff-4'],
                                 text=True, capture_output=True, check=True, timeout=30).stdout.strip()
        cmd = [args.compiler, '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-I' + str(root / 'Source/Star'), '-I' + str(out),
               str(root / 'Tests/Terrain/RemoteRasterDecodeTests.cpp'),
               '-o', str(out / 'raster-decode'), *shlex.split(flags.stdout)]
        if args.sanitize:
            cmd += ['-fsanitize=address,undefined,float-cast-overflow', '-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer']
        build = subprocess.run(cmd, text=True, capture_output=True, timeout=180)
        (out / 'build.log').write_text(build.stdout + build.stderr, encoding='utf-8')
        if build.returncode:
            print(build.stdout + build.stderr, file=sys.stderr)
            return build.returncode
        test = subprocess.run([str(out / 'raster-decode')], cwd=out, text=True,
                              capture_output=True, timeout=120)
        (out / 'test.log').write_text(test.stdout + test.stderr, encoding='utf-8')
        (out / 'results.json').write_text(json.dumps({'libtiff': version, 'exit': test.returncode,
            'scope': 'production Load/Seek with real libtiff, host I/O and UE adapters',
            'stdout': test.stdout, 'stderr': test.stderr}, indent=2), encoding='utf-8')
        print('libtiff ' + version + '\n' + test.stdout + test.stderr)
        return test.returncode
    except (OSError, subprocess.SubprocessError) as error:
        print('Raster decoder test infrastructure failed: ' + str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
