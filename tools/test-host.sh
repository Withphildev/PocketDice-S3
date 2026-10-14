#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="$repo_root/build"
mkdir -p "$build_dir"

"${CXX:-c++}" \
  -std=c++17 \
  -Wall -Wextra -Wpedantic -Werror \
  -I"$repo_root/core/PocketDiceCore/src" \
  "$repo_root/core/PocketDiceCore/src/PocketDiceCore.cpp" \
  "$repo_root/tests/core_tests.cpp" \
  -o "$build_dir/core_tests"

"$build_dir/core_tests"

