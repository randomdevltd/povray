#!/usr/bin/env python3
"""table.py COUNTS.tsv [BASECOL NEWCOL]: per consumer kind, scenes faster / within 1% / slower, median
and worst change of count.sh's new build against its base build, and the aggregate change."""
import statistics
import sys

if len(sys.argv) < 2:
    sys.exit(__doc__)
base_col = int(sys.argv[2]) if len(sys.argv) > 2 else 2
new_col = int(sys.argv[3]) if len(sys.argv) > 3 else 3
band = 1.0

kinds = {}
total_base = total_new = 0
for line in open(sys.argv[1]):
    f = line.rstrip("\n").split("\t")
    try:
        b, n = int(f[base_col]), int(f[new_col])
    except (IndexError, ValueError):
        continue
    if b <= 0:
        continue
    change = (n - b) / b * 100
    for k in (f[1], "ALL"):
        kinds.setdefault(k, []).append((change, f[0]))
    total_base += b
    total_new += n

if not total_base:
    sys.exit("no usable rows")
print(f"{'kind':<14}{'n':>5}{'faster':>8}{'same':>6}{'slower':>8}{'median':>9}{'worst':>9}  worst seed")
for k in sorted(kinds, key=lambda k: (k == "ALL", k)):
    rows = kinds[k]
    changes = [c for c, _ in rows]
    faster = sum(c < -band for c in changes)
    slower = sum(c > band for c in changes)
    worst = max(rows)
    print(f"{k:<14}{len(rows):>5}{faster:>8}{len(rows) - faster - slower:>6}{slower:>8}"
          f"{statistics.median(changes):>+8.1f}%{worst[0]:>+8.1f}%  {worst[1]}")
print(f"aggregate {(total_new - total_base) / total_base * 100:+.1f}%")
