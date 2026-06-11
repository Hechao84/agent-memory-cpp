#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-linux"
DIST_DIR="$SCRIPT_DIR/dist/linux"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target agent_memory memory-server memory-mcp-server agent_memory_tests -- -j"$(nproc)"
./agent_memory_tests
bash "$SCRIPT_DIR/scripts/smoke_test.sh"

mkdir -p "$DIST_DIR/bin" "$DIST_DIR/lib" "$DIST_DIR/include" "$DIST_DIR/examples"
cp memory-server "$DIST_DIR/bin/"
cp memory-mcp-server "$DIST_DIR/bin/"
cp libagent_memory.so "$DIST_DIR/lib/"
cp -r "$SCRIPT_DIR/include"/* "$DIST_DIR/include/"
cp -r "$SCRIPT_DIR/examples"/* "$DIST_DIR/examples/"

echo "Build complete: dist/linux"
