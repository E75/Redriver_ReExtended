"""Extract POLICE.IMG (via POLICE.TOC) of the WSPC game data.

Also unpacks every MERGED/*.MRG next to the files (folder MERGED/<name>_MRG/). Format: docs/wspc.md.
The helpers toc_entries, read_entry and mrg_files are used by the other wspc tools to read single files.
"""
import argparse
import struct
from pathlib import Path


def toc_entries(src: Path) -> dict[str, tuple[int, int]]:
    """POLICE.TOC -> {name with backslashes: (offset in POLICE.IMG, size)}."""
    toc = (src / 'POLICE.TOC').read_bytes()
    entries: dict[str, tuple[int, int]] = {}
    pos = 0
    while pos < len(toc):
        end = toc.index(b'\0', pos)
        name = toc[pos:end].decode('latin1')
        offset, size = struct.unpack_from('<II', toc, end + 1)
        pos = end + 9
        if not name:
            break
        entries[name] = (offset, size)
    return entries


def read_entry(src: Path, name: str, start: int = 0, length: int | None = None) -> bytes:
    """Bytes of one POLICE.IMG file, or a part of it (a .STR is 53 MB)."""
    offset, size = toc_entries(src)[name]
    with (src / 'POLICE.IMG').open('rb') as img:
        img.seek(offset + start)
        return img.read(size - start if length is None else length)


def mrg_files(data: bytes) -> dict[str, bytes]:
    """The files of a .MRG archive, in memory."""
    files = {}
    for i in range(struct.unpack_from('<I', data, 0)[0]):
        name_off, offset, size = struct.unpack_from('<III', data, 4 + i * 12)
        files[data[name_off:data.index(b'\0', name_off)].decode('latin1')] = data[offset:offset + size]
    return files


def extract_img(src: Path, out: Path) -> int:
    entries = toc_entries(src)
    with (src / 'POLICE.IMG').open('rb') as img:
        for name, (offset, size) in entries.items():
            dest = out / name.replace('\\', '/')
            dest.parent.mkdir(parents=True, exist_ok=True)
            img.seek(offset)
            dest.write_bytes(img.read(size))
    return len(entries)


def extract_mrg(path: Path, out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    for name, body in mrg_files(path.read_bytes()).items():
        (out / name).write_bytes(body)


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
