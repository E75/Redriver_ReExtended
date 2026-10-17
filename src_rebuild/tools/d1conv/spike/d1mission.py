"""Driver 1 MISSION.BLK reader (format analysis spike, see plans/d1-missions.md).

MISSION.BLK = table of u32 offsets (last entry = file end), each mission a stream of
records. A record starts with an int opcode; its byte length comes from RECLEN
(fixed) or from a special rule (mirrors FUN_8006822c of the D1 executable).
"""
import argparse
import struct
from dataclasses import dataclass
from pathlib import Path

# fixed record lengths in bytes, by opcode
RECLEN: dict[int, int] = {
    1: 4, 3: 8, 4: 28, 7: 4, 8: 4, 9: 8, 10: 4, 11: 4, 12: 8, 13: 8, 14: 8, 15: 8, 16: 12, 17: 12,
    19: 12, 21: 4, 24: 8, 25: 16, 26: 4, 27: 4, 28: 4, 29: 4, 30: 4, 31: 4, 33: 40, 34: 8, 35: 8,
    36: 4, 37: 4, 38: 8, 39: 4, 40: 4, 41: 8, 48: 4, 49: 4, 50: 4, 51: 8, 52: 8, 53: 8, 54: 28,
    55: 4, 56: 4, 57: 8, 58: 4, 59: 8, 60: 8, 61: 4, 63: 4, 64: 8, 65: 8, 66: 4, 67: 8, 68: 8,
    69: 20, 71: 24, 74: 8, 77: 4, 78: 4, 80: 32, 82: 24, 83: 12, 84: 8, 85: 16, 87: 4, 88: 4,
    89: 4, 90: 4, 91: 4, 92: 12, 93: 16, 95: 20, 96: 12, 97: 12, 98: 16, 99: 4, 100: 4, 101: 4,
    102: 4, 103: 16, 105: 8, 106: 8, 107: 4, 108: 8, 109: 16, 110: 8, 111: 8, 112: 16, 113: 12,
    114: 8,
}
# script variables that hold text (opcode 2 then carries a length and the characters)
STRING_VARS = {0, 5, 7, 17, 20, 21, 22, 23, 36, 64, 65}
# script variables whose meaning is read from the target code (FUN_80069760); the others are
# tuning values that map to engine globals
VARNAMES: dict[int, str] = {
    3: "STATE", 13: "COPS_BLOCK_ARRIVAL", 32: "SPEED_GATE", 33: "SPEED_LIMIT",
    45: "MISSION_ACTIVE", 5: "FAIL_TEXT", 59: "AMBIENT_MODE", 60: "AMBIENT_MIN", 61: "AMBIENT_RANGE",
    7: "EARLY_TEXT", 12: "PLAYER_DAMAGE", 20: "LOSE_TAIL_TEXT", 21: "WRECK_TEXT", 23: "OTHER_FAIL_TEXT",
    42: "COP_SPEED_SCALE", 43: "COP_POWER_SCALE", 46: "MIN_COPS", 69: "BEACON", 70: "BEACON_SIZE",
}
OP_SETVAR = 2
OP_MESSAGE = 22
OP_STRING_LIST = 20
OP_TABLE = 70
OP_BLOB = 94
OP_ARRAY = 104
OP_INT_LIST = 86
LIST_END = 0x7FFFFFFF

# opcode -> (name, confirmed). confirmed = behaviour read in the D1 interpreter or prescan;
# False = guessed from the script data or only partly read, never use for conversion.
# NOP = both jump tables of the D1 interpreter (prescan and runtime) skip the opcode.
# SCENE_*: one passenger scene (pedestrians walk to or from the car, short camera): START begins
# it, WAIT yields until it is over, CHAR / ROUTE / CHAR_MODE describe the walkers and their
# 4 point route. Understood at block level only, the fields are not decoded.
# SLOT_MESSAGE [slot, display time, len]: text shown for a target slot.
# Targets (FUN_80069760): a slot is reached when the player is closer than its radius (op 25 / 69
# arg), then STATE becomes 0x7fff and WAIT_STATE continues. STATE 2 = waiting for a target,
# 0xff0 = timer ran out. EARLY_LIMIT [timer]: with EARLY_CHECK_ON a target only counts once the
# timer has dropped to that value ("Too early"). SPEED_GATE = 1: arrival also needs speed <=
# SPEED_LIMIT. COPS_BLOCK_ARRIVAL = 1: no arrival while heat >= 659 and cops are chasing.
# SPAWN_CAR [slot, type, palette, rotation, x, z] (FUN_80016100): creates a car 200 units above
# the ground. type = D1 traffic type, 1 cop car, 2 and 3 the two civilian cars (type t = slot t-1
# of the city's car list); palette 0..3 colour variant. Miami missions park the same two cars at
# the start. AMBIENT_MODE (var 59: 0 off, 1 on, 3 only while chased), AMBIENT_MIN / AMBIENT_RANGE
# (frames) and AMBIENT_LIST (a list of sound ids) make random ambient sounds (FUN_8006a114).
# MAX_COPS [n] (global 0x9719c, D1 default 1) and var 46 MIN_COPS bound the wanted number of cop cars:
# D2 maxCopCars / gMinimumCops (MRSetVariable). Vars 42 / 43 are the cop speed / power scale (4096 =
# 1.0, D2 gCopDesiredSpeedScale / gCopMaxPowerScale); 47-49 are their gain per heat level (no D2 twin).
# PLAYER_DAMAGE (var 12) scales the damage the player car takes (2048 = half). Text vars: FAIL_TEXT =
# end reason 1 (timer out), WRECK_TEXT = reason 2 (wreck check, D2 msgCarWrecked), EARLY_TEXT /
# LOSE_TAIL_TEXT = shown while an arrival rule blocks (slot flag bits 2 / 4). Op 67 and vars 69 / 70
# only change the target marker and a beacon effect.
# COPS_ON / COPS_OFF: the whole heat and cop system (FUN_800644bc) runs only while the chase flag
# (global 0x97134) is set; OFF also clears the cop count. Var 15 is the cop timing in frames.
# HEAT_MIN [n]: heat = max(heat, (n - 1) * 430 + 659); heat 0..4096 equals D2 felony
# (FELONY_PURSUIT_MIN_VALUE 658, FELONY_MAX_VALUE 4096). Timer values are 16384 per second.
OPNAMES: dict[int, tuple[str, bool]] = {
    1: ("END", True), 2: ("SETVAR", True), 4: ("TRIGGER3", True), 7: ("WAIT_STATE", True),
    8: ("TIMER_UP", True), 9: ("TIMER_DOWN", True), 10: ("FLAG_962CC_ON", True),
    11: ("FLAG_962CC_OFF", True), 12: ("INC", True), 14: ("DEC", True), 16: ("ADD", True),
    17: ("SUB", True), 19: ("START_POS", True), 20: ("TEST_POSITIONS", True), 22: ("MESSAGE", True),
    24: ("WAIT_SEC", True), 25: ("TARGET0", True), 26: ("TARGET0_ON", True),
    27: ("TARGET0_OFF", True), 28: ("TIMER_BIT_CLEAR", True), 29: ("TIMER_BIT_SET", True),
    30: ("CFG_964FC", True), 31: ("CFG_962A4", True), 33: ("TRIGGER5", True), 34: ("CITY", True),
    35: ("EARLY_LIMIT", True), 36: ("EARLY_CHECK_ON", True), 37: ("EARLY_CHECK_OFF", True),
    38: ("MAX_COPS", True), 39: ("CFG_GP101C", True), 40: ("CFG_GP1020", True),
    49: ("COPS_OFF", True), 50: ("COPS_ON", True), 52: ("START_ROT", True),
    48: ("NOP", True), 51: ("NOP", True), 68: ("NOP", True),
    53: ("HEAT_MIN", True), 54: ("SPAWN_CAR", True), 55: ("FLAGS11C_SET2", True),
    56: ("FLAGS11C_CLR2", True), 58: ("TIMER_CLEAR", True), 59: ("TIMER_ADD", True),
    60: ("TIMER_SUB", True), 61: ("CFG_962EC", True), 63: ("CFG_96B30", True),
    64: ("CFG_96290", True), 65: ("DAMAGE_LIMIT", True), 66: ("CFG_964FD", True),
    67: ("CFG_96213", True), 69: ("TARGET_SLOT", True), 70: ("SLOT_MESSAGE", True),
    71: ("SLOT_VALUES", True), 74: ("SET_BFC78", True), 77: ("SCENE_START", True),
    78: ("SCENE_WAIT", True), 80: ("SCENE_CHAR", True), 82: ("SCENE_ROUTE", True),
    83: ("SCENE_CHAR_MODE", True), 84: ("CFG_2DF", True), 85: ("EFFECT", False), 86: ("YIELD_LIST", True),
    87: ("STOP_IF_NO_96C14", True), 88: ("SAVE_PLAYER", True), 89: ("SAVE_RESTART", True),
    90: ("FLAGS11C_SET4", True), 91: ("FLAGS11C_CLR4", True), 92: ("SLOT_FLAGS_134", True),
    93: ("SET_BFC1C", True), 94: ("BLOB_128", True), 95: ("SET_C46C8", True),
    96: ("SLOT_FLAGS_13C", True), 97: ("SLOT_LINK_CAR", True), 98: ("SET_8A8D0", True),
    99: ("RESTART", True), 100: ("GOTO_START", True), 101: ("BEST_TIME", True),
    102: ("WAIT_SPAWN", True), 103: ("MARKER_PROPS", True), 104: ("AMBIENT_LIST", True),
    105: ("LABEL", True), 106: ("GOTO", True), 107: ("SPLIT_TIME", True),
    108: ("BEGIN_MISSION", True), 109: ("JUMP_IF_EQ", True), 110: ("CFG_96264", True),
    111: ("SET_BFC18", True), 112: ("HANDLER_FC", True), 113: ("SAVE_CAR", True),
    114: ("RESPAWN_CAR", True),
}


@dataclass
class Record:
    opcode: int
    args: list[int]
    text: str | None = None


def rec_len(words: tuple[int, ...], i: int) -> int:
    """Byte length of the record at word index i."""
    op = words[i]
    if op in RECLEN:
        return RECLEN[op]
    if op == OP_SETVAR:
        return words[i + 2] + 12 if words[i + 1] in STRING_VARS else 12
    if op == OP_MESSAGE:
        return words[i + 2] + 12
    if op == OP_STRING_LIST:
        return words[i + 1] * 12 + 8
    if op == OP_TABLE:
        return words[i + 3] + 16
    if op == OP_BLOB:
        return words[i + 1] + 8
    if op == OP_ARRAY:
        return (words[i + 1] << 2) + 8
    if op == OP_INT_LIST:
        j = i + 1
        while words[j] != LIST_END:
            j += 1
        return (j + 1 - i) * 4
    raise ValueError(f"unknown opcode {op}")


def parse(mission: bytes) -> list[Record]:
    """Split one mission into records; raises ValueError if the stream does not end exactly."""
    words = struct.unpack_from(f"<{len(mission) // 4}i", mission, 0)
    i, out = 0, []
    while i < len(words):
        n = rec_len(words, i) // 4
        if n <= 0 or i + n > len(words):
            raise ValueError(f"record at word {i} overruns the mission")
        op = words[i]
        if op == OP_MESSAGE or (op == OP_SETVAR and words[i + 1] in STRING_VARS):
            raw = mission[i * 4 + 12:i * 4 + 12 + words[i + 2]]
            out.append(Record(op, list(words[i + 1:i + 3]), raw.split(b"\0")[0].decode("latin1")))
        elif op == OP_TABLE:
            raw = mission[i * 4 + 16:i * 4 + 16 + words[i + 3]]
            out.append(Record(op, list(words[i + 1:i + 4]), raw.split(b"\0")[0].decode("latin1")))
        else:
            out.append(Record(op, list(words[i + 1:i + n])))
        i += n
    return out


def load(path: Path) -> list[bytes]:
    data = path.read_bytes()
    count = struct.unpack_from("<I", data, 0)[0] // 4
    offs = [*struct.unpack_from(f"<{count}I", data, 0), len(data)]
    return [data[offs[k]:offs[k + 1]] for k in range(count)]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("blk", type=Path, help="DRIVER1\\MISSIONS\\MISSION.BLK")
    ap.add_argument("--check", action="store_true", help="only verify that every mission parses")
    ap.add_argument("--coverage", action="store_true", help="list opcodes without a confirmed name")
    ap.add_argument("missions", type=int, nargs="*", help="mission numbers to print (default all)")
    args = ap.parse_args()
    missions = load(args.blk)
    if args.check:
        bad = 0
        for k, m in enumerate(missions):
            try:
                parse(m)
            except ValueError as error:
                bad += 1
                print(f"mission {k}: {error}")
        print(f"missions {sum(1 for m in missions if m)} bad {bad}")
        return
    if args.coverage:
        for k, m in enumerate(missions):
            unknown = sorted({r.opcode for r in parse(m) if not OPNAMES.get(r.opcode, ("", False))[1]})
            if unknown:
                print(f"mission {k}: unconfirmed opcodes {unknown}")
        return
    for k in args.missions or range(len(missions)):
        print(f"== mission {k}")
        for r in parse(missions[k]):
            name = OPNAMES.get(r.opcode, ("OP", False))[0]
            var = f" {VARNAMES[r.args[0]]}" if r.opcode == OP_SETVAR and r.args[0] in VARNAMES else ""
            print(f"  {name:16s}({r.opcode:3d}){var} {r.args}" + (f"  {r.text!r}" if r.text is not None else ""))


if __name__ == "__main__":
    main()
