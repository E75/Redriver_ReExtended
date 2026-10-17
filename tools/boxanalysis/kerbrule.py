"""Prints the boxes the Driver 1 kerb wall rule removes, from a MAPEDITOR_BOXES.txt report (check of RemoveKerbWallBoxes in
src_rebuild/d1dlc/lev_format.h; keep both rules the same). Usage: python kerbrule.py <MAPEDITOR_BOXES.txt>
A low model (<= 250 high) with a box that is long (>= 1000), thin (<= 800) and stands at the clamp height (top >= 395) is a kerb
wall: the car stops at the kerb stone instead of driving up on the pavement. BARRIER models keep their boxes."""
import sys

from boxan import load

MAX_MODEL_HEIGHT = 250
MIN_BOX_TOP = 395
MIN_LONG = 1000
MAX_SHORT = 800


def main(report: str) -> None:
    for r in load(report):
        model_height = -r['my'][0]
        top = -(r['py'] - r['sy'] // 2)
        long_side, short_side = max(r['sx'], r['sz']), min(r['sx'], r['sz'])
        if model_height <= MAX_MODEL_HEIGHT and top >= MIN_BOX_TOP and long_side >= MIN_LONG and short_side <= MAX_SHORT and not r['name'].startswith('BARRIER'):
            print(f"model {r['t']} {r['name'] or '-'} box {r['i']}/{r['n']}: model height {model_height}, box top {top}, size {r['sx']} x {r['sz']}")


if __name__ == '__main__':
    main(sys.argv[1])
