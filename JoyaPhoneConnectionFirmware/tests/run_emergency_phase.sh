#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out_file=$(mktemp)
trap 'rm -f "$out_file"' EXIT

cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/include" \
    "$repo_dir/src/emergency_phase.c" \
    "$repo_dir/tests/test_emergency_phase.c" -o "$out_file"
"$out_file"
