"""Extract a named production C++ method for a narrowly scoped host adapter test."""
from pathlib import Path
import re


def extract(source: str, signature: str) -> str:
    """Preserve the method text, ignoring braces in comments and string literals."""
    start = source.find(signature)
    if start < 0 or source.find(signature, start + len(signature)) >= 0:
        raise ValueError(f'Expected exactly one definition of {signature}')
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda match: ' ' * len(match.group()), source, flags=re.DOTALL)
    opening = masked.find('{', start)
    if opening < 0:
        raise ValueError(f'Missing body of {signature}')
    depth = 0
    for end in range(opening, len(masked)):
        if masked[end] == '{':
            depth += 1
        elif masked[end] == '}':
            depth -= 1
            if depth == 0:
                return source[start:end + 1]
    raise ValueError(f'Unbalanced body of {signature}')


def prepare_eva(root: Path, output: Path) -> None:
    source = (root / 'Source/Star/Runtime/StarPlayerController.cpp').read_text(encoding='utf-8')
    load = extract(source, 'bool AStarPlayerController::LoadGame()')
    # Static integration guard, not an execution of Unreal LoadGame. Disposal
    # must follow the validated flight commit and precede settings/EVA spawning.
    if load.count('ClearEVAForLoad();') != 1:
        raise ValueError('LoadGame must dispose the old EVA actor exactly once')
    restore = load.index('if(!Ship->SetWorldUtc(SavedUtc)||!Ship->RestoreFlight(Pending))')
    cleanup = load.index('ClearEVAForLoad();')
    settings = load.index('Director->SetClockRate(SavedRate);')
    spawn = load.index('GetWorld()->SpawnActor<AStarEVAPawn>()')
    if not restore < load.index('return false;', restore) < cleanup < settings < spawn:
        raise ValueError('EVA disposal must be after restore failure handling, before the new actor')
    runtime = (root / 'Source/Star/Runtime/StarEVARuntime.cpp').read_text(encoding='utf-8')
    output.mkdir(parents=True, exist_ok=True)
    (output / 'eva-load-method.inc').write_text(
        extract(runtime, 'void AStarPlayerController::ClearEVAForLoad()') + '\n', encoding='utf-8')


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if not output.is_relative_to(root / 'work'):
        parser.error('Generated adapters must be written under this checkout\'s work/ directory')
    prepare_eva(root, output)
    print('EVA load ordering guard passed; extracted production cleanup method (not UE acceptance).')
