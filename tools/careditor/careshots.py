#!/usr/bin/env python3
"""Exact lamp check: renders the rear and the front of cars from the car editor in an orthographic view (2 pixels per unit)
with a ruler in engine units, markers on top, so a lamp marker can be compared with its housing on the body.

  careshots.py --cars <dir> --out <dir> <id>...

Needs Playwright and Microsoft Edge (channel msedge) and the built standalone editor (npm run build in tools/careditor).
"""
import argparse
import base64
import io
import json
from pathlib import Path

from PIL import Image, ImageDraw
from playwright.sync_api import sync_playwright

HERE = Path(__file__).parent
CENTRE_Y = 110          # scene y (up) of the middle of the picture
HALF_W = 200            # units shown left and right of the middle
Y_TOP, Y_BOTTOM = 260, -10   # scene y range of the crop
SCALE = 2               # device pixels per unit


def ruler(im: Image.Image, view: str) -> Image.Image:
    """lines every 20 units with the engine x (sideways) and y (down is positive) as labels"""
    d = ImageDraw.Draw(im, 'RGBA')
    w, h = im.size
    flip = -1 if view == 'rear' else 1
    for ux in range(-HALF_W, HALF_W + 1, 20):
        x = (ux + HALF_W) * SCALE
        d.line([(x, 0), (x, h)], fill=(255, 255, 255, 40 if ux else 120), width=1)
        if ux % 40 == 0:
            d.text((x + 2, 2), str(ux * flip), fill=(255, 255, 0, 255))
    for sy in range(Y_BOTTOM + 10, Y_TOP, 20):
        y = (Y_TOP - sy) * SCALE
        d.line([(0, y), (w, y)], fill=(255, 255, 255, 40), width=1)
        if sy % 40 == 0:
            d.text((2, y + 2), str(-sy), fill=(0, 255, 255, 255))
    return im


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--cars', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('ids', nargs='+')
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    html = (HERE / 'dist' / 'index.html').as_uri()
    with sync_playwright() as p:
        browser = p.chromium.launch(channel='msedge')
        page = browser.new_page(viewport={'width': 1300, 'height': 1000}, device_scale_factor=SCALE)
        page.goto(html)
        page.wait_for_function('window.careditor !== undefined')
        for cid in a.ids:
            j = json.loads((a.cars / cid / 'car.json').read_text(encoding='utf-8'))
            glb = base64.b64encode((a.cars / cid / 'car.glb').read_bytes()).decode()
            page.evaluate("""([id, j, b64]) => { const bin = atob(b64); const u = new Uint8Array(bin.length);
                for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
                careditor.addCar(id, j, u.buffer, null); careditor.loadCar(id); }""", [cid, j, glb])
            page.wait_for_timeout(1500)
            parts = []
            for view in ('rear', 'front'):
                page.evaluate('([v, y]) => careditor.orthoShot(v, y)', [view, CENTRE_Y])
                page.wait_for_timeout(300)
                png = page.locator('#view canvas').screenshot()
                im = Image.open(io.BytesIO(png)).convert('RGB')
                x0 = (500 - HALF_W) * SCALE
                y0 = (500 - (Y_TOP - CENTRE_Y)) * SCALE
                crop = im.crop((x0, y0, x0 + 2 * HALF_W * SCALE, y0 + (Y_TOP - Y_BOTTOM) * SCALE))
                parts.append(ruler(crop, view))
            sheet = Image.new('RGB', (parts[0].width, parts[0].height * 2 + 6), (40, 40, 40))
            sheet.paste(parts[0], (0, 0))
            sheet.paste(parts[1], (0, parts[0].height + 6))
            sheet.save(a.out / f'{cid}.png')
            print(a.out / f'{cid}.png')
        browser.close()


if __name__ == '__main__':
    main()
