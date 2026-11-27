#!/usr/bin/env bash
# Builds the SDK and the offline assistant demo for this machine and downloads the demo models from Hugging Face.
# Usage: ./build_demo.sh   (OS=<target-os> ./build_demo.sh overrides the detected OS)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if [[ -z "${OS:-}" ]]; then
  case "$(uname -s)/$(uname -m)" in
    Darwin/arm64) OS=macos_arm64 ;;
    Darwin/x86_64) OS=macos_x64 ;;
    Linux/x86_64) OS=linux_x86_64 ;;
    *)
      echo "No native demo build for $(uname -s)/$(uname -m); see 'Building' in README.md" >&2
      exit 1
      ;;
  esac
fi
echo "Target OS: $OS"

if ! command -v hf >/dev/null; then
  if [[ "$OS" == macos_* ]]; then hint="brew install hf"; else hint="pip install -U huggingface_hub"; fi
  echo "The 'hf' command is needed to download the models: $hint" >&2
  exit 1
fi

M=example/offline_assistant/model
hf download ai-sage/GigaEdge-Models --include "*/*" --local-dir "$M"
hf download ai-sage/GigaEdge-LLM --include "*/*" --local-dir "$M"
# Only the list of the parts is in the repository: every part, graphs included, must have come from Hugging Face.
parts=$(python3 -c 'import json, sys; m = json.load(open(sys.argv[1])); print(m["core"], *m["parts"].values())' \
          "$M/meta.json")
for part in $parts; do
  [[ -d "$M/$part" ]] || { echo "The part $part of $M/meta.json was not downloaded" >&2; exit 1; }
done

./tools/scripts/build_giga_edge.sh --target-os "$OS" --preset offline_assistant
cmake -S example/offline_assistant -B build/offline_assistant -DCMAKE_BUILD_TYPE=Release \
  -DGIGA_EDGE_DEPS_BUILD="$ROOT/build_$OS" -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build/offline_assistant -j --target offline_assistant_demo

echo "Done. Start the assistant: example/offline_assistant/virtual_house/run.sh"
