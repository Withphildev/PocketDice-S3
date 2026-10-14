#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

./tools/test-host.sh
node --check web/src/app.js

for file in shared/*.json core/PocketDiceCore/library.json; do
  python3 -m json.tool "$file" >/dev/null
done

git diff --check

if [[ -x .venv/bin/pio ]]; then
  .venv/bin/pio run
  .venv/bin/pio run -t buildfs
else
  echo "PlatformIO not installed in .venv; skipped firmware and LittleFS builds."
fi

