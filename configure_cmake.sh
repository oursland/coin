#!/usr/bin/env bash
set -euo pipefail

rm -rf bld
mkdir -p bld && cd bld
cmake -G "Ninja" -DCMAKE_INSTALL_PREFIX=install -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCOIN_BUILD_SHARED_LIBS=0 \
    -DCOIN_BUILD_EXAMPLES=1 -DHAVE_VRML97=1 ..
