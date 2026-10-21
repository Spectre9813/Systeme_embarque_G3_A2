#!/usr/bin/env bash
# Compilation, téléversement et démonstration de la station 3W (Linux / macOS).
#   scripts/build.sh build | upload | demo | monitor | test | clean | size
#   scripts/build.sh verify /media/$USER/CARTE_SD [FILE_MAX_SIZE]
set -euo pipefail
cd "$(dirname "$0")/.."

PIO=$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")
[ -x "$PIO" ] || { echo "PlatformIO introuvable (extension VS Code ou pip install platformio)"; exit 1; }
PY=$([ -x "$HOME/.platformio/penv/bin/python" ] && echo "$HOME/.platformio/penv/bin/python" || command -v python3)

run() { echo "> pio $*"; "$PIO" "$@"; }
monitor() { mkdir -p logs; (cd logs && run device monitor -d ..); }

case "${1:-help}" in
  build)   run run -e nucleo_l476rg ;;
  size)    run run -e nucleo_l476rg -t size ;;
  clean)   run run -t clean ;;
  upload)  run run -e nucleo_l476rg -t upload ;;
  demo)    run run -e demo -t upload; monitor ;;
  monitor) monitor ;;
  test)    run test -e native ;;
  verify)  "$PY" scripts/verifier_logs.py "${2:?chemin de la carte SD}" --file-max-size "${3:-2048}" ;;
  *)       sed -n '2,4p' "$0" ;;
esac
