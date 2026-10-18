"""Packs recorded chase routes (one JSON per variant, made by the player's own offline run) into DRIVER1\\CONVERTED\\M<1000 + script>.CHASE.

Format (little endian), see docs/dev-notes/d1-chase-paths.md:
  char magic[4] = "D1CP", int32 version = 1, int32 variants
  per variant: int32 frames, then per frame: int32 x, int32 z, int16 y, int16 yaw (4096 = full turn)
Only the frames up to reliable_frames (plus one for the speed of the last) are stored.
The route files are the player's own data and never go into the repository.
"""
import argparse
import json
import struct
from pathlib import Path

GROUP_TO_SCRIPT = [6, 11, 24, 27, 35, 40]		# replay group k = story script that starts the chase
MISSION_BASE = 1000
MAGIC = b'D1CP'
VERSION = 1


def pack_group(paths: list[Path]) -> bytes:
    out = bytearray(MAGIC + struct.pack('<ii', VERSION, len(paths)))
    for path in paths:
        route = json.loads(path.read_text(encoding='utf-8'))
        frames = route['frames'][:route['reliable_frames'] + 1]
        out += struct.pack('<i', len(frames))
        for frame in frames:
            out += struct.pack('<iihh', frame['x'], frame['z'], frame['y'], frame['yaw'])
    return bytes(out)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('routes', type=Path, help='folder with group<g>_variant<vv>_entry<n>.json files')
    parser.add_argument('converted', type=Path, help='<install>\\DRIVER1\\CONVERTED')
    args = parser.parse_args()

    for group, script in enumerate(GROUP_TO_SCRIPT):
        paths = sorted(args.routes.glob(f'group{group}_variant*_entry*.json'))
        if not paths:
            continue
        target = args.converted / f'M{MISSION_BASE + script}.CHASE'
        target.write_bytes(pack_group(paths))
        print(f'{target.name}: {len(paths)} variants')


if __name__ == '__main__':
    main()
