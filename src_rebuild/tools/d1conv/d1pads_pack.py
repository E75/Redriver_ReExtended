"""Packs the recorded pad streams of the Driver 1 chase cars (the player's own REPLAY.BLK) into DRIVER1\\CONVERTED\\M<1000 + script>.PADS.

Format (little endian), next to the .CHASE file of the same mission, same variant order:
  char magic[4] = "D1PD", int32 version = 1, int32 variants
  per variant: int32 frames, int32 startX, startY, startZ, int32 startYaw (4096 = full turn), int32 carModel,
  then per frame: uint16 pad, int8 steer
pad = recorded pad word masked like the original (0xa0fc); bit 0 is set when the frame uses analogue steering
(the original also steers analogue with a stick value of 0, so steer alone cannot tell it apart).
steer = stick value from the 16 byte steering table of the game data (0 for digital frames).
Pad frame f is the input applied to route frame f of the .CHASE file (frame 0 = start state), repeat counts unrolled
(records of 3 bytes: pad high, pad low, repeat; 1000 of them before the other entry data, which long runs read on into).
startY is the stored value; the original ignores it and puts the car at ground height + 163.
carModel = the chase car model the mission script sets (0 when the script sets none).
The input files are the player's own disc files; nothing from them goes into the repository.
"""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / 'spike'))
import d1mission  # noqa: E402  (mission script parser in spike/)

GROUP_TO_SCRIPT = [6, 11, 24, 27, 35, 40]		# replay group k = story script that starts the chase
MISSION_BASE = 1000
MAGIC = b'D1PD'
VERSION = 1
ENTRIES = 362
VARIANTS_PER_GROUP = 20
START_OFFSET = 4600
PAD_MASK = 0xa0fc
PAD_ANALOGUE = 0x0001
OP_CHASE_CAR_MODEL = 67


def steer_table(data: bytes, offset: int) -> tuple[int, ...]:
    table = struct.unpack_from('<16b', data, offset)
    if table[0] != 0 or table[8] != 0 or any(table[9 + k] != -table[1 + k] for k in range(7)):
        raise ValueError(f'no steering table at file offset {offset:#x} (wrong file or offset?)')
    return table


def chase_car_model(mission: bytes) -> int:
    return next((r.args[0] for r in d1mission.parse(mission) if r.opcode == OP_CHASE_CAR_MODEL), 0)


def pack_variant(entry: bytes, steer: tuple[int, ...], car_model: int) -> bytes:
    frames, x, y, z, yaw = struct.unpack_from('<4ih', entry, START_OFFSET)
    out = bytearray(struct.pack('<6i', frames, x, y, z, yaw & 4095, car_model))
    done = 0
    for i in range(len(entry) // 3):		# the original reader has no record limit: long runs read on past record 1000
        b0, b1, repeat = entry[3 * i:3 * i + 3]
        nibble = b0 & 15
        pad = ((b0 << 8) | b1) & PAD_MASK | (PAD_ANALOGUE if nibble else 0)
        count = min(repeat + 1, frames - done)
        out += struct.pack('<Hb', pad, steer[nibble]) * count
        done += count
        if done == frames:
            return bytes(out)
    raise ValueError(f'pad stream ends after {done} of {frames} frames')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('replay', type=Path, help='DRIVER\\REPLAYS\\REPLAY.BLK')
    parser.add_argument('data', type=Path, help='game data file that holds the steering table')
    parser.add_argument('steer_offset', type=lambda s: int(s, 0), help='file offset of the 16 byte steering table in that file')
    parser.add_argument('missions', type=Path, help='DRIVER\\MISSIONS\\MISSION.BLK')
    parser.add_argument('converted', type=Path, help='<install>\\DRIVER1\\CONVERTED')
    args = parser.parse_args()

    steer = steer_table(args.data.read_bytes(), args.steer_offset)
    replay = args.replay.read_bytes()
    offsets = struct.unpack_from(f'<{ENTRIES}i', replay, 0)
    missions = d1mission.load(args.missions)

    for group, script in enumerate(GROUP_TO_SCRIPT):
        car_model = chase_car_model(missions[script])
        first = group * VARIANTS_PER_GROUP
        entries = [o for o in offsets[first:first + VARIANTS_PER_GROUP] if o >= 0]
        out = bytearray(MAGIC + struct.pack('<ii', VERSION, len(entries)))
        for offset in entries:
            out += pack_variant(replay[offset:offset + START_OFFSET + 20], steer, car_model)
        target = args.converted / f'M{MISSION_BASE + script}.PADS'
        target.write_bytes(bytes(out))
        print(f'{target.name}: {len(entries)} variants, car model {car_model}')


if __name__ == '__main__':
    main()
