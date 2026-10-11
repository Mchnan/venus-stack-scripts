#!/bin/bash
# Round-9 Cocoa e2e: rebuilt MoltenVK (upstream SPIRV-Cross @ aa217aeb) + denial-full rootfs.
# Forensics: MVK logs + vkr logs -> /tmp/qemu-starry-venus.log (stderr),
# watcher POSTs dd.log snapshots to 10.0.2.2:8000 (recv-log.py on host),
# screendump via monitor socket denmon.sock.
set -u
# NOTE: never rm the QEMU stdout/stderr redirect target here — the launch
# shell opens it before this script runs; deleting it loses all MVK logs.
rm -f /Users/herbcheng/venus-stack/den.sock /Users/herbcheng/venus-stack/den_console.log \
      /Users/herbcheng/venus-stack/denmon.sock
# Round-9: ICD swapped to the rebuilt MoltenVK main dylib (brew 1.4.2 untouched).
export VK_DRIVER_FILES=/Users/herbcheng/venus-stack/prefix/share/vulkan/icd.d/MoltenVK-main_icd.json
export VK_ICD_FILENAMES="$VK_DRIVER_FILES"
export DYLD_LIBRARY_PATH=/opt/homebrew/lib:/Users/herbcheng/venus-stack/prefix/lib
export MVK_CONFIG_LOG_LEVEL=3
export MVK_CONFIG_REDIRECT_LOGGING_TO_STDERR=1
exec /Users/herbcheng/venus-stack/src/qemu/build/qemu-system-aarch64 \
  -machine virt -accel hvf -cpu host -smp 4 -m 4096 \
  -kernel /Users/herbcheng/tgoskits/target/aarch64-unknown-none-softfloat/release/starryos.bin \
  -append "root=/dev/nvme0n1 console=ttyS0 init=/root/e2e-run.sh" \
  -device nvme,drive=disk0,serial=tgoskits,max_ioqpairs=64,msix_qsize=65 \
  -drive id=disk0,if=none,format=raw,file=/Users/herbcheng/tgoskits/tmp/axbuild/rootfs/rootfs-aarch64-denial-full.img \
  -device virtio-gpu-gl,blob=true,venus=true,hostmem=256M,max_hostmem=1G,xres=1280,yres=800 \
  -device virtio-net-pci,netdev=n0 -netdev user,id=n0 \
  -device virtio-keyboard-pci -device virtio-mouse-pci \
  -monitor unix:/Users/herbcheng/venus-stack/denmon.sock,server,nowait \
  -chardev socket,id=ser0,path=/Users/herbcheng/venus-stack/den.sock,server=on,wait=off,logfile=/Users/herbcheng/venus-stack/den_console.log \
  -serial chardev:ser0
