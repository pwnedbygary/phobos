#!/usr/bin/env bash
# Fast environment reconciliation; APK compilation remains an explicit action.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
export GIT_TERMINAL_PROMPT=0
git submodule update --init --recursive
# Do not invoke interactive SDK setup or accept licenses in this hook.
bash scripts/build-replit.sh help