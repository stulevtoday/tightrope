#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
VCPKG_DIR="$ROOT/vcpkg"
ARCH="$(uname -m)"
OS="$(uname -s)"

red()   { printf '\033[0;31m%s\033[0m\n' "$*"; }
green() { printf '\033[0;32m%s\033[0m\n' "$*"; }
dim()   { printf '\033[0;90m%s\033[0m\n' "$*"; }
bold()  { printf '\033[1m%s\033[0m\n' "$*"; }

detect_triplet() {
  local arch
  case "$ARCH" in
    arm64|aarch64) arch="arm64" ;;
    x86_64)        arch="x64"   ;;
    *)             red "Unsupported architecture: $ARCH"; exit 1 ;;
  esac

  case "$OS" in
    Darwin) echo "${arch}-osx"   ;;
    Linux)  echo "${arch}-linux-static"  ;;
    *)      red "Unsupported OS: $OS"; exit 1 ;;
  esac
}

TRIPLET="$(detect_triplet)"

usage() {
  bold "tightrope build system"
  echo ""
  echo "Usage: ./build.sh <command>"
  echo ""
  echo "Commands:"
  echo "  setup       Install vcpkg, dependencies, and npm packages"
  echo "  native      Build the C++ native module (release)"
  echo "  debug       Build the C++ native module (debug + tests)"
  echo "  test        Build debug and run tests"
  echo "  app         Build the Electron app"
  echo "  clean       Remove build artifacts"
  echo "  rebuild     Clean + setup + native"
  echo "  all         setup + native + app"
  echo ""
  dim "Detected: $OS $ARCH → triplet $TRIPLET"
}

cmd_setup() {
  bold "Setting up vcpkg..."

  if [ ! -d "$VCPKG_DIR" ]; then
    git clone https://github.com/microsoft/vcpkg.git "$VCPKG_DIR"
  else
    dim "vcpkg directory exists, updating..."
    git -C "$VCPKG_DIR" pull --ff-only 2>/dev/null || true
  fi

  if [ ! -x "$VCPKG_DIR/vcpkg" ]; then
    "$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics
  fi

  export VCPKG_ROOT="$VCPKG_DIR"

  bold "Installing vcpkg dependencies (triplet: $TRIPLET)..."
  "$VCPKG_DIR/vcpkg" install \
  --triplet="$TRIPLET" \
  --overlay-triplets="$ROOT/triplets" \
  --overlay-ports="$ROOT/ports" \
  --x-manifest-root="$ROOT" \
  --x-install-root="$ROOT/vcpkg_installed" \
  --allow-unsupported

  bold "Installing npm packages..."
  cd "$ROOT/app"
  npm install
  cd "$ROOT"

  green "Setup complete."
}

cmd_native() {
  export VCPKG_ROOT="${VCPKG_ROOT:-$VCPKG_DIR}"
  bold "Building native module (release, $TRIPLET)..."

  cmake -B "$ROOT/build" -S "$ROOT" \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET="$TRIPLET" \
    -DVCPKG_OVERLAY_TRIPLETS="$ROOT/triplets" \
    -DCMAKE_BUILD_TYPE=Release

  cmake --build "$ROOT/build" --parallel "$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"

  if [ -f "$ROOT/build/tightrope-core.node" ]; then
    green "Built: build/tightrope-core.node"
  else
    red "Build completed but tightrope-core.node not found."
    exit 1
  fi
}

cmd_debug() {
  export VCPKG_ROOT="${VCPKG_ROOT:-$VCPKG_DIR}"
  bold "Building native module (debug + tests, $TRIPLET)..."

  cmake -B "$ROOT/build-debug" -S "$ROOT" \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET="$TRIPLET" \
    -DVCPKG_OVERLAY_TRIPLETS="$ROOT/triplets" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON

  cmake --build "$ROOT/build-debug" --parallel "$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"

  green "Debug build complete."
}

cmd_test() {
  cmd_debug

  bold "Running tests..."
  if [ -f "$ROOT/build-debug/tightrope-tests" ]; then
    "$ROOT/build-debug/tightrope-tests" --reporter console
    green "Tests passed."
  else
    dim "No test binary found (no test sources yet)."
  fi
}

cmd_app() {
  bold "Building Electron app..."
  cd "$ROOT/app"

  if [ ! -d node_modules ]; then
    npm install
  fi

  npm run build:main
  npm run build:renderer
  green "Electron app compiled (main + renderer)."
}

cmd_clean() {
  bold "Cleaning build artifacts..."
  rm -rf "$ROOT/build" "$ROOT/build-debug" "$ROOT/app/dist"
  green "Clean."
}

cmd_rebuild() {
  cmd_clean
  cmd_setup
  cmd_native
}

cmd_all() {
  cmd_setup
  cmd_native
  cmd_app
}

case "${1:-}" in
  setup)   cmd_setup   ;;
  native)  cmd_native  ;;
  debug)   cmd_debug   ;;
  test)    cmd_test    ;;
  app)     cmd_app     ;;
  clean)   cmd_clean   ;;
  rebuild) cmd_rebuild ;;
  all)     cmd_all     ;;
  *)       usage       ;;
esac
