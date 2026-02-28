#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CMAKE_FILE="$ROOT_DIR/CMakeLists.txt"
BUILD_DIR="$ROOT_DIR/build"

current_version="$(sed -n 's/^set(VITA_VERSION  "\([0-9][0-9]\.[0-9][0-9]\)")$/\1/p' "$CMAKE_FILE")"
if [ -z "$current_version" ]; then
  echo "Unable to parse VITA_VERSION from $CMAKE_FILE" >&2
  exit 1
fi

major="${current_version%%.*}"
minor="${current_version##*.}"
major_num=$((10#$major))
minor_num=$((10#$minor + 1))

if [ "$minor_num" -gt 99 ]; then
  minor_num=0
  major_num=$((major_num + 1))
fi

new_version="$(printf '%02d.%02d' "$major_num" "$minor_num")"

perl -0pi -e "s/set\(VITA_VERSION\s+\"[0-9]{2}\.[0-9]{2}\"\)/set(VITA_VERSION  \"$new_version\")/" "$CMAKE_FILE"

echo "Bumped VITA_VERSION: $current_version -> $new_version"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make -j

ls -lh vita_wifi_scope.vpk
