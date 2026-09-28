#!/bin/sh
# release-build.sh — build a universal (arm64 + Intel) dotpipe binary for
# a GitHub Release, so users can skip `make`.
#
# Usage:  sh tools/release-build.sh [version]
#         (default version: git describe --tags --always)
#
# Output: dist/dotpipe-<version>-macos-universal          (the binary)
#         dist/dotpipe-<version>-macos-universal.tar.gz   (binary + README)
#
# The binary is unsigned. macOS Gatekeeper quarantines downloaded files, so
# the first run needs (once):   xattr -d com.apple.quarantine dotpipe
# or right-click the file -> Open.
#
# Upload (from the repo root):
#   gh release create v0.1.0 dist/dotpipe-v0.1.0-macos-universal.tar.gz \
#       --title "dotpipe for macOS (universal)" \
#       --notes "Download, extract, chmod +x dotpipe. Or: make"
set -eu

cd "$(dirname "$0")/.."

VER=${1:-$(git describe --tags --always 2>/dev/null || echo local)}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# -- build one slice per arch (via the Makefile, flags stay single-sourced) --
for arch in arm64 x86_64; do
  make CC="cc -arch $arch" DOTPIPE="$TMP/dotpipe-$arch" "$TMP/dotpipe-$arch"
done

# -- merge into a universal, stripped binary ---------------------------------
mkdir -p dist
lipo -create "$TMP/dotpipe-arm64" "$TMP/dotpipe-x86_64" -output "$TMP/dotpipe"
strip "$TMP/dotpipe"
chmod +x "$TMP/dotpipe"
cp "$TMP/dotpipe" "dist/dotpipe-$VER-macos-universal"

cat > "$TMP/README.txt" <<EOF
dotpipe $VER - macOS (universal: Apple silicon + Intel)

  chmod +x dotpipe
  xattr -d com.apple.quarantine dotpipe   # once, only if macOS complains
  ./dotpipe -w 1280 -h 720 --vignette 0.7 < in.raw > out.raw

Or build from source instead:  make
EOF

tar -czf "dist/dotpipe-$VER-macos-universal.tar.gz" -C "$TMP" dotpipe README.txt

lipo -info "$TMP/dotpipe"
ls -la "dist/dotpipe-$VER-macos-universal" "dist/dotpipe-$VER-macos-universal.tar.gz"
