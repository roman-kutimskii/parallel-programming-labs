#!/usr/bin/env bash
# Серия замеров для графиков T/S/E (Linux/WSL): ядра 1..4 через taskset × потоки 1..16 × 5 повторов.
# Использование: ./bench.sh <путь к lab02-blur> <input.bmp> <h|v|s> <radius> > results.csv
set -eu
if [ $# -ne 4 ]; then
  echo "Usage: $0 <lab02-blur> <input.bmp> <h|v|s> <radius>" >&2
  exit 1
fi
BIN=$1; IN=$2; SPLIT=$3; RADIUS=$4
OUT=$(mktemp --suffix=.bmp)
trap 'rm -f "$OUT"' EXIT
echo "cores,threads,split,radius,hw,ms"
for c in 1 2 3 4; do
  for n in $(seq 1 16); do
    for _ in 1 2 3 4 5; do
      printf '%s,' "$c"
      taskset -c 0-$((c-1)) "$BIN" "$IN" "$OUT" "$n" "$SPLIT" "$RADIUS"
    done
  done
done
