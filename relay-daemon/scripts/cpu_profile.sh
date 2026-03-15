#!/usr/bin/env bash
set -euo pipefail

PID=""
DURATION="30"
OUT_DIR="build/perf-cpu"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pid|-p)
      PID="$2"
      shift 2
      ;;
    --duration|-d)
      DURATION="$2"
      shift 2
      ;;
    --out|-o)
      OUT_DIR="$2"
      shift 2
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [[ -z "${PID}" ]]; then
  echo "Usage: $0 --pid <process-id> [--duration <seconds>] [--out <dir>]" >&2
  exit 2
fi

mkdir -p "${OUT_DIR}"
STAMP="$(date +%Y%m%d-%H%M%S)"

if command -v xctrace >/dev/null 2>&1; then
  OUT_FILE="${OUT_DIR}/time-profiler-${STAMP}.trace"
  echo "Recording with xctrace for ${DURATION}s -> ${OUT_FILE}"
  xctrace record \
    --template "Time Profiler" \
    --attach "${PID}" \
    --time-limit "${DURATION}s" \
    --output "${OUT_FILE}"
  echo "Saved: ${OUT_FILE}"
  exit 0
fi

if command -v perf >/dev/null 2>&1; then
  OUT_FILE="${OUT_DIR}/perf-${STAMP}.data"
  echo "Recording with perf for ${DURATION}s -> ${OUT_FILE}"
  perf record -F 99 -g -p "${PID}" --output "${OUT_FILE}" -- sleep "${DURATION}"
  echo "Saved: ${OUT_FILE}"
  exit 0
fi

echo "No supported profiler found (xctrace or perf)." >&2
exit 1
