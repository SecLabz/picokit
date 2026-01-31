#!/bin/bash
set -e

mkdir -p build
cmake -B build
make -C build -j
picotool load -v -x build/picokit.uf2 -f

echo "Build and deploy complete."
