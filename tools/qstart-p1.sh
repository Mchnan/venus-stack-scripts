#!/bin/bash
# Phase 1 vtest boot: SOP denial QEMU + virtio-net (slirp) for guest->host vtest relay
set -u
rm -f /tmp/prof/ser.sock /tmp/prof/console.log /tmp/prof/serctl.sock
exec qemu-system-aarch64 \
  -machine virt -cpu max -accel hvf -smp 4 -m 2048M \
  -display none \
  -monitor unix:/tmp/p1/qmon.sock,server,nowait \
  -chardev socket,id=ser0,path=/tmp/prof/ser.sock,server=on,wait=off,logfile=/tmp/p1/console.log \
  -serial chardev:ser0 \
  -device virtio-gpu-pci -device virtio-keyboard-pci -device virtio-mouse-pci \
  -device virtio-net-pci,netdev=net0 -netdev user,id=net0 \
  -device nvme,drive=disk0,serial=tgoskits,max_ioqpairs=64,msix_qsize=65 \
  -drive id=disk0,if=none,format=raw,file=/Users/herbcheng/tgoskits/tmp/axbuild/rootfs/rootfs-aarch64-denial-full.img \
  -append "root=/dev/nvme0n1 console=ttyS0" \
  -kernel /tmp/vblank-rebase/target/aarch64-unknown-none-softfloat/release/starryos.bin
