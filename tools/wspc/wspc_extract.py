"""Extract POLICE.IMG (via POLICE.TOC) of the PS1 game World's Scariest Police Chases / Deadly Pursuit.

Also unpacks every MERGED/*.MRG next to the files (folder MERGED/<name>_MRG/). Format: docs/wspc.md.
"""
import argparse
import struct
from pathlib import Path


def extract_img(src: Path, out: Path) -> int:
    toc = (src / 'POLICE.TOC').read_bytes()
    count = pos = 0
    with (src / 'POLICE.IMG').open('rb') as img:
        while pos < len(toc):
            end = toc.index(b'\0', pos)
            name = toc[pos:end].decode('latin1')
            offset, size = struct.unpack_from('<II', toc, end + 1)
            pos = end + 9
            if not name:
                break
            dest = out / name.replace('\\', '/')
            dest.parent.mkdir(parents=True, exist_ok=True)
            img.seek(offset)
            dest.write_bytes(img.read(size))
            count += 1
    return count


def extract_mrg(path: Path, out: Path) -> None:
    data = path.read_bytes()
    out.mkdir(parents=True, exist_ok=True)
    for i in range(struct.unpack_from('<I', data, 0)[0]):
        name_off, offset, size = struct.unpack_from('<III', data, 4 + i * 12)
        name = data[name_off:data.index(b'\0', name_off)].decode('latin1')
        (out / name).write_bytes(data[offset:offset + size])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path, help='folder with POLICE.TOC and POLICE.IMG')
    parser.add_argument('out', type=Path, help='output folder')
    args = parser.parse_args()
    print(extract_img(args.game, args.out), 'files')
    for mrg in sorted((args.out / 'MERGED').glob('*.MRG')):
        extract_mrg(mrg, mrg.with_name(mrg.stem + '_MRG'))


if __name__ == '__main__':
    main()
