#!/usr/bin/env bash
set -e

# Use the directory supplied on the command line, if any.
if [ "$#" -gt 0 ]; then
    INTDIR="$1"
else
    INTDIR="../INTLIST"
    if [ ! -d "$INTDIR" ]; then
        INTDIR="."
    fi
fi

if [ ! -d "$INTDIR" ]; then
    echo "INTLIST directory not found: $INTDIR"
    exit 1
fi

if [ ! -x "./RBILBLD.LX" ]; then
    echo "RBILBLD.LX not found or not executable!"
    exit 1
fi

echo "Converting the intlist files from $INTDIR to RBILVIEW format..."
./RBILBLD.LX RBIL.RBD "$INTDIR"

if [ ! -f "RBIL.RBD" ]; then
    echo "Database generation failed: RBIL.RBD was not created."
    exit 1
fi

echo "RBIL.RBD generated successfully."
