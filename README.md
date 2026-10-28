# RBILVIEW

A text-mode viewer for Ralf Brown's Interrupt List (RBIL).

This repository includes DOS and native Linux executables, source code, database-generation tools, the generated database, and the INTER61 archive set.

## Contents

- RBILVIEW: the text-mode interrupt-list viewer.
- RBILBLD: converts the interrupt-list archives into the database format.
- RBIL.RBD: the generated database.
- DOS executables and batch files.
- Native Linux executables and shell scripts.
- INTLIST: the INTER61 archive set.

## Linux

### Requirements

A C compiler, Bash, standard build utilities, and ncurses development files are required to build the Linux programs.

On Ubuntu or Debian:

    sudo apt install build-essential libncurses-dev

### Run

From the repository root:

    cd RBILVIEW
    ./RUNLINUX.SH

The script uses the included RBIL.RBD database when available and builds missing components as necessary.

### Build the Linux programs

Build the database-generation utility:

    ./MAKEBLD.SH

Build the viewer:

    ./MAKEVIEW.SH

Run these commands from the RBILVIEW directory.

### Regenerate the database

From the RBILVIEW directory:

    ./GENERATE.SH

By default, the script looks for the archives in the sibling INTLIST directory. An alternative archive directory can be supplied as an argument:

    ./GENERATE.SH /path/to/INTLIST

The generated database is written to RBIL.RBD in the current directory.

## DOS

The DOS version is built using Open Watcom V1.9.

Run RUNME.BAT from the RBILVIEW directory in a DOS environment configured with the required Open Watcom V1.9 tools. This batch file handles the DOS build workflow.

The distribution also includes the DOS executables and supporting batch files for building the viewer, building the database utility, and generating the database.

## Checksums

MD5SUMS contains MD5 checksums for regular files directly inside INTLIST and RBILVIEW.

From the repository root, verify the files with:

    md5sum -c MD5SUMS

The checksum file does not checksum itself, README.md, or .gitignore.

## Repository layout

    RBIL/
    ├── README.md
    ├── .gitignore
    ├── MD5SUMS
    ├── INTLIST/
    └── RBILVIEW/

## Attribution and licensing

Ralf Brown's Interrupt List is the underlying reference material. Consult the original material and accompanying notices for applicable redistribution and reuse terms.

Source code and bundled executables are included as distributed in this repository. Consult any accompanying notices for applicable licensing terms.
