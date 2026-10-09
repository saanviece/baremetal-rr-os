#!/bin/sh
# Builds tools/bin/simtest.exe against the simavr library (see docs/M0_toolchain.md).
# Run inside an MSYS2 UCRT64 shell:  C:\msys64\ucrt64.exe  then  sh tools/build_simtest.sh
SIMAVR=${SIMAVR:-/c/simavr_src/simavr/simavr}
mkdir -p tools/bin
gcc -O2 -Wall -Wextra -std=gnu11 \
    -I"$SIMAVR/sim" -I"$SIMAVR/cores" \
    tools/simtest.c -o tools/bin/simtest.exe \
    "$SIMAVR"/obj-*/libsimavr.a -lelf -lws2_32
