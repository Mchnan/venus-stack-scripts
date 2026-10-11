#!/bin/bash
set -e
cd /Users/herbcheng/venus-stack/src
[ -f qemu-11.0.3.tar.xz ] || curl -sSLO https://download.qemu.org/qemu-11.0.3.tar.xz
[ -d qemu-11.0.3 ] || tar xf qemu-11.0.3.tar.xz
# install our virglrenderer to a local prefix
cd /Users/herbcheng/venus-stack/src/virglrenderer
meson configure build-darwin -Dprefix=/Users/herbcheng/venus-stack/prefix >/dev/null
ninja -C build-darwin >/dev/null
ninja -C build-darwin install >/dev/null
echo "VIRGL_INSTALLED"
ls /Users/herbcheng/venus-stack/prefix/lib/pkgconfig/
cd /Users/herbcheng/venus-stack/src/qemu-11.0.3
export PKG_CONFIG_PATH=/Users/herbcheng/venus-stack/prefix/lib/pkgconfig
./configure --target-list=aarch64-softmmu \
  --enable-virglrenderer \
  --disable-cocoa --disable-gtk --disable-sdl --disable-vnc \
  --disable-docs --disable-user \
  --extra-cflags="-I/opt/homebrew/include" --extra-ldflags="-L/opt/homebrew/lib"
echo "QEMU_CONFIGURED"
ninja -C build
echo "QEMU_BUILT"
ls -la build/qemu-system-aarch64
