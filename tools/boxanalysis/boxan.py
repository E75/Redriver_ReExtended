"""Reads the MAPEDITOR_BOXES.txt report that the game writes with -editortest (every collision box next to the vertex box of its
model) and lists the boxes of low models. This is how the Driver 1 kerb wall rule (RemoveKerbWallBoxes in lev_format.h) was found.
Usage: python boxan.py <MAPEDITOR_BOXES.txt> [max model height, default 400]"""
import re
import sys
from pathlib import Path

R = re.compile(r'(\d+) (\S*) box (\d+)/(\d+) type (\d+) flags (\d+) pos (-?\d+) (-?\d+) (-?\d+) yang (-?\d+) size (\d+) (\d+) (\d+) \| model x (-?\d+)\.\.(-?\d+) y (-?\d+)\.\.(-?\d+) z (-?\d+)\.\.(-?\d+)')


def load(path: str) -> list[dict]:
    rows = []
    for l in Path(path).read_text(encoding='utf-8').splitlines():
        m = R.match(l)
        if not m:
            continue
        t, name, i, n, ty, fl, px, py, pz, ya, sx, sy, sz, x0, x1, y0, y1, z0, z1 = m.groups()
        t, i, n, fl, px, py, pz, ya, sx, sy, sz, x0, x1, y0, y1, z0, z1 = map(int, (t, i, n, fl, px, py, pz, ya, sx, sy, sz, x0, x1, y0, y1, z0, z1))
        rows.append(dict(t=t, name=name, i=i, n=n, flags=fl, px=px, py=py, pz=pz, yang=ya, sx=sx, sy=sy, sz=sz, mx=(x0, x1), my=(y0, y1), mz=(z0, z1)))
    return rows


def main(report: str, maxh: int) -> None:
    for r in load(report):
        mh = -r['my'][0]
        if mh <= maxh:
            top = -(r['py'] - r['sy'] // 2)
            print('%4d %-18s #%d/%d model h %4d box top %4d h %4d size %4d x %4d  pos %5d %5d %5d fl %d' % (r['t'], r['name'], r['i'], r['n'], mh, top, r['sy'], r['sx'], r['sz'], r['px'], r['py'], r['pz'], r['flags']))


if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 400)
