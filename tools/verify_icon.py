#!/usr/bin/env python3
"""Verifies the structure of resources/qshot.ico without using Qt.

Why not just load it with QIcon and call it done: Qt reads its own way, and the thing that
actually has to accept this file is the Windows shell. Parsing the container by hand checks
the parts a wrong generator gets wrong and a lenient reader forgives -- the entry count, the
256-is-encoded-as-zero convention, the offsets, and whether each payload really is a PNG of
the size its directory entry claims.

Run after regenerating the icon:  python tools/verify_icon.py
"""

import struct
import sys
from pathlib import Path

EXPECTED_SIZES = [16, 20, 24, 32, 48, 64, 128, 256]
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def png_size(payload: bytes) -> tuple[int, int]:
    """Reads width and height straight out of the PNG IHDR chunk."""
    if not payload.startswith(PNG_MAGIC):
        raise ValueError("payload does not start with the PNG signature")
    if payload[12:16] != b"IHDR":
        raise ValueError("first PNG chunk is not IHDR")
    width, height = struct.unpack(">II", payload[16:24])
    return width, height


def main() -> int:
    ico_path = Path(__file__).resolve().parent.parent / "resources" / "qshot.ico"
    if not ico_path.exists():
        print(f"FAIL {ico_path} does not exist")
        return 1

    data = ico_path.read_bytes()
    failures = 0

    def check(ok: bool, what: str) -> None:
        nonlocal failures
        if not ok:
            failures += 1
        print(f"  {'ok  ' if ok else 'FAIL'} {what}")

    reserved, image_type, count = struct.unpack_from("<HHH", data, 0)
    print(f"[1] header")
    check(reserved == 0, f"reserved field is 0 (got {reserved})")
    check(image_type == 1, f"type is 1 = icon (got {image_type})")
    check(count == len(EXPECTED_SIZES),
          f"declares {len(EXPECTED_SIZES)} images (got {count})")

    print(f"\n[2] directory entries and payloads")
    found = []
    for i in range(count):
        off = 6 + 16 * i
        w, h, colors, res, planes, bpp, size, offset = struct.unpack_from("<BBBBHHII", data, off)

        # The format stores 256 as 0 in a single byte, so a 256px icon is indistinguishable
        # from a corrupt zero unless it is decoded this way. Getting this wrong is the
        # classic way to produce a file that looks fine and fails to load.
        real_w = w if w != 0 else 256
        real_h = h if h != 0 else 256

        payload = data[offset:offset + size]
        try:
            png_w, png_h = png_size(payload)
        except ValueError as exc:
            check(False, f"entry {i} ({real_w}px): {exc}")
            continue

        ok = (png_w == real_w and png_h == real_h
              and offset + size <= len(data) and bpp == 32)
        check(ok, f"entry {i}: directory says {real_w}x{real_h}, payload is "
                  f"{png_w}x{png_h}, {bpp}bpp, {size} bytes at {offset}")
        found.append(real_w)

    print(f"\n[3] sizes present")
    check(found == EXPECTED_SIZES, f"all sizes in order: {found}")

    print(f"\n[4] file size is sane")
    # The 256px entry as a raw DIB would be 256KB on its own; PNG entries are what keep this
    # small. A file much larger than this means something is being stored uncompressed.
    check(len(data) < 64 * 1024, f"{len(data)} bytes total (under 64KB)")

    print(f"\n{'all checks passed' if failures == 0 else f'{failures} failure(s)'}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
