#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cmake -S "$project_dir/backend" -B "$project_dir/backend/build" -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "$project_dir/backend/build" --target notes_print_tests -j 4
ctest --test-dir "$project_dir/backend/build" --output-on-failure -R '^notes_print$'
