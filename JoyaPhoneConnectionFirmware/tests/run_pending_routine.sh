#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out_file=$(mktemp)
trap 'rm -f "$out_file"' EXIT

cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/include" \
    "$repo_dir/tests/test_pending_routine.c" -o "$out_file"
"$out_file"
