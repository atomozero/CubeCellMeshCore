#!/bin/bash
# Host-side tests for lib/ed25519 (run on a PC with gcc, no hardware needed)
#  1. RFC 8032 test vectors (sign + verify + tamper rejection)
#  2. Differential test: compact sc.c vs the original unrolled orlp sc.c
#     (taken from git history, commit 7baffdc) on random inputs
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LIB="$ROOT/lib/ed25519/src"
T="$ROOT/tools/ed25519_host_test"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

gcc -O2 -I"$LIB" -o "$OUT/rfc" "$T/rfc8032_test.c" "$LIB"/*.c
"$OUT/rfc"

git -C "$ROOT" show 7baffdc:lib/ed25519/src/sc.c \
  | sed 's/sc_reduce/old_sc_reduce/; s/sc_muladd/old_sc_muladd/; s/#include "sc.h"//' > "$OUT/sc_old.c"
gcc -O2 -I"$LIB" -o "$OUT/scdiff" "$T/sc_diff_test.c" "$LIB/sc.c" "$OUT/sc_old.c"
"$OUT/scdiff"
