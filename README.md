# venus-stack-scripts

macOS (Apple Silicon / hvf) 上 StarryOS venus 加速桌面的宿主侧栈操作脚本与
取证工具集。配套的三个源码仓各自独立维护：

| 仓 | 分支 | 内容 |
| --- | --- | --- |
| [Mchnan/tgoskits](https://github.com/Mchnan/tgoskits)（fork rcore-os/tgoskits） | `venus-into-dev-0924` | guest 内核 card0/vgpu、axdisplay、崩溃取证与全部文档 |
| [Mchnan/virglrenderer-darwin](https://github.com/Mchnan/virglrenderer-darwin) | `darwin-venus` | vkr：shm/MTLBuffer 内存基建、创建失败兼容层、linear tiling 直通 |
| [Mchnan/qemu](https://github.com/Mchnan/qemu) | `darwin-venus` | hvf 未对齐段跳过、pixman blob scanout、UI 刷新、cocoa bounds、模式钉死 |
| [Mchnan/MoltenVK](https://github.com/Mchnan/MoltenVK) | `darwin-venus-probes` | R9-PROBE 仪表（基于上游 main 67b2682 重建） |
| [Mchnan/denial](https://github.com/Mchnan/denial) | `venus-stack-e2e` | 合成器 GL root present 修复 + field 诊断（R6 验证态） |

## 目录

- `qstart-alpine.sh` / `qstart-alpine-mvkmain.sh` — vkprobe/基础栈启动（后者指向重建的 MoltenVK-main ICD）。
- `qstart-denial-e2e.sh` — denial 桌面端到端启动：MoltenVK-main ICD、denial-full rootfs、monitor 套接字（screendump/pmemsave/mtree 取证）。
- `build-qemu.sh` / `build-qemu-git.sh` — 从 tarball / git 树构建 QEMU（链接本 prefix 的 virglrenderer）。
- `e2e-run.sh` — guest 侧 init 脚本（debugfs 注入 rootfs 内）：zink 环境、drirc、取证 watcher（心跳 + dd.log 快照 POST 到 10.0.2.2:8000）。
- `recv.py` / `recv-log.py` — 宿主侧 8000 端口收集器（落 `/tmp/pulled/`）。
- `alphold.py` / `alpser.py` — allocprobe 会话保持与串口桥。
- `host_vk_probe.c` / `vtest_smoke.c` / `metal-shm-test.c` / `allocprobe-host.c` / `fmtprobe-host.c` / `metal_test_spv.h` — MoltenVK/hostmem 行为探针（BAR 往返、compute 读回、Metal shm 语义、格式探针）。
- `shim/venus-protocol/vulkan_metal.h` — guest shim 用的 Metal external-memory 头。
- `tools/qstart-p1.sh` / `tools/vn_renderer_vtest.c` — 第一阶段 vtest 中继取证。

## 取证口径（给下一轮）

- screendump（读 DisplaySurface 内存）与 Cocoa 窗口（等 `dpy_gfx_update` 才重绘）是两条路；绑定层健康判据 = `TEMP set_scanout_blob` 高频且无 `no mapped memory`。
- busybox `wget --post-file` 把 URL 基名当响应体落盘：每次 POST 前清 `/tmp/snap` 与 `/root/snap`。
- 换 ICD/换 mesa 后必须核对日志 `MoltenVK version` 行与 mesa 库代际齐套。
- 完整排查史与缺陷总账见 tgoskits 仓 `docs/design/starry-venus-into-dev-card0.md` 与 `docs/starry-upstream-defects-ledger.canvas.tsx`。
