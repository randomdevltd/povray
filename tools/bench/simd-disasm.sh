#!/usr/bin/env bash
# simd-disasm.sh [builddir] [march...]: compiles simd-disasm.cpp per -march and diffs the two kernels' opcodes, as sorted
# counts with a >= b and b <= a (one instruction, operands swapped) taken as the same.
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../.." && pwd); BUILD=${1:-$TOP}; shift || true
MARCHES=("${@:-x86-64-v3 native}")
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
status=0
for m in ${MARCHES[@]}; do
  ${CXX:-g++} -std=gnu++17 -O3 -ffast-math -fno-finite-math-only -march="$m" -DHAVE_CONFIG_H -I"$BUILD/unix" \
    -I"$TOP/unix/povconfig" -I"$TOP/libraries/xsimd/include" -I"$TOP/source" -I"$TOP/platform/unix" -I"$TOP/platform/x86" \
    -c "$TOP/tools/bench/simd-disasm.cpp" -o "$TMP/k.o"
  for k in slabs_simd slabs_raw; do
    objdump -d --no-show-raw-insn --no-addresses "$TMP/k.o" --disassemble="$k" | awk -F'\t' 'NF > 1 {print $2}' |
      awk '{print $1}' | sed -E 's/cmpge_oq/cmple_oq/; s/cmpgt_oq/cmplt_oq/; s/cmpnlt/cmple/' | sort > "$TMP/$k"
  done
  a=$(wc -l < "$TMP/slabs_simd"); b=$(wc -l < "$TMP/slabs_raw")
  [ "$b" -eq 0 ] && { echo "-march=$m: no AVX2, so no raw kernel to compare; skipped"; continue; }
  if diff -u "$TMP/slabs_raw" "$TMP/slabs_simd" > "$TMP/diff"; then
    echo "-march=$m: the same $a instructions"
  else
    echo "-march=$m: simd:: $a instructions, raw $b"; cat "$TMP/diff"; status=1
  fi
done
exit $status
