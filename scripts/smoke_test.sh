#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-linux"
DATA_DIR="$(mktemp -d)"
PORT="18090"
SERVER_LOG="$(mktemp)"

cleanup()
{
    if [ -n "${SERVER_PID:-}" ]; then
        kill "$SERVER_PID" >/dev/null 2>&1 || true
    fi
    rm -rf "$DATA_DIR" "$SERVER_LOG"
}
trap cleanup EXIT

LD_LIBRARY_PATH="$BUILD_DIR" "$BUILD_DIR/memory-server" --host 127.0.0.1 --port "$PORT" --data "$DATA_DIR" > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 1

curl -fsS "http://127.0.0.1:$PORT/health" >/dev/null
curl -fsS -X POST "http://127.0.0.1:$PORT/v1/events" \
    -H 'Content-Type: application/json' \
    -d '{"type":2,"agentId":"agent-1","sessionId":"session-1","role":"user","content":"I prefer concise answers about tests"}' >/dev/null
curl -fsS -X POST "http://127.0.0.1:$PORT/v1/consolidate" \
    -H 'Content-Type: application/json' \
    -d '{"agentId":"agent-1","sessionId":"session-1","force":true}' >/dev/null
curl -fsS -X POST "http://127.0.0.1:$PORT/v1/context" \
    -H 'Content-Type: application/json' \
    -d '{"agentId":"agent-1","sessionId":"session-1","query":"answer"}' >/dev/null
curl -fsS "http://127.0.0.1:$PORT/v1/stats" >/dev/null

printf '%s\n' \
    '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
    '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' \
    '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"memory_append_event","arguments":{"type":2,"agentId":"agent-1","sessionId":"session-1","role":"user","content":"I prefer MCP tests"}}}' \
    '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"memory_consolidate","arguments":{"agentId":"agent-1","sessionId":"session-1","force":true}}}' \
    '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"memory_stats","arguments":{}}}' \
    | LD_LIBRARY_PATH="$BUILD_DIR" "$BUILD_DIR/memory-mcp-server" --data "$DATA_DIR" >/dev/null

echo "Smoke tests passed"
