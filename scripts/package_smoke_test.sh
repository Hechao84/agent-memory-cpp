#!/bin/bash

set -e

DIST_DIR="${1:-dist/linux}"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

test -x "$DIST_DIR/bin/memory-server"
test -f "$DIST_DIR/lib/libagent_memory.so"
test -f "$DIST_DIR/include/agent_memory/runtime.h"
test -f "$DIST_DIR/include/agent_memory/builtin_memory_runtime.h"
test -f "$DIST_DIR/include/agent_memory/error.h"
test -f "$DIST_DIR/include/agent_memory/config.h"
test -f "$DIST_DIR/include/agent_memory/context.h"
test -f "$DIST_DIR/include/agent_memory/event.h"
test -f "$DIST_DIR/include/agent_memory/long_term_memory.h"
test -f "$DIST_DIR/include/agent_memory/model_client.h"
test -f "$DIST_DIR/include/agent_memory/payload.h"
test -f "$DIST_DIR/include/agent_memory/search.h"
test -f "$DIST_DIR/examples/memory_server/server_config.example.json"

cat > "$TMP_DIR/sdk_smoke.cpp" <<'CPP'
#include "agent_memory/builtin_memory_runtime.h"

#include <cstdlib>

int main()
{
    agent_memory::MemoryConfig config;
    config.dataPath = std::getenv("AGENT_MEMORY_PACKAGE_SMOKE_DATA");
    agent_memory::BuiltinMemoryRuntime runtime(config);
    auto stats = runtime.GetStats();
    return stats ? 0 : 1;
}
CPP

c++ -std=c++17 "$TMP_DIR/sdk_smoke.cpp" \
    -I"$DIST_DIR/include" \
    -L"$DIST_DIR/lib" \
    -Wl,-rpath,"$DIST_DIR/lib" \
    -lagent_memory \
    -o "$TMP_DIR/sdk_smoke"
AGENT_MEMORY_PACKAGE_SMOKE_DATA="$TMP_DIR/data" LD_LIBRARY_PATH="$DIST_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$TMP_DIR/sdk_smoke"


echo "Package smoke tests passed"
