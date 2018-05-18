#!/bin/bash

gcc -c -o tracer.o tracer.c && \
tools/build.py --clean --debug --builddir $PWD/../jerry-build \
    --jerry-libc=OFF --jerry-libm=OFF --jerry-ext=OFF \
    --compile-flag=-finstrument-functions --linker-flag=$PWD/tracer.o --link-lib=-lm
