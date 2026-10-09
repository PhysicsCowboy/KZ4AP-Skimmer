#!/bin/sh
# Builds Bell's program, bell1977.f, with gfortran (Bell stage-1 plan, Task 3).
#   reference/bell1977/build.sh [OUTDIR]     (default OUTDIR: build/bell1977/bin, under the current folder)
# The compiler is $FC, else gfortran on the PATH. Two binaries:
#   bell1977      the program;
#   bell1977-fpe  the same, with -ffpe-trap=invalid,zero: a division by zero or a NaN-producing operation
#                 stops it with SIGFPE (plan section 9 item 6, SAVEP's zero sum); otherwise identical results.
# Flags (reference sheet (h)): -std=legacy -fdec for DEC Fortran (Hollerith and character data in integer
# arrays, the $ edit descriptor); -fno-automatic -finit-local-zero: static locals that start at zero, as the
# listing assumes (PDP-10); -fdefault-real-8 -fdefault-double-8: every REAL in double precision (plan 4.3).
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-build/bell1977/bin}
fc=${FC:-gfortran}
flags="-std=legacy -fdec -fno-automatic -finit-local-zero -fdefault-real-8 -fdefault-double-8 -O2"
mkdir -p "$out"
$fc $flags -o "$out/bell1977" "$here/bell1977.f"
$fc $flags -ffpe-trap=invalid,zero -o "$out/bell1977-fpe" "$here/bell1977.f"
$fc --version | head -n 1
