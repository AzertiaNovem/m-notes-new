#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cmake -S "$project_dir/backend" -B "$project_dir/backend/build" -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release >&2
cmake --build "$project_dir/backend/build" --target embedding_benchmark -j 4 >&2
"$project_dir/backend/build/embedding_benchmark"
