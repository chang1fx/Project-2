#!/bin/sh
# usage: ./build.sh                      -> stock build
#        ./build.sh -DHW1_SEMAPHORES     -> CHANGED + extra defines
rm -f *.o nachos
if [ $# -eq 0 ]; then make; else make DEFINES="-DTHREADS -DCHANGED $*"; fi
