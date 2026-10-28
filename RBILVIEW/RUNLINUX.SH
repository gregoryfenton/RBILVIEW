#!/usr/bin/env bash
set -e

# Generate the database if it does not already exist.
if [ -f "RBIL.RBD" ]; then
    echo "RBIL.RBD found"
else
    if [ ! -f "RBILBLD.LX" ]; then
        if [ -f "MAKEBLD.SH" ]; then
            chmod +x MAKEBLD.SH
            ./MAKEBLD.SH
        else
            echo "Critical build files are missing."
            echo "Missing: MAKEBLD.SH"
            exit 1
        fi
    fi

    if [ -f "GENERATE.SH" ]; then
        chmod +x GENERATE.SH
        ./GENERATE.SH
    else
        echo "Critical build files are missing."
        echo "Missing: GENERATE.SH"
        exit 1
    fi
fi

# Build the viewer if necessary.
if [ ! -f "RBILVIEW.LX" ]; then
    if [ -f "MAKEVIEW.SH" ]; then
        chmod +x MAKEVIEW.SH
        ./MAKEVIEW.SH
    else
        echo "The viewer is missing: MAKEVIEW.SH not found."
        exit 1
    fi
fi

if [ ! -x "./RBILVIEW.LX" ]; then
    echo "RBILVIEW.LX not found or not executable."
    exit 1
fi

if [ ! -f "RBIL.RBD" ]; then
    echo "Database missing: RBIL.RBD"
    exit 1
fi

echo "Launching viewer..."
./RBILVIEW.LX RBIL.RBD
