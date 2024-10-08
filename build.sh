#!/usr/bin/env bash

OS=$(uname)
ARCH=$(uname -m)
PLATFORM=""
if [ "$OS" == "Linux" ]; then
  if [ "$ARCH" == "x86_64" ]; then
    PLATFORM="linux_amd64"
  elif [ "$ARCH" == "aarch64" ]; then
    PLATFORM="linux_aarch64"
  else
    echo "Unsupported Linux architecture: $ARCH"
    exit 1
  fi
elif [ "$OS" == "Darwin" ]; then
  if [ "$ARCH" == "x86_64" ]; then
    PLATFORM="macos_amd64"
  elif [ "$ARCH" == "arm64" ]; then
    PLATFORM="macos_aarch64"
  else
    echo "Unsupported macOS architecture: $ARCH"
    exit 1
  fi
else
  echo "Unknown OS: $OS"
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

rm -rf "$SCRIPT_DIR/build"
rm -rf "$SCRIPT_DIR/dist"

mkdir "$SCRIPT_DIR/build"
mkdir "$SCRIPT_DIR/dist"

cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build" -DCMAKE_INSTALL_PREFIX="$SCRIPT_DIR/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$SCRIPT_DIR/build"
cmake --install "$SCRIPT_DIR/build"

VERSION=$(cat "$SCRIPT_DIR/VERSION")

tar -czf "$SCRIPT_DIR/dist/libwnp-${VERSION}_${PLATFORM}.tar.gz" \
  -C "$SCRIPT_DIR" README.md LICENSE CHANGELOG.md VERSION \
  -C "$SCRIPT_DIR/build" lib include
