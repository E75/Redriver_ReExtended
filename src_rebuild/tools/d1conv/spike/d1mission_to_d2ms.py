"""Prototype: Driver 1 mission script -> Driver 2 D2MS file (see plans/d1-missions.md).

Strict: a mission is converted only when every record is either mapped or on a short list of
records the D2 engine has no use for. Anything else raises Unsupported and the mission is
skipped. Nothing here is shipped game data: the input is the player's own MISSION.BLK.
"""
import argparse
import math
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import d1mission as d1  # noqa: E402  (same folder)

MISSION_IDENT = struct.unpack("<i", b"SM2D")[0]
HEADER_NAMES = [
    "id", "size", "city", "time", "weather", "startX", "startZ", "startRot", "type", "timerPacked",
    "strings", "copSpeed", "copPower", "copMin", "copMax", "msgCarWrecked", "msgOutOfTime",
    "msgComplete", "region", "route", "msgDrowned", "carModel", "carColour", "maxDamage",
    "res0", "res1", "res2", "res3", "res4", "nCutscenes", "title", "msgPoliceCar", "msgDoorsLocked",
]
HEADER_BYTES = len(HEADER_NAMES) * 4
MAX_TARGETS = 16
TARGET_INTS = 16
TIMER_COUNTER_FLAG = 0x8000
TIME_DAY = 1
TARGET_POINT = 1
POINT_DISPLAY = 0x70                       # minimap blip, direction arrow, 3D marker (99 of the 211 shown D2 point targets)

# D1 city id (op 34) -> index of the converted city (g_D1DlcCities)
D1_CITY = {0: 4, 1: 5, 3: 6, 5: 7}
D1_MISSION_BASE = 1000

# D2 script words
CMD_SHOW_MESSAGE = 0x1000050
CMD_BRANCH_IF = 0x1000001
CMD_SET_VARIABLE = 0x1000010
CMD_SET_FELONY = 0x1000030
CMD_TIMER_COUNT_UP = 0x1000040      # sets the counter flag of the player timer
CMD_TIMER_COUNT_DOWN = 0x1000041    # removes it
VAR_TIMER = 0x2000008               # seconds
CMD_COMPLETE = 0x1001001
CMD_STOP_THREAD = 0x1001000
FN_PROCESS_TARGET = 0x4000020
VAR_MAX_COP_CARS = 0x2000103

# records with no counterpart in the D2 mission logic (cosmetic, HUD, unused tuning); reported
# (104 + vars 59/60/61 = ambient sound chatter, 92 = far range of a target, only read by the arrival gates
# that the converter refuses anyway)
# 88 = state hand-over to the next story mission; the missions here are standalone
IGNORED_OPS = {10, 11, 20, 24, 28, 30, 31, 48, 51, 64, 67, 68, 88, 92, 104}
# SETVAR targets the D2 mission logic does not use here (var 15 = cop timing, D2 has its own); reported
IGNORED_VARS = {8, 9, 14, 15, 16, 28, 45, 47, 48, 49, 50, 52, 59, 60, 61, 69, 70}
# passenger scene (pedestrians walk to or from the car, short camera): the plan decides to skip it
SCENE_OPS = {74, 77, 78, 80, 82, 83, 112}
# a spawned car with one of these records around has a role (target, scene, save) the converter cannot map yet
CAR_ROLE_OPS = {20, 96, 97, 102, 113, 114}
# D1 traffic type of SPAWN_CAR -> (converted car model, cop). Type t is slot t-1 of the city's car list:
# 1 cop car, 2 and 3 the two civilian cars. Palette 0..3 are the colour variants.
CAR_TYPES = {1: (0, True), 2: (1, False), 3: (2, False)}
MAX_PALETTE = 3
TARGET_CAR = 2
CARTARGET_FLAG_ISCOP = 8
CMD_START_THREAD = 0x1001002
VAR_STATE, VAR_FAIL_TEXT, VAR_MAX_DAMAGE = 3, 5, 10
VAR_SPEED_GATE, VAR_SPEED_LIMIT = 32, 33
# new Point target flag (engine hook, section 8 of the plan): the target only counts while the player
# is slower than the limit stored in target int 12. D1 speed units are assumed to equal the D2 units
# of FIXEDH(wheel_speed) (both games list 56/97/138 style limits and 120/180 speeding thresholds):
# SPEED_UNIT_PERCENT is the calibration knob.
TARGET_FLAG_POINT_MAX_SPEED = 0x4000000
# second new flag: the target only counts once the countdown timer is down to the limit in int 13
# (D2 timer count, 3000 per second); the message offset in int 14 is shown while it blocks
TARGET_FLAG_POINT_MIN_TIMER = 0x8000000
SPEED_UNIT_PERCENT = 100
TIMER_COUNT_PER_SECOND = 3000
HEAT_BASE, HEAT_STEP, MAX_HEAT_LEVEL = 659, 430, 8   # HEAT_MIN [n]: heat (D2 felony) = base + (n - 1) * step
VAR_COP_SPEED, VAR_COP_POWER, VAR_PLAYER_DAMAGE, VAR_MIN_COPS = 42, 43, 12, 46
VAR_MIN_COP_CARS = 0x2000102
MAX_COPS_D1 = 3
VAR_SCENE_FILE, VAR_TIMER_RAW, TIMER_ACTIVE_MIN = 0, 1, 659
DEFAULT_LOSE_TAIL = "Lose the tail."
VAR_COPS_BLOCK, VAR_EARLY_TEXT, VAR_LOSE_TAIL_TEXT, VAR_WRECK_TEXT = 13, 7, 20, 21
STATE_WAIT_TARGET = 2
FRAMES_PER_SECOND = 30
TIMER_UNITS_PER_SECOND = 16384
MIN_DAMAGE, MAX_DAMAGE = 4096, 40000      # D1 fragile cars use 4096 / 8192, D2 missions 20000..40000

# Ram missions (STATE 5 + TARGET_SLOT, plan section 9): the D1 target car drives a recorded run (REPLAY.BLK) that
# needs the D1 physics. Adaptation (user choice A, then civilian AI): a chase car target (type 3) that the engine
# (mission.c IsDriver1ChaseCar) creates as a driving civilian AI car on the lane nearest to the given spot, a little
# ahead of the player; done once it has taken the D1 damage limit (var 31), the same rule as the D2 chases.
OP_TARGET_SLOT, OP_SLOT_VALUES, OP_SLOT_FLAGS_13C, OP_SLOT_LINK_CAR = 69, 71, 96, 97
RAM_SLOT_RECORDS = {OP_TARGET_SLOT: [0, 104857600, 0, 0], OP_SLOT_LINK_CAR: [0, 23], OP_SLOT_FLAGS_13C: [0, 65],
                    OP_SLOT_VALUES: [0, 750, 0, -200, 0]}      # identical in all ram missions
RAM_SETUP_OPS = {40, 61, 84}               # D1 chase game mode switches, replaced by the D2 chase rules
STATE_RAM = 5
OP_TIMER_CLEAR = 58                        # removes a running timer: nothing to do while none was started
VAR_ESCAPE_TEXT = 23                       # "Too late. Duval's got clear."
DEFAULT_ESCAPE_TEXT = "Too late. Mission failed."
TARGET_CHASE_CAR = 3                       # car target type: lead car that is chased
RAM_CAR_MODEL = 1                          # converted civilian car
VAR_RAM_MODE, VAR_RAM_DAMAGE, VAR_RAM_FLAG = 30, 31, 56   # D1 target code: ram mode on (1), damage that completes the ram, flag (1)
RAM_CAR_AHEAD = 4000                       # units in front of the player start
# start spots (x, z) where the D1 heading faces a closed wall in the converted world: the Miami garage yard (missions 0, 4, 5, 9)
TURNED_START_SPOTS = ((293839, -59343),)
RAM_CAR_DISPLAY = 0x70                     # blip, arrow and 3D marker like the D2 chases
NO_GETTING_FAR_MESSAGE = 255


class Unsupported(Exception):
    """The mission needs something the converter does not map yet."""


@dataclass
class D2Mission:
    header: dict[str, int]
    targets: list[list[int]]
    script: list[int]
    strings: bytes


@dataclass
class Result:
    data: bytes
    notes: list[str] = field(default_factory=list)


def pack_d2ms(m: D2Mission) -> bytes:
    """Header, MAX_TARGETS targets, script words, string block."""
    h = dict(m.header)
    h["strings"] = len(m.script)
    out = bytearray(struct.pack(f"<{len(HEADER_NAMES)}i", *[h[n] for n in HEADER_NAMES]))
    targets = [t + [0] * (TARGET_INTS - len(t)) for t in m.targets]
    targets += [[0] * TARGET_INTS] * (MAX_TARGETS - len(targets))
    for t in targets:
        out += struct.pack(f"<{TARGET_INTS}i", *t)
    out += struct.pack(f"<{len(m.script)}I", *m.script)
    return bytes(out) + m.strings


def unpack_d2ms(data: bytes) -> D2Mission:
    header = dict(zip(HEADER_NAMES, struct.unpack_from(f"<{len(HEADER_NAMES)}i", data, 0)))
    size, nwords = header["size"], header["strings"]
    targets = [list(struct.unpack_from(f"<{TARGET_INTS}i", data, size + i * TARGET_INTS * 4))
               for i in range(MAX_TARGETS)]
    start = size + MAX_TARGETS * TARGET_INTS * 4
    script = list(struct.unpack_from(f"<{nwords}I", data, start))
    return D2Mission(header, targets, script, data[start + nwords * 4:])


def new_header(city: int, start: tuple[int, int, int]) -> dict[str, int]:
    h = dict.fromkeys(HEADER_NAMES, 0)
    h.update(id=MISSION_IDENT, size=HEADER_BYTES, city=city, time=TIME_DAY, startX=start[0],
             startZ=start[1], startRot=start[2], copSpeed=4096, copPower=4096, copMax=2,
             msgOutOfTime=-1, msgComplete=-1, msgDrowned=-1, carModel=1, maxDamage=22000,
             res0=1, res1=2, res2=3, res4=10, title=-1, msgPoliceCar=-1, msgDoorsLocked=-1)
    return h


class Strings:
    """Zero terminated string block; add() returns the byte offset the script uses."""

    def __init__(self) -> None:
        self.blob = bytearray()
        self.index: dict[str, int] = {}

    def add(self, text: str) -> int:
        if text not in self.index:
            self.index[text] = len(self.blob)
            self.blob += text.encode("latin1") + b"\0"
        return self.index[text]



def set_timer(h: dict[str, int], seconds: int | None, counting_up: bool) -> None:
    flags = TIMER_COUNTER_FLAG if counting_up else 0
    h["timerPacked"] = ((flags << 16) | (seconds or 0)) & 0xFFFFFFFF
    if h["timerPacked"] >= 1 << 31:
        h["timerPacked"] -= 1 << 32


def check_d2ms(m: D2Mission) -> None:
    """Structural proof that the output obeys the D2 interpreter's rules; raises Unsupported if not."""
    s, n = m.script, len(m.script)
    starts = {0} | {i + 1 for i, b in enumerate(m.strings[:-1]) if b == 0}
    for name in ("msgCarWrecked", "msgOutOfTime", "msgComplete", "msgDrowned"):
        if m.header[name] not in starts:
            raise Unsupported(f"check: {name} is not a string start")
    for t in m.targets:
        if t[0] == TARGET_POINT:
            if t[8] != -1 and t[8] not in starts:
                raise Unsupported("check: lose-tail text offset")
            if t[1] & TARGET_FLAG_POINT_MIN_TIMER and t[14] not in starts:
                raise Unsupported("check: early text offset")
        if t[0] == TARGET_CAR and t[9] == TARGET_CHASE_CAR and t[14] not in starts:
            raise Unsupported("check: chase car message offset")
    if not n or s[-1] != CMD_STOP_THREAD:
        raise Unsupported("check: script does not end with StopThread")
    loops = 0
    for i, w in enumerate(s):
        if w == CMD_SHOW_MESSAGE and (i < 2 or s[i - 1] not in starts or not 0 < s[i - 2] < 100):
            raise Unsupported(f"check: message at word {i}")
        if w == FN_PROCESS_TARGET and (i < 1 or s[i - 1] >= MAX_TARGETS or m.targets[s[i - 1]][0] == 0):
            raise Unsupported(f"check: ProcessTarget at word {i} uses an empty target")
        if w == CMD_BRANCH_IF:
            if i < 3 or s[i - 1] != 0xFFFFFFFC or s[i - 2] != FN_PROCESS_TARGET:
                raise Unsupported(f"check: BranchIf at word {i} is not a ProcessTarget loop")
            loops += 1
        if w == CMD_START_THREAD:
            start = i + 1 + s[i - 1]
            if not 0 <= start < n - 4 or s[start + 1] != FN_PROCESS_TARGET or s[start + 4] != CMD_STOP_THREAD:
                raise Unsupported(f"check: thread start at word {i}")
    if not loops:
        raise Unsupported("check: no target loop")


def convert(mission: bytes) -> Result:
    recs = d1.parse(mission)
    names = {op: n for op, (n, _) in d1.OPNAMES.items()}
    first = {}
    for r in recs:
        first.setdefault(r.opcode, r)
    for op in (19, 52, 34):
        if op not in first:
            raise Unsupported(f"header record {names[op]} missing")
    city = D1_CITY.get(first[34].args[0])
    if city is None:
        raise Unsupported(f"city id {first[34].args[0]}")

    x, z = first[19].args
    rot = first[52].args[0] & 4095
    if (x, z) in TURNED_START_SPOTS:
        rot = (rot + 2048) & 4095
    h = new_header(city, (x, z, rot))
    notes: list[str] = []
    strings = Strings()
    targets: list[list[int]] = []
    script: list[int] = []
    fail_text = complete_text = None
    pending: list[int] | None = None
    pending_added = False
    gate_on, gate_limit = False, 0
    early_on, early_limit = False, 0
    cops_block = False
    early_text = lose_text = wreck_text = None
    cops_on, max_cops, min_cops = False, 1, 0
    timer_started = timer_up = False
    damage_factor = 4096
    has_cops = any(x.opcode == 50 for x in recs)
    waiting = title_seen = False
    car_threads: list[tuple[int, int]] = []   # (script index of the address push, target index)
    waits = 0
    point_index: int | None = None
    last_wait = max((i for i, r in enumerate(recs) if r.opcode == 7), default=-1)
    has_ram_slot = any(x.opcode == OP_TARGET_SLOT for x in recs)
    ram_records: set[int] = set()         # slot records seen so far (all four needed before STATE 5)
    ram_waiting = ram_done = False
    ram_index, ram_damage = -1, None
    escape_text = None

    def add_pending() -> None:
        nonlocal point_index, pending_added
        if len(targets) >= MAX_TARGETS:
            raise Unsupported("more than 16 targets")
        targets.append(list(pending) + [0] * (15 - len(pending)))
        point_index = len(targets) - 1
        pending_added = True

    def apply_arrival_rules(target: list[int]) -> None:
        """The D1 target code reads these variables live while waiting, so they apply at WAIT_STATE."""
        if gate_on:
            target[1] |= TARGET_FLAG_POINT_MAX_SPEED
            target[12] = gate_limit
            notes.append(f"arrival speed limit {gate_limit} (engine flag)")
        if early_on:
            if timer_up:
                raise Unsupported("EARLY_CHECK while the timer counts up")
            if early_text is None:
                raise Unsupported("EARLY_CHECK without text")
            target[1] |= TARGET_FLAG_POINT_MIN_TIMER
            target[13] = early_limit * TIMER_COUNT_PER_SECOND // TIMER_UNITS_PER_SECOND
            target[14] = strings.add(early_text)
            notes.append("arrival only when the timer is low enough (engine flag)")
        if cops_block:
            target[8] = strings.add(lose_text or DEFAULT_LOSE_TAIL)       # D2's own lose-the-tail rule
            notes.append("lose the tail rule (D2 loseTailMessage)")

    for i, r in enumerate(recs):
        op = r.opcode
        if op == 22:
            if not title_seen:
                title_seen = True
                notes.append("title not shown in the mission (menu uses it)")
            elif i > last_wait:
                complete_text = r.text
            else:
                script += [max(1, r.args[0] // FRAMES_PER_SECOND), strings.add(r.text or ""), CMD_SHOW_MESSAGE]
        elif op == 2:
            var, value = r.args
            if var == VAR_FAIL_TEXT and r.text is not None:
                if fail_text not in (None, r.text):
                    raise Unsupported("two different timeout texts")
                fail_text = r.text
            elif var == VAR_STATE and value == STATE_RAM:
                if ram_records != set(RAM_SLOT_RECORDS) or ram_damage is None:
                    raise Unsupported("STATE 5 without a complete target slot and damage limit")
                if pending is not None or ram_done:
                    raise Unsupported("ram target together with another target")
                ram_waiting = True
            elif var == VAR_STATE:
                if value != STATE_WAIT_TARGET:
                    raise Unsupported(f"STATE {value}")
                waiting = True
            elif var == VAR_ESCAPE_TEXT and r.text is not None:
                escape_text = r.text
            elif var in (VAR_RAM_MODE, VAR_RAM_FLAG) and has_ram_slot:
                if value != 1:
                    raise Unsupported(f"SETVAR {var} = {value}")
            elif var == VAR_RAM_DAMAGE and has_ram_slot:
                if not MIN_DAMAGE <= value <= MAX_DAMAGE:
                    raise Unsupported(f"ram damage limit {value}")
                ram_damage = value
            elif var == VAR_EARLY_TEXT and r.text is not None:
                early_text = r.text
            elif var == VAR_LOSE_TAIL_TEXT and r.text is not None:
                lose_text = r.text
            elif var == VAR_WRECK_TEXT and r.text is not None:
                wreck_text = r.text
            elif var in (VAR_COP_SPEED, VAR_COP_POWER):
                if not 2048 <= value <= 8192:
                    raise Unsupported(f"cop scale var {var} = {value}")
                h["copSpeed" if var == VAR_COP_SPEED else "copPower"] = value
            elif var == VAR_PLAYER_DAMAGE:
                if not 1024 <= value <= 8192:
                    raise Unsupported(f"player damage factor {value}")
                damage_factor = value
            elif var == VAR_MIN_COPS:
                if not 0 <= value <= MAX_COPS_D1:
                    raise Unsupported(f"minimum cops {value}")
                min_cops = value
                if cops_on:
                    script += [value, VAR_MIN_COP_CARS, CMD_SET_VARIABLE]
            elif var == VAR_COPS_BLOCK:
                if value not in (0, 1):
                    raise Unsupported(f"COPS_BLOCK_ARRIVAL {value}")
                cops_block = bool(value)
            elif var == VAR_SPEED_GATE:
                if value not in (0, 1):
                    raise Unsupported(f"SPEED_GATE {value}")
                gate_on = bool(value)
            elif var == VAR_SPEED_LIMIT:
                gate_limit = value * SPEED_UNIT_PERCENT // 100
            elif var == VAR_MAX_DAMAGE:
                if not MIN_DAMAGE <= value <= MAX_DAMAGE:
                    raise Unsupported(f"var 10 = {value}")
                h["maxDamage"] = value
            elif var == VAR_SCENE_FILE and r.text is not None:
                notes.append("scene file name ignored")
            elif var == VAR_TIMER_RAW:
                # var 1 is the D1 mission timer itself; a value below 659 (0x293) right after TIMER_UP is the count-up start (0)
                if value >= TIMER_ACTIVE_MIN or not timer_up:
                    raise Unsupported(f"SETVAR timer = {value}")
            elif var not in IGNORED_VARS or r.text is not None:
                raise Unsupported(f"SETVAR {var}")
        elif op in (8, 9):
            counting_up = op == 8
            seconds = 0 if counting_up else r.args[0] // TIMER_UNITS_PER_SECOND
            if not timer_started:
                if point_index is not None:
                    raise Unsupported("timer first started after the first target")
                set_timer(h, seconds, counting_up)
                timer_started = True
            else:
                # a later timer phase: switch the D2 timer mode and set its value
                script += [CMD_TIMER_COUNT_UP if counting_up else CMD_TIMER_COUNT_DOWN,
                           seconds, VAR_TIMER, CMD_SET_VARIABLE]
            timer_up = counting_up
        elif op == 25:
            radius, tx, tz = r.args
            pending = [TARGET_POINT, 0, POINT_DISPLAY, tx, tz, radius, 0, 0, -1]
            pending_added = False
        elif op == 26:
            if pending is None:
                raise Unsupported("TARGET0_ON without a target")
            if not pending_added:
                add_pending()
            waiting = True        # switching a target on arms the wait (STATE keeps its value 2)
        elif op == 7 and ram_waiting:
            # ram the target car: a chase car a little ahead of the player start (the engine puts it on the nearest lane)
            if len(targets) >= MAX_TARGETS:
                raise Unsupported("more than 16 targets")
            angle = h["startRot"] * 2 * math.pi / 4096
            ram_index = len(targets)
            targets.append([TARGET_CAR, 0, RAM_CAR_DISPLAY,
                            h["startX"] + math.floor(math.sin(angle) * RAM_CAR_AHEAD + 0.5),
                            h["startZ"] + math.floor(math.cos(angle) * RAM_CAR_AHEAD + 0.5),
                            h["startRot"], -1, RAM_CAR_MODEL, 0, TARGET_CHASE_CAR, 0, 0, 0, ram_damage,
                            0, NO_GETTING_FAR_MESSAGE])      # int 14 (message when he gets away) is set at the end
            script += [ram_index, FN_PROCESS_TARGET, -4 & 0xFFFFFFFF, CMD_BRANCH_IF]
            ram_waiting = False
            ram_done = True
            waits += 1
            notes.append("ram target = civilian AI chase car (approximation, D1 replay needs D1 physics)")
        elif op == 7:
            # TARGET0 alone already makes the target count: later legs of a mission often have no TARGET0_ON
            if pending is not None and not pending_added:
                add_pending()
            if not waiting or point_index is None:
                raise Unsupported("WAIT_STATE without a target")
            apply_arrival_rules(targets[point_index])
            script += [point_index, FN_PROCESS_TARGET, -4 & 0xFFFFFFFF, CMD_BRANCH_IF]
            waiting = False
            waits += 1
        elif op in RAM_SLOT_RECORDS:
            if op in ram_records or r.args != RAM_SLOT_RECORDS[op] or (op != OP_TARGET_SLOT and OP_TARGET_SLOT not in ram_records):
                raise Unsupported(f"target slot record {op} {r.args}")
            ram_records.add(op)
        elif op in RAM_SETUP_OPS and has_ram_slot:
            notes.append("D1 chase mode switch replaced by the D2 chase rules")
        elif op == 54:
            _slot, car_type, palette, rot, cx, cz = r.args
            if car_type not in CAR_TYPES or not 0 <= palette <= MAX_PALETTE:
                raise Unsupported(f"SPAWN_CAR type {car_type} palette {palette}")
            if any(x.opcode in CAR_ROLE_OPS for x in recs):
                raise Unsupported("spawned car has a role (target, scene or save)")
            if len(targets) >= MAX_TARGETS:
                raise Unsupported("more than 16 targets")
            model, cop = CAR_TYPES[car_type]
            targets.append([TARGET_CAR, 0, 0, cx, cz, rot & 4095, -1, model, palette, 0,
                            CARTARGET_FLAG_ISCOP if cop else 0, 0, 0])
            car_threads.append((len(script), len(targets) - 1))
            script += [0, CMD_START_THREAD]       # address patched below
        elif op == 53:
            level = r.args[0]
            if not 0 <= level <= MAX_HEAT_LEVEL:
                raise Unsupported(f"HEAT_MIN {level}")
            if level:
                script += [HEAT_BASE + (level - 1) * HEAT_STEP, CMD_SET_FELONY]
                notes.append("start heat set as D2 felony")
        elif op == 38:
            if not 0 <= r.args[0] <= MAX_COPS_D1:
                raise Unsupported(f"max cops {r.args[0]}")
            max_cops = r.args[0]
            if cops_on:
                script += [max_cops, VAR_MAX_COP_CARS, CMD_SET_VARIABLE]
        elif op in (49, 50):
            # cops on during a chase leg, off again after it; a mission without COPS_ON never has cops
            cops_on = op == 50
            if has_cops:
                script += [max_cops if cops_on else 0, VAR_MAX_COP_CARS, CMD_SET_VARIABLE]
                if cops_on and min_cops:
                    script += [min_cops, VAR_MIN_COP_CARS, CMD_SET_VARIABLE]
        elif op == 35:
            early_limit = r.args[0]
        elif op in (36, 37):
            early_on = op == 36
        elif op == 55:
            notes.append("wreck check = D2 default")
        elif op == OP_TIMER_CLEAR:
            if timer_started:
                raise Unsupported("TIMER_CLEAR after a timer started")
        elif op in (19, 52, 34, 108):
            pass
        elif op == 1:
            if i != len(recs) - 1:
                raise Unsupported("END in the middle")
            script += [CMD_COMPLETE, CMD_STOP_THREAD]
        elif op in SCENE_OPS:
            notes.append("passenger scene skipped")
        elif op in IGNORED_OPS:
            notes.append(f"ignored {names[op]}")
        else:
            raise Unsupported(f"{names.get(op, 'OP')}({op})")

    if not waits:
        raise Unsupported("no target to reach")
    # one helper thread per parked car: ProcessTarget every frame creates the car when the player is near
    for push_at, target in car_threads:
        script[push_at] = len(script) - (push_at + 2)    # pc is behind the command when the thread starts
        script += [target, FN_PROCESS_TARGET, -4 & 0xFFFFFFFF, CMD_BRANCH_IF, CMD_STOP_THREAD]
    h["maxDamage"] = h["maxDamage"] * 4096 // damage_factor      # half damage taken = twice the capacity
    h["msgCarWrecked"] = strings.add(wreck_text or "You wrecked your vehicle.")
    h["msgOutOfTime"] = strings.add(fail_text or "Out of time")
    h["msgComplete"] = strings.add(complete_text or "Well done.")
    h["msgDrowned"] = strings.add("You drowned.")
    if ram_done:
        targets[ram_index][14] = strings.add(escape_text or fail_text or DEFAULT_ESCAPE_TEXT)
    out = D2Mission(h, targets, script, bytes(strings.blob))
    check_d2ms(out)
    return Result(pack_d2ms(out), notes)


def selftest(blk: Path) -> None:
    """Rebuild every real Driver 2 mission from its parsed parts and compare byte for byte."""
    raw = blk.read_bytes()
    ok = bad = 0
    for n in range(2048 // 4):
        entry = struct.unpack_from("<I", raw, n * 4)[0]
        data = raw[entry & 0x7FFFF:(entry & 0x7FFFF) + (entry >> 19)]
        if not entry or data[:4] != b"SM2D":
            continue
        if pack_d2ms(unpack_d2ms(data)) == data:
            ok += 1
        else:
            bad += 1
            print(f"mission {n}: rebuild differs")
    print(f"D2MS layout selftest: {ok} missions rebuilt identically, {bad} differ")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("blk", type=Path, nargs="?", help="DRIVER1\\MISSIONS\\MISSION.BLK")
    ap.add_argument("--out", type=Path, help="write M<1000+n>.D2MS files here")
    ap.add_argument("--selftest", type=Path, help="DRIVER2\\MISSIONS\\MISSIONS.BLK to verify the layout")
    ap.add_argument("missions", type=int, nargs="*", help="mission numbers (default: the 44 story missions)")
    args = ap.parse_args()
    if args.selftest:
        selftest(args.selftest)
    if not args.blk:
        return
    missions = d1.load(args.blk)
    done = 0
    for n in args.missions or range(44):
        try:
            res = convert(missions[n])
        except Unsupported as why:
            print(f"mission {n}: skipped: {why}")
            continue
        done += 1
        print(f"mission {n}: ok, {len(res.data)} bytes, notes: {'; '.join(sorted(set(res.notes)))}")
        if args.out:
            args.out.mkdir(parents=True, exist_ok=True)
            (args.out / f"M{D1_MISSION_BASE + n}.D2MS").write_bytes(res.data)
    print(f"converted {done}")


if __name__ == "__main__":
    main()
