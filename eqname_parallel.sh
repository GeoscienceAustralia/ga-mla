#!/usr/bin/env bash
set -euo pipefail
shopt -s nullglob

# Usage:
#   cat input.csv | ./eqnamer_parallel.sh N [path/to/eqnamer_batch] > output.csv
N="${1:?need N}"
CMD="${2:-./eqnamer_batch}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

in="$tmp/in.csv"
cat >"$in"

header_file="$tmp/header.csv"
head -n 1 "$in" >"$header_file"

total_lines="$(wc -l <"$in")"
data_lines=$((total_lines - 1))
lines_per_chunk=$(((data_lines + N - 1) / N))
((lines_per_chunk < 1)) && lines_per_chunk=1

# Split data rows into chunk_0000, chunk_0001, ...
tail -n +2 "$in" | split -d -a 4 -l "$lines_per_chunk" - "$tmp/chunk_"

# If input had only a header (no data rows), make one empty chunk so we still run once.
chunks=("$tmp"/chunk_*)
if ((${#chunks[@]} == 0)); then
    : >"$tmp/chunk_0000"
    chunks=("$tmp/chunk_0000")
fi

# Process each chunk in parallel, but feed eqnamer_batch from a *file* (not a pipe).
i=0
for chunk in "${chunks[@]}"; do
    inchunk="$(printf "%s/in_%04d.csv" "$tmp" "$i")"
    outchunk="$(printf "%s/out_%04d.csv" "$tmp" "$i")"

    cat "$header_file" "$chunk" >"$inchunk"

    (
        "$CMD" <"$inchunk" >"$outchunk"
    ) &
    i=$((i + 1))
done
wait

# Recombine to stdout (header once)
first="$tmp/out_0000.csv"
head -n 1 "$first"
tail -n +2 "$first"

for outchunk in "$tmp"/out_*.csv; do
    [[ "$outchunk" == "$first" ]] && continue
    tail -n +2 "$outchunk"
done
