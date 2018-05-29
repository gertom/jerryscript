#!/bin/bash

JERRYSRCDIR=$PWD
JERRYBLDDIR=$PWD/../jerry-build
TRACEFILE=tracer.trc

EtM=$JERRYBLDDIR/../elf-to-map.py
TtG=$JERRYBLDDIR/../trace-to-graph.py
CGF=$JERRYBLDDIR/../convert-graph-formats.py

if [ ! -x $EtM -o  ! -x $TtG -o  ! -x $CGF -o ! -e tracer.c ]
then
    exit 1
fi

if [ -n "$1" ]
then
    JERRYVERSION="jerry-$1"
else
    JERRYVERSION=$(date +jerry-%Y%m%d-%H%M%S)
fi

#
# Build jerry for tracing
#

gcc -c -o $JERRYSRCDIR/tracer.o tracer.c && \
tools/build.py --clean --debug --builddir $JERRYBLDDIR \
    --jerry-libc=OFF --jerry-libm=OFF --jerry-ext=OFF \
    --compile-flag=-finstrument-functions --linker-flag=$JERRYSRCDIR/tracer.o --link-lib=-lm

#
# Create dynamic graph
#

pushd $JERRYBLDDIR
rm -f $TRACEFILE
for X in $JERRYSRCDIR/tests/jerry/*.js ; do bin/jerry $X ; done
mv $TRACEFILE $JERRYVERSION.trc
$TtG -m -g $JERRYVERSION.trc
$EtM bin/jerry $JERRYVERSION.dynamic.map
$CGF $JERRYVERSION.trc.all.graph.json $JERRYVERSION.dynamic.graphml -m $JERRYVERSION.dynamic.map
gzip $JERRYVERSION.trc $JERRYVERSION.dynamic.map $JERRYVERSION.trc.all.graph.json
popd
