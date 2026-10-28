#!/usr/bin/env bash
set -e

echo "Building RBILVIEW.LX (Linux host target with ncurses)..."
cc -x c -std=c99 -O2 -Wall -Wextra \
    RBILVIEW.C -lncurses -o RBILVIEW.LX

echo "RBILVIEW.LX built successfully."
