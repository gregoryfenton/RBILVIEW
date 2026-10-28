#!/usr/bin/env bash
set -e

echo "Building RBILBLD.LX (Linux host target)..."
cc -x c -std=c99 -O2 -Wall -Wextra \
    RBILBLD.C INT2QH2I.C RBILPK-I.C ZIPRDLIB.C \
    -o RBILBLD.LX

echo "RBILBLD.LX built successfully."
