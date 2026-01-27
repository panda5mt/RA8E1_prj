#!/bin/bash
set -eu

# Update HLAC LDA model C sources in src/.
#
# What it does:
# - Searches the current folder and all subfolders for a directory named:
#     - lda_model/
#   (and also lda_nodel/ as a common typo)
# - If found, copies *.c and *.h files from that directory into ./src/
#   overwriting existing files.
#
# Usage:
#   ./update_c_hlac_model.sh

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEST_DIR="$ROOT_DIR/src"

if [[ ! -d "$DEST_DIR" ]]; then
	echo "ERROR: destination folder not found: $DEST_DIR" >&2
	exit 1
fi

cd "$ROOT_DIR"

# Prefer a top-level directory if present (deterministic behavior).
model_dir=""
if [ -d "$ROOT_DIR/lda_model" ]; then
	model_dir="lda_model"
elif [ -d "$ROOT_DIR/lda_nodel" ]; then
	model_dir="lda_nodel"
else
	# Otherwise, pick the shortest path (closest to repo root).
	model_dir="$(find . -type d \( -name 'lda_model' -o -name 'lda_nodel' \) -print 2>/dev/null | \
		sed 's|^\./||' | \
		awk 'BEGIN{best=""} { if (best=="" || length($0) < length(best)) best=$0 } END{ print best }')"
fi

if [ -z "$model_dir" ]; then
	echo "No lda_model/ (or lda_nodel/) directory found under: $ROOT_DIR"
	exit 0
fi

echo "Using model directory: $model_dir"

copied=0
for f in "$ROOT_DIR/$model_dir"/*.c "$ROOT_DIR/$model_dir"/*.h; do
	if [ ! -f "$f" ]; then
		continue
	fi
	bn="$(basename "$f")"
	cp -f "$f" "$DEST_DIR/$bn"
	echo "  - updated: src/$bn"
	copied=$((copied + 1))
done

if [ "$copied" -eq 0 ]; then
	echo "No *.c/*.h files found in: $ROOT_DIR/$model_dir"
	exit 0
fi

echo "Copied $copied file(s) into: $DEST_DIR"

echo "Done."
