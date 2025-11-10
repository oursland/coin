#!/usr/bin/env bash
set -euo pipefail
set -x

# ---------------------------------------------------------------------------
# Configuration (can be overridden via environment variables)
# ---------------------------------------------------------------------------
TARGET="${TARGET:-native}"           # e.g. "emscripten" or "native"
VARIANT="${VARIANT:-release}"        # build variant used in b2
BUILD_DIR="${BUILD_DIR:-build}"      # Boost build directory
SYSROOT_DIR="${SYSROOT_DIR:-sysroot}" # installation prefix for boost install
VERBOSE="${VERBOSE:-false}"          # "true" or "false"

# Additional configurable bits
B2_LINK="${B2_LINK:-static}"
B2_RUNTIME_LINK="${B2_RUNTIME_LINK:-shared}"
B2_EXCEPTION_HANDLING_METHOD="${B2_EXCEPTION_HANDLING_METHOD:-js}"

# ---------------------------------------------------------------------------
# Derived values (mirror the original build logic)
# ---------------------------------------------------------------------------

if [[ "${TARGET}" == "emscripten" ]]; then
  b2_threading="single"
  b2_toolset="emscripten"
else
  b2_threading="multi"
  b2_toolset="gcc"
fi

b2_variant="${VARIANT}"
b2_link="${B2_LINK}"
b2_runtime_link="${B2_RUNTIME_LINK}"
b2_exception_handling_method="${B2_EXCEPTION_HANDLING_METHOD}"

# b2-options
b2_options=(
  "--build-dir=./${BUILD_DIR}"
  "--prefix=${SYSROOT_DIR}"
  "--with-filesystem"
  "--with-program_options"
  "--with-regex"
  "--with-system"
  "--with-thread"
  "--with-date_time"
  "--layout=target"
  "toolset=${b2_toolset}"
)

if [[ "${VERBOSE}" == "true" ]]; then
  b2_options+=("-d2")
fi

# b2-build-options
b2_build_options=(
  "link=${b2_link}"
  "variant=${b2_variant}"
  "threading=${b2_threading}"
  "runtime-link=${b2_runtime_link}"
  "cxxflags=-fPIC -pthread -DBOOST_HAS_PTHREADS"
  "linkflags=-pthread"
)

# Defined but not wired into commands by default.
if [[ "${TARGET}" == "emscripten" ]]; then
  b2_target_specific_options=(
    "exception-handling-method=${b2_exception_handling_method}"
  )
else
  b2_target_specific_options=()
fi

# ---------------------------------------------------------------------------
# Working directory (set working-directory := "../../../thirdparty/boost")
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOST_DIR="${SCRIPT_DIR}/thirdparty/boost"
if [[ ! -d "${BOOST_DIR}" ]]; then
  cat <<'EOF' >&2
Boost source directory not found at '${BOOST_DIR}'.
Please clone or initialize thirdparty/boost before running this script.
Example: git clone --recursive https://github.com/boostorg/boost thirdparty/boost
EOF
  exit 1
fi

cd "${BOOST_DIR}"

# ---------------------------------------------------------------------------
# Functions implementing bootstrap/build/install steps
# ---------------------------------------------------------------------------

bootstrap_b2() {
  cd tools/build
  if [[ ! -f b2 ]]; then
    ./bootstrap.sh
    # workaround https://github.com/bfgroup/b2/discussions/337
    sed -i 's/-fwasm-exceptions/-fexceptions/g' src/tools/emscripten.jam
  fi
  cd ../..
}

build() {
  mkdir -p "${BUILD_DIR}"
  ./tools/build/b2 "${b2_options[@]}" headers
  ./tools/build/b2 "${b2_options[@]}" "${b2_build_options[@]}" stage
}

install_boost() {
  ./tools/build/b2 "${b2_options[@]}" "${b2_build_options[@]}" install

  # Optional: if EMSDK_SYSROOT is defined, copy the generated sysroot there.
  # if [[ "${TARGET}" == "emscripten" && -n "${EMSCRIPTEN_SYSROOT:-}" ]]; then
  #   cp -R sysroot/* "${EMSCRIPTEN_SYSROOT}/"
  # fi
}

# ---------------------------------------------------------------------------
# CLI: mimic `default: bootstrap-b2 build install`
# ---------------------------------------------------------------------------
# Usage:
#   ./boost-build.sh            # runs bootstrap-b2, build, install
#   ./boost-build.sh bootstrap-b2
#   ./boost-build.sh build
#   ./boost-build.sh install
# ---------------------------------------------------------------------------

cmd="${1:-all}"

case "${cmd}" in
  bootstrap-b2)
    bootstrap_b2
    ;;
  build)
    build
    ;;
  install)
    install_boost
    ;;
  all)
    bootstrap_b2
    build
    install_boost
    ;;
  *)
    echo "Usage: $0 [bootstrap-b2|build|install|all]" >&2
    exit 1
    ;;
esac
