#!/usr/bin/env bash
#
# benchmark.sh — Compare httpserver vs nginx on simple GET requests.
#
# Dependencies: wrk (https://github.com/wg/wrk), nginx
# Usage:  ./benchmark.sh [our_port] [nginx_port] [duration] [connections]
#
# Example: ./benchmark.sh 8080 80 10s 100

set -euo pipefail

OUR_PORT="${1:-8080}"
NGINX_PORT="${2:-80}"
DURATION="${3:-10s}"
CONNECTIONS="${4:-100}"
THREADS=4
URL_PATH="/index.html"

run_wrk() {
    local label="$1"
    local port="$2"
    echo "=== $label (port $port, ${CONNECTIONS} conns, ${DURATION}) ==="
    wrk -t"$THREADS" -c"$CONNECTIONS" -d"$DURATION" \
        "http://localhost:${port}${URL_PATH}"
    echo
}

if ! command -v wrk &>/dev/null; then
    echo "Error: 'wrk' not found. Install it with: sudo apt install wrk"
    exit 1
fi

run_wrk "httpserver" "$OUR_PORT"
run_wrk "nginx"      "$NGINX_PORT"
