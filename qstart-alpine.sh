#!/bin/bash
set -u
# ACCEL=hvf fails MAP_BLOB on blobs whose guest-chosen hostmem offset is not
# 16K-aligned (HVF requires 16K gpa/host alignment; the Linux guest allocator
# works at 4K). TCG has no such constraint -- use it for the venus probe.
ACCEL="${QEMU_ACCEL:-tcg}"
CPU_ARGS="-cpu max"
if [ "$ACCEL" = "hvf" ]; then CPU_ARGS="-cpu host"; fi
rm -f /Users/herbcheng/venus-stack/alp.sock /Users/herbcheng/venus-stack/alp_console.log /Users/herbcheng/venus-stack/alpctl.sock
export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export DYLD_LIBRARY_PATH=/opt/homebrew/lib:/Users/herbcheng/venus-stack/prefix/lib
exec /Users/herbcheng/venus-stack/src/qemu/build/qemu-system-aarch64 \
  -machine virt -accel "$ACCEL" $CPU_ARGS -smp 4 -m 4096 \
  -kernel /Users/herbcheng/venus-stack/vmlinuz-lts -initrd /Users/herbcheng/venus-stack/initramfs-lts \
  -append "console=ttyAMA0 ip=10.0.2.15::10.0.2.2:255.255.255.0:alpine:eth0:on:10.0.2.3 alpine_repo=http://10.0.2.2:8000 modloop=http://10.0.2.2:8000/modloop-lts modules=loop,squashfs,sd-mod,usb-storage" \
  -device virtio-net-pci,netdev=n0 -netdev user,id=n0 \
  -device virtio-gpu-gl,blob=true,venus=true,hostmem=256M,max_hostmem=1G \
  -display none \
  -d trace:virtio_gpu_cmd_res_create_blob,trace:virtio_gpu_cmd_res_map_blob,trace:virtio_gpu_cmd_ctx_res_attach,trace:virtio_gpu_cmd_ctx_create,trace:virtio_gpu_cmd_ctx_submit,trace:virtio_gpu_fence_ctrl,trace:virtio_gpu_fence_resp,guest_errors \
  -monitor unix:/Users/herbcheng/venus-stack/alpmon.sock,server,nowait \
  -chardev socket,id=ser0,path=/Users/herbcheng/venus-stack/alp.sock,server=on,wait=off,logfile=/Users/herbcheng/venus-stack/alp_console.log \
  -serial chardev:ser0
