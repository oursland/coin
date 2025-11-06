#!/usr/bin/env bash
set -ex
cd "$(dirname "$0")"

# check if debian and
# sudo apt install freeglut3-dev libglfw3-dev libglu-dev ninja-build libboost-dev

export COIN_PATH="$PWD/../.."
export COIN_BUILD_PATH="$COIN_PATH/${1:-bld}"
ninja -C $COIN_BUILD_PATH

mkdir -p bin/
g++ -O0 -ggdb glfw.cpp \
    -I$COIN_PATH/include -I$COIN_BUILD_PATH/include -L$COIN_BUILD_PATH/lib \
    -lCoin -lGL -lGLU -lglut -lglfw -lX11 -lEGL \
    -o bin/glfw

###

export LD_LIBRARY_PATH=$COIN_BUILD_PATH/lib:"$LD_LIBRARY_PATH"
./bin/glfw
