#!/usr/bin/env python3
"""Dump every OSCpar instance's configuration from a .bwproject.

OSCpar stores its whole config as plaintext XML inside a JUCE state chunk, and
Bitwig keeps those chunks as plugin-states/<uuid>.vstpreset in a ZIP appended to
the project file at the first PK\\x03\\x04.

Read-only. Point it at the PRE-patch file (or a backup) -- after a class-UID
patch the chunks are still OSCpar XML, but a migrated-and-saved project will
have replaced them with the new plugin's own format.

Usage:
  extract_oscpar_config.py <project.bwproject> [--json]

Conversion notes, all observed in real data:
  - Prefix has NO leading slash ("lx/mixer/..."); OSC addresses need one.
  - Address is "0.0.0.0" on every instance -- a bind address, not a destination.
    Treat it as 127.0.0.1.
  - <Macro Name=...> casing is inconsistent ("macro1" vs "Macro9"); match on
    position in the list, never on the name.
  - <PARAM id="MacroN" value=".."/> carries each macro's last value, and those
    ids ARE capitalised. Do not assume casing agrees between the two sections.
  - OSCpar exposes 10 macros. A replacement with fewer must truncate.
"""
import json
import re
import sys
import zipfile


def presets(path):
    raw = open(path, 'rb').read()
    body = raw[:raw.find(b'PK\x03\x04')]
    z = zipfile.ZipFile(path)
    rows = []

    for name in z.namelist():
        data = z.read(name)
        head = re.search(rb'<Preset\b[^>]*>', data)
        if not head:
            continue
        head = head.group().decode('latin1')

        def attr(key):
            m = re.search(key + r'="([^"]*)"', head)
            return m.group(1) if m else None

        uuid = name.split('/')[-1]
        values = {m.group(1).decode().lower(): float(m.group(2))
                  for m in re.finditer(rb'id="(Macro\d+)" value="([^"]*)"', data)}
        macros = [{'index': i,
                   'name': nm.decode(),
                   'scale': [float(lo), float(hi)],
                   'value': values.get(f'macro{i}')}
                  for i, (nm, lo, hi) in enumerate(
                      re.findall(rb'<Macro Name="([^"]*)" ScaleMin="([^"]*)" '
                                 rb'ScaleMax="([^"]*)"', data), 1)]

        prefix = attr('Prefix') or ''
        address = attr('Address') or ''
        rows.append({
            'preset': uuid,
            'body_offset': body.find(uuid.encode()),
            'prefix_raw': prefix,
            'prefix': ('/' + prefix) if prefix and not prefix.startswith('/') else prefix,
            'address_raw': address,
            'host': '127.0.0.1' if address in ('', '0.0.0.0', None) else address,
            'port': int(attr('Port') or 3030),
            'macros': macros,
        })

    rows.sort(key=lambda r: r['body_offset'])
    return rows


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    rows = presets(sys.argv[1])

    if '--json' in sys.argv:
        print(json.dumps(rows, indent=2))
        return 0

    print(f"{len(rows)} instances")
    hosts = {(r['host'], r['port']) for r in rows}
    print(f"distinct host/port: {hosts}\n")

    seen = {}
    for r in rows:
        print(f"{r['preset'][:8]}  {r['prefix']}  ({r['host']}:{r['port']})")
        for m in r['macros']:
            v = '' if m['value'] is None else f"  value={m['value']}"
            print(f"      macro{m['index']:<3} scale={m['scale']}{v}")
        seen.setdefault(r['prefix'], []).append(r['preset'][:8])

    dupes = {k: v for k, v in seen.items() if len(v) > 1}
    if dupes:
        print("\nWARNING - prefixes claimed by more than one instance")
        print("(two senders on one OSC address is last-packet-wins):")
        for k, v in dupes.items():
            print(f"  {k}  <- {', '.join(v)}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
