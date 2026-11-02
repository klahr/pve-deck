#!/usr/bin/env bash
# Build RPMs with the community Sailfish SDK image (coderus/sailfishos-platform-sdk-*).
# Usage: scripts/docker-build.sh [aarch64|armv7hl|i486] [sfos-version]
set -euo pipefail

ARCH="${1:-aarch64}"
SFOS="${2:-4.5.0.16}"
IMAGE="coderus/sailfishos-platform-sdk-${ARCH}:${SFOS}"
TARGET="SailfishOS-${SFOS}-${ARCH}"
SRC="$(cd "$(dirname "$0")/.." && pwd)"

docker run --rm \
  -v "${SRC}:/home/mersdk/src" \
  -w /home/mersdk/src \
  "${IMAGE}" \
  bash -c "sudo chown -R mersdk:mersdk /home/mersdk/src && mb2 -t ${TARGET} build"
