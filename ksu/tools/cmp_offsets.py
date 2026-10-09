#!/usr/bin/env python3
"""Compare pahole struct dumps (name+offset+size), ignoring type-name spelling."""
import re, sys

def parse(path):
    members = {}
    # line format: <whitespace><type...> <name>[dims] ; /* <offset> <size> */
    # or nested anonymous: "struct { ... } name;" lines also carry /* off size */
    pat = re.compile(r'/\*\s*(-?\d+)\s+(\d+)\s*\*/')
    for line in open(path, errors='replace'):
        m = pat.search(line)
        if not m:
            # also catch "/* size: X" trailer
            continue
        off, sz = int(m.group(1)), int(m.group(2))
        # member name: last identifier before ';' (or before '/*')
        pre = line[:m.start()]
        nm = re.findall(r'([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\])?\s*;', pre)
        if not nm:
            nm = re.findall(r'([A-Za-z_][A-Za-z0-9_]*)\s*$', pre.rstrip())
        name = nm[-1] if nm else pre.strip()[:40]
        depth = line.count('\t')
        members.setdefault((name, off, sz), 0)
        members[(name, off, sz)] += 1
    return members

a, b = sys.argv[1], sys.argv[2]
ma, mb = parse(a), parse(b)
only_a = sorted(set(ma) - set(mb))
only_b = sorted(set(mb) - set(ma))
if not only_a and not only_b:
    print(f"IDENTICAL: {len(ma)} member entries")
else:
    print(f"DIFF: only-in-module({a}):")
    for t in only_a: print("  ", t)
    print(f"DIFF: only-in-device({b}):")
    for t in only_b: print("  ", t)
