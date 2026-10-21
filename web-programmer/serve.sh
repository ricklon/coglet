#!/usr/bin/env bash
# Serve the Coglet Web Programmer locally (Web Serial needs localhost or HTTPS).
# Usage: ./serve.sh [port]   (default 8080). Package firmware first: just package
set -euo pipefail
cd "$(dirname "$0")"
exec python3 serve.py "${1:-8080}"
