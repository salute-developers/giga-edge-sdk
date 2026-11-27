#!/usr/bin/env bash
# The offline assistant in the virtual house: the 3D page instead of a Home Assistant.
#
# Usage: ./run.sh [--voice] [<model dir>]
#   --voice   listen to the microphone («Гига Ёж, …») instead of taking typed requests from the page
# Environment: PORT (8124), HOUSE (lake_house: the house to start with), DEMO (the built offline_assistant_demo),
# LOG (virtual_house.log next to this script: the log of the assistant).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
mode=(--text)
if [[ "${1:-}" == --voice ]]; then
  mode=()
  shift
fi
model="${1:-$here/../model}"
port="${PORT:-8124}"
demo="${DEMO:-$here/../../../build/offline_assistant/offline_assistant_demo}"
log="${LOG:-$here/virtual_house.log}"

[[ -x "$demo" ]] || { echo "no $demo: build the demo (../README.md, step 1) or set DEMO" >&2; exit 1; }
if [[ ! -f "$here/web/dist/index.html" ]]; then
  (cd "$here/web" && npm ci && npm run build)
fi
# The demo reaches the house with curl, which follows the proxy variables; the house is on this machine.
export NO_PROXY="127.0.0.1,localhost${NO_PROXY:+,$NO_PROXY}"
export no_proxy="$NO_PROXY"

python3 "$here/house.py" --port "$port" --house "${HOUSE:-lake_house}" -- \
  "$demo" ${mode[@]+"${mode[@]}"} --ha-url "http://127.0.0.1:$port/api/mcp/assist" "$model" 2>"$log" || {
  status=$?
  echo "stopped with code $status; the end of $log:" >&2
  tail -n 5 "$log" >&2
  exit "$status"
}
