#!/bin/bash
# Build the Moonrush game module. Output: deploy/subsdk8 + deploy/main.npdm
# Run via build.ps1 (sets MOONRUSH_PYTHON), or from devkitPro's msys2 bash.
# make can't handle the spaces in "My SMO Mods", so the build runs in a space-free scratch copy.
set -e
SRC="$(cd "$(dirname "$0")" && pwd)"
if [ -n "$MOONRUSH_PYTHON" ]; then
    export PATH="$(dirname "$(cygpath -u "$MOONRUSH_PYTHON")"):$PATH"
fi
export DEVKITPRO=/opt/devkitpro
python "$SRC/../tools/gen_offsets.py" > /dev/null

WORK=/tmp/moonrush
mkdir -p "$WORK"
# Sync sources (keep $WORK/build for incremental object files).
for item in Makefile config.mk config.json misc source; do
    rm -rf "$WORK/$item"
    cp -r "$SRC/$item" "$WORK/$item"
done
cd "$WORK"
make -j8 "$@"
rm -rf "$SRC/deploy"
cp -r "$WORK/deploy" "$SRC/deploy"
echo "Built: $SRC/deploy"
ls -l "$SRC/deploy"
