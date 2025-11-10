export COIN_PATH="$PWD/../.."
export COIN_BUILD_PATH="$COIN_PATH/{$1-bld_web}"

emcc glfw.cpp -g -o bin/glfw.html \
    -DCOIN_USE_GL_RENDERER=1 -DCOIN_USE_EGL=1 \
    -I$COIN_PATH/include -I$COIN_BUILD_PATH/include -L$COIN_BUILD_PATH/lib \
    -s LEGACY_GL_EMULATION=1 -s USE_WEBGL2=1 -s USE_GLFW=3 -s WASM=1  \
    -lCoin
