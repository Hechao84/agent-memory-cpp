#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-linux"
DIST_DIR="$SCRIPT_DIR/dist/linux"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DIST_DIR"
cmake --build . --target agent_memory memory-server agent_memory_tests consolidation_tests model_tests serialization_tests sqlite_store_tests server_tests -- -j"$(nproc)"
ctest --output-on-failure
bash "$SCRIPT_DIR/scripts/smoke_test.sh"
rm -rf "$DIST_DIR"
cmake --install .
bash "$SCRIPT_DIR/scripts/package_smoke_test.sh" "$DIST_DIR"

echo "Build complete: dist/linux"
