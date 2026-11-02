#!/usr/bin/env bash
# Build RPMs with the community Sailfish SDK image (coderus/sailfishos-platform-sdk-*).
# Usage: scripts/docker-build.sh [aarch64|armv7hl|i486] [sfos-version]
#
# The container builds from a copy of the tracked and unignored files, so
# the working tree is never touched; only the RPMs are written to RPMS/,
# owned by the calling user.
set -euo pipefail

ARCH="${1:-aarch64}"
SFOS="${2:-4.5.0.16}"
IMAGE="coderus/sailfishos-platform-sdk-${ARCH}:${SFOS}"
TARGET="SailfishOS-${SFOS}-${ARCH}"
SRC="$(cd "$(dirname "$0")/.." && pwd)"

mkdir -p "${SRC}/RPMS"

cd "${SRC}"
git ls-files -z --cached --others --exclude-standard \
  | tar --null --ignore-failed-read -T - -cf - \
  | docker run --rm -i \
      -v "${SRC}/RPMS:/out" \
      "${IMAGE}" \
      bash -c "set -e
        mkdir -p /home/mersdk/build && cd /home/mersdk/build
        tar -xf -
        mb2 -t ${TARGET} build
        sudo install -o $(id -u) -g $(id -g) -m 644 RPMS/*.rpm /out/"
