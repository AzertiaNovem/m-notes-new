#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S backend -B backend/build -DCMAKE_BUILD_TYPE=Release
cmake --build backend/build -j 4
pnpm install --frozen-lockfile
pnpm build:web
exec ./backend/build/mistakebook
