"""Puts the five pictures of sweep.ps1 in one: python montage.py <prefix> (files <prefix>_0..4.png in the current folder)."""
import sys
from pathlib import Path

from PIL import Image

# layout: top row = NW, NE, centre; bottom row = SW, SE
ORDER = [1, 2, 0, 3, 4]


def main(prefix: str) -> None:
    folder = Path.cwd()
    ims = [Image.open(folder / f'{prefix}_{i}.png') for i in range(5)]
    w, h = ims[0].size
    montage = Image.new('RGB', (w * 3, h * 2))
    for k, i in enumerate(ORDER):
        montage.paste(ims[i], ((k % 3) * w, (k // 3) * h))

    montage.save(folder / f'{prefix}_montage.png')


if __name__ == '__main__':
    main(sys.argv[1])
