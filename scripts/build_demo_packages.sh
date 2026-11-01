#!/usr/bin/env bash
# Rebuild the sample runtime packages and the gaussian splat sample.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
python3 tools/gen_gsplat.py assets/splats/genesis_gaussians.ply --count 12000
for src in assets/packages/*_src; do
    [[ -d "$src" ]] || continue
    python3 tools/genesis_pack.py validate "$src"
    python3 tools/genesis_pack.py build "$src"
done
python3 tools/genesis_pack.py list assets/packages/cascade.gpkg >/dev/null
echo "demo packages rebuilt."
