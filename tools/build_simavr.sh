#!/bin/sh
# How simavr was built on this Windows PC (no ready-made Windows package exists).
# Run inside an MSYS2 UCRT64 shell. Third-party source goes OUTSIDE this repo.
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libelf make git
mkdir -p /c/simavr_src && cd /c/simavr_src
git clone --depth 1 https://github.com/buserror/simavr.git
cd simavr && make -C simavr RELEASE=1      # produces libsimavr.a and run_avr
