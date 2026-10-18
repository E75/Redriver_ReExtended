#!/usr/bin/env python3
"""Command line API of the car editor: read and fix car.json files and take game shots of a car.

  carapi.py --cars <dir> list
  carapi.py --cars <dir> get <id>
  carapi.py --cars <dir> set <id> <point> <x> <y> <z> [--single]   point: headlight.L brake.R indicatorFront.L ... exhaust police wheel0..3
  carapi.py --cars <dir> nudge <id> <point> <dx> <dy> <dz> [--single]
  carapi.py --cars <dir> radius <id> <r>
  carapi.py --cars <dir> shots <id> [<id>...] --game <install dir> --out <dir> [--views front,side,rear] [--sheet name.png]

A pair point (L or R) moves its mirror image over x = cog.x too unless --single. The game reads the same folder with
-carsdir <dir>. Ids are <city>_<model>, see index of the export.
"""
import argparse
import json
import subprocess
import time
from pathlib import Path

PAIRS = ['headlight', 'brake', 'indicatorFront', 'indicatorBack', 'reverse']
D2_MISSION = {'chicago': 50, 'havana': 20, 'vegas': 26, 'rio': 32}
D1_CITY = {'miami': 4, 'frisco': 5, 'la': 6, 'newyork': 7, 'newcastle': 8, 'training': 9}
VIEWS = {'front': 4096, 'side': 3072, 'rear': 2048, 'side2': 1024}


def car_path(cars: Path, cid: str) -> Path:
    return cars / cid / 'car.json'


def load(cars: Path, cid: str) -> dict:
    with car_path(cars, cid).open(encoding='utf-8') as f:
        return json.load(f)


def update_raw(j: dict) -> None:
    """The values as the game stores them (see docs/dev-notes/car-editor.md)."""
    cog = j['cog']

    def rel(p: list[int]) -> list[int]:
        return [p[0] - cog[0], p[1] - cog[1], p[2] - cog[2]]

    old = j['raw']
    L = j['lamps']
    j['raw'] = {
        'headLight': rel(L['headlight']['L']), 'frontInd': rel(L['indicatorFront']['L']), 'backInd': rel(L['indicatorBack']['L']),
        'brakeLight': rel(L['brake']['L']), 'revLight': rel(L['reverse']['L']),
        'policeLight': [old['policeLight'][0], L['police'][1] - cog[1], L['police'][2] - cog[2]],
        'exhaust': rel(L['exhaust']), 'smoke': old['smoke'], 'fire': old['fire'], 'cog': cog[:], 'wheelSize': j['wheelRadius'],
        'wheelDisp': [[w['hubX'] - w['pos'][0], 14 - w['compression'] - j['wheelRadius'] - w['pos'][1], -w['pos'][2]] for w in j['wheels']],
    }


def fmt(j: dict) -> str:
    def v(a: list[int]) -> str:
        return '[' + ', '.join(str(int(x)) for x in a) + ']'

    r = j['raw']
    s = '{\n  "schema": %s,\n  "id": %s,\n  "name": %s,\n' % (json.dumps(j['schema']), json.dumps(j['id']), json.dumps(j['name']))
    s += '  "source": %s,\n  "units": %s,\n  "frame": %s,\n' % (json.dumps(j['source']), json.dumps(j['units']), json.dumps(j['frame']))
    s += '  "mesh": %s,\n  "cog": %s,\n  "wheelRadius": %d,\n  "wheels": [\n' % (json.dumps(j['mesh']), v(j['cog']), j['wheelRadius'])
    s += ',\n'.join('    { "axle": "%s", "hubX": %d, "compression": %d, "pos": %s }' % (w['axle'], w['hubX'], w['compression'], v(w['pos'])) for w in j['wheels'])
    s += '\n  ],\n  "lamps": {\n'
    s += ',\n'.join('    "%s": { "L": %s, "R": %s }' % (k, v(j['lamps'][k]['L']), v(j['lamps'][k]['R'])) for k in PAIRS) + ',\n'
    s += '    "exhaust": %s,\n    "police": %s\n  },\n  "raw": {\n' % (v(j['lamps']['exhaust']), v(j['lamps']['police']))
    s += ',\n'.join('    "%s": %s' % (k, v(r[k])) for k in ['headLight', 'frontInd', 'backInd', 'brakeLight', 'revLight', 'policeLight', 'exhaust', 'smoke', 'fire', 'cog']) + ',\n'
    s += '    "wheelSize": %d,\n    "wheelDisp": [%s]\n  }' % (r['wheelSize'], ', '.join(v(d) for d in r['wheelDisp']))
    if 'textures' in j:
        s += ',\n  "textures": [\n' + ',\n'.join('    ' + json.dumps(t) for t in j['textures']) + '\n  ]'
    s += '\n}\n'
    return s


def save(cars: Path, j: dict) -> None:
    update_raw(j)
    with car_path(cars, j['id']).open('w', encoding='utf-8', newline='') as f:
        f.write(fmt(j))


def point(j: dict, name: str) -> tuple[list[int], list[int] | None]:
    """the list that holds a point, and the mirror image of it (or None)"""
    if name.startswith('wheel') and name != 'wheelRadius':
        i = int(name[5:])
        other = [k for k in range(4) if k != i and j['wheels'][k]['axle'] == j['wheels'][i]['axle']][0]
        return j['wheels'][i]['pos'], j['wheels'][other]['pos']
    if '.' in name:
        k, side = name.split('.')
        return j['lamps'][k][side], j['lamps'][k]['R' if side == 'L' else 'L']
    return j['lamps'][name], None


def set_point(j: dict, name: str, p: list[int], single: bool) -> None:
    dst, mir = point(j, name)
    dst[:] = [int(x) for x in p]
    if mir is not None and not single:
        mir[:] = [2 * j['cog'][0] - int(p[0]), int(p[1]), int(p[2])]


def launch_args(cid: str) -> list[str]:
    city = cid.rsplit('_', 1)[0]
    if city in D2_MISSION:
        return ['-mission', str(D2_MISSION[city])]
    return ['-city', str(D1_CITY[city]), '-mission', '50']


def shot(game: Path, cars: Path, cid: str, view: str, out: Path, frame: int = 150) -> Path:
    """one game screenshot of the car in the showroom, cropped to the car; returns the png path"""
    from PIL import Image
    exe = game / 'REDRIVER2_dev.exe'
    bmp = game / 'SCREENSHOT.BMP'
    bmp.unlink(missing_ok=True)
    args = [str(exe)] + launch_args(cid) + ['-carspawner', '0', '-spawnid', cid, '-spawnangle', str(VIEWS[view]), '-lightson',
                                          '-carsdir', str(cars.resolve()), '-shot', str(frame)]
    p = subprocess.Popen(args, cwd=game)
    t = time.time()
    while p.poll() is None and not bmp.exists() and time.time() - t < 240:
        time.sleep(0.5)
    time.sleep(1.0)
    p.kill()
    im = Image.open(bmp).transpose(Image.FLIP_TOP_BOTTOM).crop((240, 200, 1040, 680))
    out.mkdir(parents=True, exist_ok=True)
    png = out / f'{cid}_{view}.png'
    im.save(png)
    return png


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--cars', required=True, type=Path)
    ap.add_argument('--game', type=Path)
    ap.add_argument('--out', type=Path)
    ap.add_argument('--views', default='front,side,rear')
    ap.add_argument('--sheet')
    ap.add_argument('--single', action='store_true')
    ap.add_argument('--scale', type=float, default=1.0)
    ap.add_argument('cmd')
    ap.add_argument('args', nargs='*')
    a = ap.parse_args()

    if a.cmd == 'list':
        for d in sorted(p.name for p in a.cars.iterdir()):
            if car_path(a.cars, d).exists():
                print(d, load(a.cars, d)['name'])
    elif a.cmd == 'get':
        j = load(a.cars, a.args[0])
        print(json.dumps({'cog': j['cog'], 'wheelRadius': j['wheelRadius'], 'wheels': j['wheels'], 'lamps': j['lamps']}, indent=1))
    elif a.cmd in ('set', 'nudge'):
        cid, name = a.args[:2]
        xyz = [int(x) for x in a.args[2:5]]
        j = load(a.cars, cid)
        if a.cmd == 'nudge':
            cur, _ = point(j, name)
            xyz = [c + d for c, d in zip(cur, xyz)]
        set_point(j, name, xyz, a.single)
        save(a.cars, j)
        print(cid, name, xyz)
    elif a.cmd == 'radius':
        j = load(a.cars, a.args[0])
        j['wheelRadius'] = int(a.args[1])
        save(a.cars, j)
    elif a.cmd == 'shots':
        from PIL import Image
        views = a.views.split(',')
        rows = []
        for cid in a.args:
            rows.append([Image.open(shot(a.game, a.cars, cid, v, a.out)) for v in views])
        if a.sheet:
            w, h = rows[0][0].size
            sheet = Image.new('RGB', (w * len(views), h * len(rows)))
            for r, row in enumerate(rows):
                for c, im in enumerate(row):
                    sheet.paste(im, (c * w, r * h))
            if a.scale != 1.0:
                sheet = sheet.resize((int(sheet.width * a.scale), int(sheet.height * a.scale)))
            sheet.save(a.out / a.sheet)
            print(a.out / a.sheet)
    else:
        ap.error('unknown command')


if __name__ == '__main__':
    main()
