#!/usr/bin/env bash

mkdir -p dist
version=$(<VERSION)

archive="dist/libwnp-${version}_linux_amd64.tar.gz"
rm -f $archive
tar -cvzf $archive "build/libwnp_linux_amd64.a" --transform='s/build\/libwnp_linux_amd64.a/lib\/libwnp.a/' "include/wnp.h" "CHANGELOG.md" "README.md" "LICENSE" "VERSION" --show-transformed-names

archive_nodp="dist/libwnp-${version}_linux_amd64_nodp.tar.gz"
rm -f $archive_nodp
tar -cvzf $archive_nodp "build/libwnp_linux_amd64_nodp.a" --transform='s/build\/libwnp_linux_amd64_nodp.a/lib\/libwnp.a/' "include/wnp.h" "CHANGELOG.md" "README.md" "LICENSE" "VERSION" --show-transformed-names
