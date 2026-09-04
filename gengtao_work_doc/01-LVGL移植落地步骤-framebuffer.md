# LVGL 移植落地步骤（Linux Framebuffer · ATK-MX6ULL）

> 日期：2026-09-04  
> 板卡：正点原子 ATK-MX6ULL（CORE + ALPHA）  
> 屏：本工程实测 **/dev/fb0 = 1024×600，16bpp**  
> 触摸：**/dev/input/event1**（`goodix-ts` 电容；文档 **21** L4）；`event0` 为电源键，勿用  
> 工程：`~/linux-imx6ull/gengtao_linux_frambuffer_lvgl/lv_port_linux_frame_buffer`  
> 工具链：`/usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin`  
> 关联 BSP：内核 LCD/fb 见 overlay 文档 **09**；电容触摸见 **21**/**22**/**23**

**本文只给实施清单，由你本地改配置与编译；不默认改仓库源码。**

---

## 目录

| 步 | 内容 |
|----|------|
| [L0](#l0-确认板上显示与触摸已通) | 确认 fb / 触摸（已测可勾） |
| [L1](#l1-确认工程与-submodule) | 工程与 submodule |
| [L2](#l2-按本板改三处配置) | 改分辨率 / 色深 / 触摸节点 |
| [L3](#l3-交叉编译) | 交叉编译出 `lvgl_build_output/demo` |
| [L4](#l4-部署到板子) | 拷贝到板子 |
| [L5](#l5-板上运行与验收) | 运行与验收 |
| [附录](#附录) | 排错 / 触摸校准 / 阅读对照 |

---

## 总览

```text
宿主机：改配置 → arm-linux-gnueabihf-gcc 编出 lvgl_build_output/demo
                ↓
板子：  /dev/fb0 (1024×600,16) ← fbdev_flush
        /dev/input/event1      ← evdev_read
                ↓
        ./demo → lv_demo_widgets 出图 + 可点
```

本 port **不写内核驱动**，只依赖用户态 framebuffer + evdev。

---

## L0 确认板上显示与触摸已通

在板子上执行（你已测过，可勾选）：

```bash
ls -l /dev/fb0
cat /sys/class/graphics/fb0/virtual_size    # 期望：1024,600
cat /sys/class/graphics/fb0/bits_per_pixel # 期望：16

# 可选：花屏即 fb 可写
# cat /dev/urandom > /dev/fb0

ls /dev/input/event*
cat /proc/bus/input/devices
# 期望：goodix-ts → Handlers=... event1
#       snvs-powerkey → event0（勿作触摸）
```

| 检查项 | 本板结果 | 勾选 |
|--------|----------|------|
| `/dev/fb0` 存在 | 有 | [x] |
| 分辨率 | 1024×600 | [x] |
| bpp | 16 | [x] |
| 触摸节点 | `goodix-ts` → `event1` | [x] |

**未过 L0 不要编 LVGL**——先回头打通 lcdif/mxsfb（文档 09）。

---

## L1 确认工程与 submodule

```bash
cd ~/linux-imx6ull/gengtao_linux_frambuffer_lvgl/lv_port_linux_frame_buffer

# 若 lvgl / lv_drivers 目录不完整
git submodule update --init --recursive

ls lvgl/lvgl.h lv_drivers/display/fbdev.c main.c Makefile lv_conf.h lv_drv_conf.h
```

| 文件 | 作用 |
|------|------|
| `main.c` | 注册 disp/indev，调 `lv_demo_widgets` |
| `lv_conf.h` | LVGL 内核配置（色深、demo 开关、内存） |
| `lv_drv_conf.h` | fbdev / evdev 路径 |
| `Makefile` | 编出 `lvgl_build_output/demo`（`.o` 同目录） |

默认已开：`USE_FBDEV=1`、`USE_EVDEV=1`、`LV_USE_DEMO_WIDGETS=1`。

---

## L2 按本板改三处配置

### 2.1 `main.c` — 分辨率

找到：

```c
disp_drv.hor_res    = 800;
disp_drv.ver_res    = 480;
```

改为（对齐 `virtual_size`）：

```c
disp_drv.hor_res    = 1024;
disp_drv.ver_res    = 600;
```

### 2.2 `lv_conf.h` — 色深

找到：

```c
#define LV_COLOR_DEPTH 32
```

改为（对齐 `bits_per_pixel`）：

```c
#define LV_COLOR_DEPTH 16
```

说明：与 fb 色深不一致时常见花屏/颜色错。本板为 16，必须改。

### 2.3 `lv_drv_conf.h` — 触摸设备（电容 goodix）

```c
#define EVDEV_NAME        "/dev/input/event1"   /* goodix-ts；先 cat /proc/bus/input/devices 核对 */
#define EVDEV_SWAP_AXES   0
#define EVDEV_CALIBRATE   0   /* 电容坐标已近像素；勿用电阻 TSC 的 0~4095 映射 */
```

> **本板已改（2026-09-04）：** `EVDEV_NAME=event1`，`EVDEV_CALIBRATE=0`；已 `./build_lvgl && ./copy_lvgl_build_file.sh` → NFS `/root/demo`。

若光标整体偏移，再按附录 B 临时开校准并改 MIN/MAX（不要照搬电阻 4095）。

### 2.4 本步自检

| 项 | 期望 |
|----|------|
| `main.c` | 1024×600 |
| `LV_COLOR_DEPTH` | 16 |
| `EVDEV_NAME` | `/dev/input/event1`（goodix-ts） |
| `EVDEV_CALIBRATE` | `0`（电容） |

---

## L3 交叉编译

可以用脚本（推荐），也可手敲命令。

### 方式 A：脚本（推荐）

脚本在工程目录：`lv_port_linux_frame_buffer/build_lvgl`（传入 `CC=` / `BUILD_DIR=`，并用 **GCC 4.9 兼容 CFLAGS** 覆盖官方偏新的 `-W*`）。**产物统一在 `lvgl_build_output/`**（`.o` + `demo`）。

```bash
cd ~/linux-imx6ull/gengtao_linux_frambuffer_lvgl/lv_port_linux_frame_buffer
chmod +x build_lvgl
./build_lvgl
```

可选：

```bash
./build_lvgl clean                          # 只 clean
JOBS=4 ./build_lvgl                         # 指定并行数
TOOLCHAIN_BIN=/你的工具链/bin ./build_lvgl  # 工具链不在默认路径时
```

脚本会：`make clean` → `make CC=… CFLAGS=… BUILD_DIR=lvgl_build_output -j…` → `file lvgl_build_output/demo` 检查是否为 **ARM**。

### 方式 B：手动命令

```bash
cd ~/linux-imx6ull/gengtao_linux_frambuffer_lvgl/lv_port_linux_frame_buffer

export PATH=/usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin:$PATH
which arm-linux-gnueabihf-gcc   # 应能找到

# 注意：必须带兼容 CFLAGS，否则 Linaro 4.9 会报 -Wshift-negative-value
CFLAGS_ARM='-std=gnu99 -O3 -g0 -I'"$(pwd)"'/ -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-unused-function -fno-strict-aliasing'

make clean
make CC=arm-linux-gnueabihf-gcc CFLAGS="$CFLAGS_ARM" BUILD_DIR=lvgl_build_output -j$(nproc)

file lvgl_build_output/demo
# 期望：ELF 32-bit LSB executable, ARM, ...
ls -lh lvgl_build_output/demo
```

| 现象 | 处理 |
|------|------|
| `arm-linux-gnueabihf-gcc: not found` | 检查 PATH / `TOOLCHAIN_BIN` |
| 缺头文件 / submodule | 重做 L1 `git submodule update --init --recursive` |
| 编出 x86 的 `demo` | 未用交叉 `CC=`；用脚本可自动检查 |

**通过判据：** `file lvgl_build_output/demo` 显示 **ARM**，非 x86-64。

---

## L4 部署到板子

任选一种（按你现有 NFS/串口环境）：

```bash
# 例：scp（产物在 lvgl_build_output/）
scp lvgl_build_output/demo root@<板子IP>:/root/

# 或拷到 NFS rootfs 再启动/同步
# cp lvgl_build_output/demo /path/to/nfsroot/root/
```

板上：

```bash
chmod +x /root/demo   # 或你放置的路径
```

---

## L5 板上运行与验收

```bash
cd /root   # 或 demo 所在目录
./demo
```

### 5.1 显示验收

| 期望 | 说明 |
|------|------|
| 屏上出现 widgets demo（按钮/滑条等） | LVGL + fb0 通 |
| 无明显花屏、色带错乱 | 色深 16 已对齐 |
| 画面铺满或按 1024×600 布局合理 | 分辨率已对齐 |

### 5.2 触摸验收

| 期望 | 说明 |
|------|------|
| 手指点击能点中控件 | `event1` 正确 |
| 光标（若有）跟随手指 | indev 正常 |

若显示正常但触摸反了/偏移 → 见 [附录 B](#附录-b触摸轴反了或不准)。

### 5.3 结束进程

串口 `Ctrl+C` 结束 `./demo`。

---

## 附录

### 附录 A 排错速查

| 现象 | 先查 |
|------|------|
| 打不开 fb / 黑屏 | `/dev/fb0`、背光、文档 09 |
| 花屏、颜色怪 | `LV_COLOR_DEPTH` 是否为 **16** |
| 只显示一角 / 错位 | `hor_res/ver_res` 是否为 **1024×600** |
| 能显示不能点 | `EVDEV_NAME` 是否为 **event1**（不是 event0） |
| `Illegal instruction` | 是否 ARM 交叉编译 |
| 编译通过板上不跑 | `file lvgl_build_output/demo`、动态库/ABI、权限 `chmod +x` |

### 附录 B 触摸轴反了或不准

> **通路：** ATK-7RGBLCD 电容触摸 = **I2C2 + gt9xx** → `Name="goodix-ts"` → 本板 **`/dev/input/event1`**（文档 **21**）。  
> **默认：** `EVDEV_CALIBRATE 0`，直接用 ABS_MT 坐标当像素。

若光标偏移再临时：

1. `EVDEV_CALIBRATE 1`，用 `hexdump -C /dev/input/event1` 摸四角填 `EVDEV_HOR/VER_MIN/MAX`  
2. **不要**再填电阻 TSC 的 0～4095（那是 ADC 量程）  
3. 轴反了再试 `EVDEV_SWAP_AXES 1`  

改完后重新 `./build_lvgl && ./copy_lvgl_build_file.sh` 上板验证。

正式电容方案见：`linux-kernel-overlay/gengtao-bsp-doc/21-电容触摸打通-分析思路与落地顺序.md`。

### 附录 C 可选：暂不启用触摸

只验证出图时，可临时注释 `main.c` 里 `evdev_init()` 及后续 indev/光标相关代码，先确认 fb；确认后再恢复触摸。

### 附录 D 架构对照（排错用）

```text
LCD 硬件 → mxsfb → /dev/fb0 → fbdev_init/flush → LVGL disp
触摸 goodix-ts → input → /dev/input/event1 → evdev_read → LVGL indev
```

### 附录 E 实施勾选表

- [ ] L0 fb/触摸确认  
- [ ] L1 submodule 齐全  
- [x] L2 三处配置已改  
- [ ] L3 `file lvgl_build_output/demo` 为 ARM  
- [ ] L4 已部署  
- [ ] L5 出图  
- [ ] L5 触摸可用（或已记校准待办）  

---

## 一句话

**先确认 1024×600@16 + event1，再改 `main.c` / `lv_conf.h` / `lv_drv_conf.h`，用 Linaro `arm-linux-gnueabihf-gcc` 编出 `lvgl_build_output/demo` 上板运行；显示与触摸分层验收。**
