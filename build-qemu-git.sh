#!/bin/bash
set -e
cd /Users/herbcheng/venus-stack/src/qemu
export PKG_CONFIG_PATH=/Users/herbcheng/venus-stack/prefix/lib/pkgconfig
./configure --target-list=aarch64-softmmu \
  --enable-virglrenderer \
  --disable-cocoa --disable-gtk --disable-sdl --disable-vnc \
  --disable-docs --disable-user --disable-plugins --disable-snappy \
  --extra-cflags="-I/opt/homebrew/include" > /Users/herbcheng/venus-stack/logs/qemu-git-configure.log 2>&1
echo "QEMU_CONFIGURED"
LIBRARY_PATH=/opt/homebrew/lib ninja -C build > /Users/herbcheng/venus-stack/logs/qemu-git-build.log 2>&1
echo "QEMU_BUILT"
ls -la build/qemu-system-aarch64
