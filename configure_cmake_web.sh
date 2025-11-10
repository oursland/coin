#!/usr/bin/env bash
set -euo pipefail

# Helper to configure an Emscripten / WebGL Coin build in `bld-web`.
if ! command -v emcmake >/dev/null 2>&1; then
  printf 'emcmake is required for WebAssembly builds\n' >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOST_SYSROOT="${BOOST_SYSROOT:-${SCRIPT_DIR}/thirdparty/boost/sysroot}"

rm -rf bld-web
mkdir -p bld-web && cd bld-web

emcmake cmake -G "Ninja" \
    -DCMAKE_INSTALL_PREFIX=install \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1 \
    -DCOIN_BUILD_SHARED_LIBS=0 \
    -DCOIN_BUILD_TESTS=OFF \
    -DRENDER_TESTS_ENABLED=OFF \
    -DRENDER_TESTS_ENABLE_WEB=OFF \
    -DHAVE_SOUND=OFF \
    -DCOIN_BUILD_EXAMPLES=OFF \
    -DHAVE_VRML97=ON \
    -DCOIN_BUILD_DOCUMENTATION=OFF \
    -DCOIN_BUILD_GLX=OFF \
    -DCOIN_BUILD_EGL=ON \
    -DBoost_NO_SYSTEM_PATHS=ON \
    -DBoost_USE_STATIC_LIBS=ON \
    -DBoost_ROOT="${BOOST_SYSROOT}" \
    -DBoost_INCLUDE_DIR="${BOOST_SYSROOT}/include" \
    -DBoost_LIBRARY_DIR="${BOOST_SYSROOT}/lib" \
    ..
