#!/usr/bin/env python3
"""Swap a VST3 plugin's class UID inside a Bitwig .bwproject, length-neutrally.

Only the 32-character uppercase-hex UID strings are replaced -- in the TLV body
and inside each .vstpreset in the appended ZIP. Same length in, same length out,
so no TLV block length and neither header offset needs recomputing.

Deliberately does NOT touch the cached plugin name, vendor, category, version or
path. Those are display metadata; leaving them stale is preferable to resizing
the body. If Bitwig resolves a device by UID alone, this is sufficient.

Usage: patch_uid.py <in.bwproject> <out.bwproject> <OLD_UID_HEX> <NEW_UID_HEX>
"""
import shutil
import struct
import sys
import zipfile


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 1
    src, dst, old, new = sys.argv[1:]
    old_b, new_b = old.encode(), new.encode()
    if len(old_b) != 32 or len(new_b) != 32:
        print("UIDs must be exactly 32 hex characters")
        return 1

    raw = open(src, 'rb').read()
    zip_off = raw.find(b'PK\x03\x04')
    body, tail = raw[:zip_off], raw[zip_off:]

    declared = int(raw[32:40], 16)
    assert declared == zip_off, f"header says zip@{declared}, found {zip_off}"

    n_body = body.count(old_b)
    body = body.replace(old_b, new_b)
    assert len(body) == zip_off, "body length changed -- refusing to write"

    # Rebuild the ZIP with the same entry names, patching preset payloads.
    zin = zipfile.ZipFile(src)
    tmp = dst + '.ziptmp'
    n_zip = 0
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as zout:
        for info in zin.infolist():
            data = zin.read(info.filename)
            n_zip += data.count(old_b)
            zout.writestr(info, data.replace(old_b, new_b))

    with open(dst, 'wb') as f:
        f.write(body)
        f.write(open(tmp, 'rb').read())

    import os
    os.remove(tmp)

    out = open(dst, 'rb').read()
    assert out.find(b'PK\x03\x04') == zip_off, "zip offset moved"
    assert int(out[32:40], 16) == zip_off, "header offset no longer matches"
    assert out.count(old_b) == 0, "old UID still present"
    zipfile.ZipFile(dst).testzip()

    print(f"body: {n_body} UID strings replaced (length unchanged)")
    print(f"zip : {n_zip} UID strings replaced across {len(zin.namelist())} entries")
    print(f"zip offset {zip_off} unchanged; header intact; CRCs valid")
    print(f"wrote {dst}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
