# SM-P205 / wisdom Linux 4.4.302 Bring-up

## 1. 目标

在不破坏当前已验证的 LineageOS 23.2 / wisdom 4.4.177 内核基线的前提下：

1. 将现有 universal7904 内核合并到官方 Linux `v4.4.302`。
2. 保留 SM-P205 / wisdom 的板级支持、Android 兼容补丁和已经验证的设备修复。
3. 从 Eureka Kernel 中按风险分批移植可验证的性能或正确性修复。
4. 保留 `exynos7904-perfd` 0.4.2 作为用户态策略层，并避免与内核 boost 机制互相争用。
5. 通过设备验证后，才替换 ROM device tree 中的 prebuilt kernel。

这不是从 Eureka 整树移植 wisdom，也不是只修改 `Makefile` 的 `SUBLEVEL`。

## 2. 当前基线

### 2.1 工作区与分支

| 项目 | 值 |
| --- | --- |
| 原有 LOS23.2 内核工作区 | `/home/hajimi/lineageos23.2/kernel/samsung/universal7904` |
| 新 4.4.302 工作区 | `/home/hajimi/android_kernel_samsung_universal7904-4.4.302` |
| 原分支 | `lineage-23.2` |
| 新分支 | `lineage-23.2-302` |
| 新分支起点 | `72aae3c51a516405605a483dbe9732d498ec961a` |
| 起点提交 | `media: mfc: requeue pending encoder work` |
| 起点源码版本 | `4.4.177` |
| 合并后源码版本 | `4.4.302` |
| 官方 `v4.4.177` commit | `6b50202a4d53bf527c640467bcff68b50a5e38a2` |
| 官方 `v4.4.302` commit | `a09b2d8f61ea0e9ae735c400399b97966a9418d6` |
| Google Android 4.4.302 merge | `875c0cc8115381f702b12d41de293807f47cdac9` |
| universal7904 同源 bring-up | `b128d69019a8c1dd05b10b2879c9eaa0de3207c6` |
| 官方 stable 分支 | `linux-4.4.y` |

新工作区使用独立 clone，避免把旧工作区内未跟踪的 `KernelSU-Next/`、`out-gcc/`
或其他生成物带入 4.4.302 分支。

### 2.2 Git 关系

当前 wisdom 分支是官方 `v4.4.177` 的后代：

- 当前分支相对 `v4.4.177` 有约 851 个下游提交。
- 官方 `v4.4.302` 相对 `v4.4.177` 有约 7783 个 stable 提交。
- 当前分支和 `v4.4.302` 的共同祖先是官方 `v4.4.177`。

直接把官方 `linux-4.4.y` 合并进当前分支的试运行产生了 92 个冲突。继续手工
猜测这些 Android/Samsung 下游冲突的语义，不如复用同一 universal7904 源码已经
完成并长期使用的 Android 4.4.302 bring-up：

- `b128d69019` 的第一父提交 `86c332d77b` 只比当前双方共同基点 `d6d61c0be4`
  多一个 flip-cover 修复。
- 第二父提交 `875c0cc811` 是 Google `android-4.4-p` 合并官方 `v4.4.302`
  的提交，且其第二父提交就是 `a09b2d8f61`。
- `b128d69019` 已经处理 universal7904 与 Android 4.4.302 的冲突，提交说明还
  记录了 Samsung MTP gadget 的 NULL 检查及多 gadget 兼容处理。
- 当前 `lineage-23.2` 只在共同基点之后增加 18 个设备/Android 16 修复，因此
  将 `b128d69019` 合并进当前分支只产生 1 个真实冲突。

采用这条同源 merge ancestry，仍然完整包含官方 `v4.4.302` 历史，同时不会带入
Samar 分支在 `b128d69019` 之后的 CIP、BPF、LZ4、vmalloc 或性能修改。

不采用以下方法：

- cherry-pick 7783 个 stable 提交；
- 使用 Eureka 源码覆盖当前树；
- 把 Eureka 的 A20/A30 defconfig 改名成 `wisdom_defconfig`。

### 2.3 ROM 打包现状

`device/samsung/wisdom/BoardConfig.mk` 当前包含：

```make
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/prebuilt/Image
TARGET_FORCE_PREBUILT_KERNEL := true
TARGET_KERNEL_CONFIG := wisdom_defconfig
```

所以修改 kernel 源码本身不会进入 ROM。必须先独立构建、上机验证，然后明确替换：

```text
/home/hajimi/lineageos23.2/device/samsung/wisdom/prebuilt/Image
```

替换后还要验证源码构建输出、device prebuilt 和 ROM `out/.../kernel` 的 SHA-256
完全一致。

`device/samsung/universal7904-common/BoardConfigCommon.mk` 还有一个后续注意点：

```make
TARGET_KERNEL_ADDITIONAL_FLAGS += LD=ld.lld AR=llvm-ar ...
TARGET_KERNEL_ADDITIONAL_FLAGS := HOSTCFLAGS="..."
```

第二行的 `:=` 会覆盖第一行。第一阶段继续使用已知可工作的独立 GCC 4.9 构建，
不同时切换 ROM 内源码构建、Clang 或 LTO。以后如果切换到 ROM 内编译，需要单独
修正该赋值。

## 3. 总体策略

### 阶段 A：纯 4.4.302 bring-up

1. 从 `72aae3c` 创建 `lineage-23.2-302`。
2. 获取官方 `linux-4.4.y` 到 `a09b2d8`，只用于核验版本和 ancestry。
3. 获取同源 universal7904 的 `b128d69019` Android 4.4.302 bring-up。
4. 将 `b128d69019` 合并进当前分支，保留当前 18 个 LOS23.2/设备修复。
5. 处理当前分支与同源 bring-up 的少量冲突。
6. 保持 `wisdom_defconfig` 和现有 DTS/驱动，不导入 Eureka defconfig。
7. 先让 4.4.302 Android baseline 编译并启动，不加入 Eureka 性能修改，也不开
   perfd。

### 阶段 B：恢复并验证 wisdom ABI

必须保留或重新验证：

- P205 DTS、面板、触摸和 Wacom/S Pen。
- S2MU004 MUIC、USB/Type-C 音频和 2.4G 接收器相关路径。
- Exynos MFC 与录像修复。
- Android 16 所需 BPF/netd、cgroup、namespace、binder 和 SELinux 兼容代码。
- KernelSU 现有集成。
- 电池、充电、休眠和 Wi-Fi/WLBT 路径。
- HMP `selective_boost`。
- cpufreq `policy0` / `policy6` 以及现有 OPP 表。
- perfd 使用的 thermal zone、cpuset 和 sysfs ABI。

### 阶段 C：Eureka 按批移植

Eureka R24U 的源码版本为 `4.4.302-p6`，但其主要目标是 A10/A20/A20e/A30/A30s/
A40/M20 等机型，没有可直接使用的 P205/wisdom 板级 defconfig。

Eureka 仅用于：

- 参考其 4.4.302 冲突处理；
- 选择有明确原因、范围较小、可独立测试的修复；
- 对比最终代码，而不是按提交标题盲目 cherry-pick。

## 4. Eureka 候选分组

### 4.1 第一批：正确性修复

这些提交应在纯 4.4.302 启动后，先检查是否已存在等价修复，再逐个移植：

| Commit | 作用 | 处理 |
| --- | --- | --- |
| `4ca9d60a459a` | 修复 Android cpufreq time-in-state 内存泄漏 | 优先检查 |
| `ed13ccc89174` | 修复 cpuidle kobject 引用清理 | 优先检查 |
| `53d523204806` | devfreq governor resume 后重新更新频率 | 可单独测试 |
| `85320521da60` | 非法 CPU 编号不触发 WARN | 非性能项，低优先级 |

### 4.2 第二批：需要 A/B 测试的性能修改

| Commit/方向 | 风险 |
| --- | --- |
| `5401370b886c`：负载均衡时更新 CPU capacity | 可能改善放置，也会增加调度开销 |
| scheduler latency/min-granularity 调整 | 会同时影响吞吐、交互和功耗 |
| scheduler migration cost 调整 | Eureka 提交标题和实际数值不一致，禁止直接照抄 |
| power-efficient workqueue | 可能省电，也可能增加交互延迟 |
| ZRAM LZ4 默认值 | 先确认 Android userspace 实际选择的压缩算法 |
| devfreq input boost | 与 perfd 的触摸/选择性 boost 叠加后可能增加温度 |

调度参数如果最终采用，应根据 R24U 最终代码重新整理成一个 wisdom 专用提交，
不能照搬中间状态的多次 tweak/revert。

### 4.3 暂缓

- `f007733cb2d7`：MFC 录像状态联动 devfreq boost。
  - 该提交直接修改 MFC。
  - wisdom 刚修复录像问题。
  - 必须等纯 4.4.302 的录像链路稳定后再测试。
- Mali GPU threshold、GPU 驱动升级。
- CPU/GPU 超频、降压或替换 OPP/电压表。
- 新 I/O scheduler 和大量第三方 governor。

### 4.4 明确拒绝

以下修改不进入第一轮 bring-up：

- 30 秒后强制杀 wakelock。
- 停止周期性脏页回写。
- 全局 always-overcommit。
- 关闭 TTWU_QUEUE。
- 将所有 SCHED_FIFO 改成 SCHED_RR。
- 为减少开销关闭 arm64 hardware breakpoint。
- 没有可审计说明的 “gotta go fast” 类修改。
- 默认 CPU offlining、强制 Doze、修改 thermal trip。

## 5. exynos7904-perfd 合并策略

当前目标是 `exynos7904-perfd` 0.4.2，而不是恢复旧脚本的所有调参。

当前已验证的策略：

- 屏幕开启使用 `fast`。
- 屏幕关闭使用 `powersave`。
- 管理 `policy0` 和 `policy6` 的最低频率 lease。
- 按触摸、应用切换、持续 BIG 核负载申请短期 boost。
- 使用 HMP `selective_boost`。
- 85 摄氏度 thermal guard 及 5 摄氏度 hysteresis。
- 不修改 governor、thermal trip、GPU、MIF、CPU online 或 HMP threshold。
- 退出或异常恢复时归还所管理的频率和 affinity。

不要恢复旧版本中的这些内容：

- interactive `timer_rate` / `go_hispeed_load` / `hispeed_freq`。
- `dirty_writeback_centisecs` 动态修改。
- 全局 read-ahead 修改。
- GPU/MIF/thermal 节点写入。

集成顺序：

1. 纯 4.4.302，perfd 关闭。
2. 纯 4.4.302，perfd 0.4.2 开启。
3. 加入 Eureka 正确性修复，perfd 开启。
4. 每次只加入一个或一组可独立回退的性能实验。

## 6. 实际采用的 4.4.302 合并命令

新工作区初始创建命令：

```bash
git clone --filter=blob:none --depth=1000 --single-branch \
  -b lineage-23.2 \
  https://github.com/xuanyayi/android_kernel_samsung_universal7904.git \
  /home/hajimi/android_kernel_samsung_universal7904-4.4.302

cd /home/hajimi/android_kernel_samsung_universal7904-4.4.302
git switch -c lineage-23.2-302
```

拉取官方 stable，用于核对官方目标和共同祖先：

```bash
git remote add linux-stable \
  https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git

git fetch --filter=blob:none --shallow-exclude=v4.4.176 linux-stable \
  +refs/heads/linux-4.4.y:refs/remotes/linux-stable/linux-4.4.y
```

检查共同祖先：

```bash
git merge-base --is-ancestor \
  6b50202a4d53bf527c640467bcff68b50a5e38a2 HEAD

git merge-base HEAD linux-stable/linux-4.4.y
```

预期共同祖先：

```text
6b50202a4d53bf527c640467bcff68b50a5e38a2
```

纯 stable 直接合并只作为冲突规模试运行：

```bash
git merge --no-ff --no-commit linux-stable/linux-4.4.y
git diff --name-only --diff-filter=U
git merge --abort
```

该试运行得到 92 个冲突，没有提交。

实际采用同源 universal7904 Android 4.4.302 merge：

```bash
git remote add samar \
  https://github.com/SamarV-121/android_kernel_samsung_universal7904.git

git fetch --depth=1000 samar \
  +refs/heads/lineage-22.2:refs/remotes/samar/lineage-22.2

git merge-base HEAD b128d69019a8c1dd05b10b2879c9eaa0de3207c6
git show -s --format=raw b128d69019a8c1dd05b10b2879c9eaa0de3207c6
git merge --no-ff --no-commit \
  b128d69019a8c1dd05b10b2879c9eaa0de3207c6
```

这次只冲突 `kernel/locking/rtmutex.c`。解析方式是同时保留：

- 4.4.302 的 `lock->wait_lock` IRQ 语义；
- 当前修复使用 `waiter->task`，而不是错误使用 `current`；
- `rt_mutex_adjust_prio_chain()` 最后一个参数继续传递该 waiter task。

完成的 merge commit：

```text
228624978b Merge Android 4.4.302 bring-up into lineage-23.2-302
```

此外，旧树的
`drivers/gpu/arm/b_r26p0/platform/devicetree/Kbuild`
本来就提交了残留冲突标记；在独立提交 `4383ad3f2c` 中清理，避免 Make 解析失败。

## 7. 第一阶段构建命令

第一版保持和当前已验证 kernel 一样的 GCC 4.9：

```bash
export ARCH=arm64
export SUBARCH=arm64
export CROSS_COMPILE=/home/hajimi/lineageos23.2/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-
export CROSS_COMPILE_ARM32=/home/hajimi/lineageos23.2/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9/bin/arm-linux-androideabi-

KERNEL_SRC=/home/hajimi/android_kernel_samsung_universal7904-4.4.302
KERNEL_OUT=/home/hajimi/kobj-wisdom-4.4.302

make -C "$KERNEL_SRC" O="$KERNEL_OUT" wisdom_defconfig
make -C "$KERNEL_SRC" O="$KERNEL_OUT" olddefconfig
make -C "$KERNEL_SRC" O="$KERNEL_OUT" -j"$(nproc)" Image dtbs
```

第一阶段不同时启用：

- Eureka Vortex/Proton Clang。
- LTO。
- 新 governor。
- 新 I/O scheduler。
- 超频/降压。

## 8. 验证矩阵

### 8.1 静态与构建

- `Makefile` 必须显示 `VERSION=4`、`PATCHLEVEL=4`、`SUBLEVEL=302`。
- `wisdom_defconfig` 能完成 `olddefconfig`。
- 对比旧/新生成 `.config`，逐项审查新 Kconfig 默认值。
- `Image`、DTB 和所有启用模块成功生成。
- 记录 compiler、UTS version、Image size、SHA-256。
- 检查 boot partition 32 MiB 大小限制。

### 8.2 首次启动

- `uname -a` 和 `/proc/version` 显示实际 4.4.302。
- Android UI 可用，不能仅以 `adbd started` 判定成功。
- 无 kernel panic、Oops、连续 WARN、RCU stall 或 watchdog。
- init、vold、netd、zygote、system_server 正常。
- BPF/netd 网络初始化正常。

### 8.3 设备功能

- 相机预览、拍照和持续录像。
- Type-C 耳机播放、录音和拔插。
- 2.4G USB 接收器。
- USB MTP/ADB。
- S Pen 书写、悬浮、侧键和唤醒。
- Wi-Fi、蓝牙、移动网络、GPS。
- 充电、电量显示和 thermal。
- 内置/外置存储。
- KernelSU。
- suspend/resume、Doze 和待机。

### 8.4 性能 A/B

每次使用同一 ROM、同一 userspace 和同一环境：

| 组 | 内核 | perfd | Eureka |
| --- | --- | --- | --- |
| A | 4.4.177 | 关闭/开启 | 无 |
| B | 纯 4.4.302 | 关闭 | 无 |
| C | 纯 4.4.302 | 0.4.2 | 无 |
| D | 4.4.302 | 0.4.2 | 正确性修复 |
| E | 4.4.302 | 0.4.2 | 单个性能实验 |

记录：

- 应用启动中位数和 P95。
- SurfaceFlinger/gfxinfo 卡顿。
- sustained load 下的频率、温度和降频。
- 录像时的帧率、编码稳定性和温度。
- 屏幕关闭 10 至 30 分钟的真实断电待机。
- perfd runtime state、频率 lease 和 selective boost。

## 9. Prebuilt 替换门槛

只有以下条件都满足后，才替换 device tree prebuilt：

1. 新内核实际启动到可用 Android UI。
2. 录像、USB 音频、2.4G 接收器和 S Pen 全部回归通过。
3. 网络、充电、存储和待机没有明显回退。
4. perfd 0.4.2 ABI 检查通过。
5. 没有未解释的 panic、Oops、stall 或高频 WARN。
6. 记录构建输出 SHA-256。

替换后验证：

```bash
sha256sum \
  /home/hajimi/kobj-wisdom-4.4.302/arch/arm64/boot/Image \
  /home/hajimi/lineageos23.2/device/samsung/wisdom/prebuilt/Image \
  /home/hajimi/lineageos23.2/out/target/product/wisdom/kernel
```

三者哈希必须一致，且 ROM ZIP 中的实际 payload 还要再次验证版本字符串。

## 10. Bring-up 日志

### 2026-07-29

- [x] 核对远端 `lineage-23.2` HEAD：
  `72aae3c51a516405605a483dbe9732d498ec961a`
- [x] 核对官方 `linux-4.4.y` HEAD：
  `a09b2d8f61ea0e9ae735c400399b97966a9418d6`
- [x] 创建独立工作区：
  `/home/hajimi/android_kernel_samsung_universal7904-4.4.302`
- [x] 创建分支：
  `lineage-23.2-302`
- [x] 拉取可连接到 `v4.4.177` 的 stable 历史。
- [x] 验证官方 merge base：
  `6b50202a4d53bf527c640467bcff68b50a5e38a2`
- [x] 试运行官方 stable 直接 merge，记录 92 个冲突后撤销。
- [x] 找到同源 universal7904 Android 4.4.302 merge：
  `b128d69019a8c1dd05b10b2879c9eaa0de3207c6`
- [x] 合并同源 4.4.302 bring-up，仅产生并解决 1 个 `rtmutex` 冲突。
- [x] 创建 merge commit：
  `228624978b`
- [x] 清理旧树 Mali r26p0 `Kbuild` 残留冲突标记：
  `4383ad3f2c`
- [ ] 运行 `wisdom_defconfig`、`olddefconfig` 和静态检查。
- [ ] 完成第一版 GCC 4.9 kernel 构建。
- [ ] 上机验证。
- [ ] 分批移植 Eureka。
- [ ] 验证 perfd 0.4.2。
- [ ] 替换 device prebuilt。

## 11. 参考

- Linux stable：<https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git>
- Linux v4.x 发布目录：<https://www.kernel.org/pub/linux/kernel/v4.x/>
- 当前 wisdom kernel：
  <https://github.com/xuanyayi/android_kernel_samsung_universal7904>
- 同源 universal7904 4.4.302：
  <https://github.com/SamarV-121/android_kernel_samsung_universal7904>
- Google Android 4.4 common：
  <https://android.googlesource.com/kernel/common/+/deprecated/android-4.4-p>
- Eureka R24U：
  <https://github.com/eurekadevelopment/Eureka-Kernel/tree/R24U>
- exynos7904-perfd：
  <https://github.com/xuanyayi/exynos7904-perfd>
