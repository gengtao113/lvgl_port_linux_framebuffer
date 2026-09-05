# LVGL 播放 GIF 动图落地步骤（susan-lu4esm-wallpaper）

> 日期：2026-09-04（素材路径 2026-09-05 更新）  
> 工程：`gengtao_linux_frambuffer_lvgl/lvgl_port_linux_framebuffer`  
> 素材（宿主机）：`lvgl_port_linux_framebuffer/GIF/susan-lu4esm-wallpaper-1492_512.gif`（文件名含 `_512`，按 **512×512** 规划内存）  
> 板上运行路径：`/root/susan-lu4esm-wallpaper-1492_512.gif`（`./copy_lvgl_build_file.sh` 会从工程 `GIF/` 一并拷到 NFS；也可手动按 G2）  
> 屏：1024×600 / 16bpp；触摸已通（本任务可不依赖触摸）  
> 前提：`./build_lvgl.sh` 能编出 `lvgl_build_output/demo`（CMake）  
> **本板已落地（2026-09-04）：** `LV_USE_GIF=1` + `FS_STDIO('A')` + `LV_MEM_CUSTOM`；`main.c` 居中播 GIF；NFS `/root/demo` + `/root/susan-….gif` 已部署。

**默认按路线 A 实施；下文保留完整清单便于复盘。**

---

## 目录

| 步 | 内容 |
|----|------|
| [总览](#总览) | 两条路线怎么选 |
| [G0](#g0-确认素材与内存预算) | 确认 GIF、算 RAM |
| [G1](#g1-打开-lvgl-的-gif--文件系统) | 改 `lv_conf.h` |
| [G2](#g2-把-gif-放到板上能读的路径) | NFS / 根文件系统 |
| [G3](#g3-改-mainc-创建-gif-控件) | 替换或旁路 widgets demo |
| [G4](#g4-重新交叉编译并部署) | `build_lvgl.sh` + 拷贝 |
| [G5](#g5-板上验收) | 出图、循环播放 |
| [附录](#附录) | C 数组方案 / 排错 / 居中缩放 |

---

## 总览

LVGL 8.2 已内置 GIF 解码（`LV_USE_GIF`，基于 gifdec）。播放动图本质是：

```text
打开 LV_USE_GIF
    ↓
准备 GIF 数据源（二选一）
    ├─ 路线 A：文件系统读 .gif 文件（Linux 推荐）
    └─ 路线 B：转成 C 数组编进固件（无 FS、体积大）
    ↓
lv_gif_create() + lv_gif_set_src()
    ↓
主循环继续 lv_timer_handler()（解码按帧推进）
```

| 路线 | 适用 | 优点 | 代价 |
|------|------|------|------|
| **A：文件** | 本工程（Linux + NFS） | 改图不用重编大数组；部署灵活 | 要开 `LV_USE_FS_STDIO`（或 POSIX），板上有文件 |
| **B：C 数组** | 无文件系统 / 想单文件交付 | 不依赖路径 | 需 LVGL Image Converter；`demo` 变大 |

**建议先走路线 A。** 下文 G1～G5 按 A 写；B 见附录。

---

## G0 确认素材与内存预算

### 0.1 确认文件

```bash
cd ~/linux-imx6ull/gengtao_linux_frambuffer_lvgl/lvgl_port_linux_framebuffer
ls -lh GIF/susan-lu4esm-wallpaper-1492_512.gif
file GIF/susan-lu4esm-wallpaper-1492_512.gif
# 可选：identify GIF/...  或 用 Python 读 GIF 头宽高
```

记下：**宽 W、高 H、文件体积**。下文按 **W=H=512** 举例；若实际不是 512，用真实宽高重算。

### 0.2 内存（重要）

官方公式（解码缓冲，与色深有关）：

| `LV_COLOR_DEPTH` | 约需 RAM |
|------------------|----------|
| 8 | `3 × W × H` |
| **16（本板）** | **`4 × W × H`** |
| 32 | `5 × W × H` |

512×512、16bpp：`4 × 512 × 512 ≈ 1 MiB`（仅 GIF 解码，不含界面其它对象）。

当前工程 `lv_conf.h` 里常见：

```c
#define LV_MEM_SIZE (2 * 1024U * 1024U)   /* 2 MiB 内置堆 */
```

2 MiB 对「GIF 1MiB + widgets/其它」往往偏紧。任选其一（推荐 ①）：

1. **Linux 推荐**：`LV_MEM_CUSTOM 1`，用系统 `malloc`/`free`（板子有充足用户态堆）  
2. 或把 `LV_MEM_SIZE` 提到 **≥ 4 MiB**（仍用内置堆）

G1 里一并改。

---

## G1 打开 LVGL 的 GIF + 文件系统

编辑：`lvgl_port_linux_framebuffer/lv_conf.h`

### 1.1 内存（推荐自定义堆）

找到 `LV_MEM_CUSTOM`，改为用系统堆，例如：

```c
#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM == 0
    /* …原 LV_MEM_SIZE 可保留不生效… */
#else
    #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
    #define LV_MEM_CUSTOM_ALLOC   malloc
    #define LV_MEM_CUSTOM_FREE    free
    #define LV_MEM_CUSTOM_REALLOC realloc
#endif
```

（若坚持内置堆：保持 `LV_MEM_CUSTOM 0`，把 `LV_MEM_SIZE` 改为 `4 * 1024U * 1024U`。）

### 1.2 打开 STDIO 文件系统

找到 `LV_USE_FS_STDIO`，改为类似：

```c
#define LV_USE_FS_STDIO 1
#if LV_USE_FS_STDIO
    #define LV_FS_STDIO_LETTER 'A'   /* 必须大写字母；与代码里 "A:..." 一致 */
    #define LV_FS_STDIO_PATH ""     /* 空：路径按绝对/相对原样拼；也可填前缀目录 */
    #define LV_FS_STDIO_CACHE_SIZE 0
#endif
```

含义：`lv_gif_set_src(obj, "A:/root/susan-....gif")` → 实际 `fopen("/root/susan-....gif", …)`。

### 1.3 打开 GIF

```c
#define LV_USE_GIF 1
```

### 1.4 自检

- `LV_COLOR_DEPTH` 仍为 **16**（与 fb 一致，勿为 GIF 单独改回 32）  
- 不需要为 GIF 单独开 `LV_USE_PNG` / `LV_USE_BMP`

改完先不要上板，继续 G2/G3 再编译。

---

## G2 把 GIF 放到板上能读的路径

宿主机素材放在工程内：

```text
~/linux-imx6ull/gengtao_linux_frambuffer_lvgl/lvgl_port_linux_framebuffer/GIF/susan-lu4esm-wallpaper-1492_512.gif
```

### 推荐：跟 demo 一起拷（脚本已支持）

```bash
cd ~/linux-imx6ull/gengtao_linux_frambuffer_lvgl/lvgl_port_linux_framebuffer
./copy_lvgl_build_file.sh
# 等价：把 lvgl_build_output/demo 与 GIF/*.gif 拷到 NFS rootfs/root/
# 只拷 demo：COPY_GIF=0 ./copy_lvgl_build_file.sh
```

### 或手动拷贝

```bash
PROJ=~/linux-imx6ull/gengtao_linux_frambuffer_lvgl/lvgl_port_linux_framebuffer
SRC="$PROJ/GIF/susan-lu4esm-wallpaper-1492_512.gif"
DEST=~/linux-imx6ull/nfs/rootfs/root/susan-lu4esm-wallpaper-1492_512.gif
cp -f "$SRC" "$DEST"
ls -lh "$DEST"
```

板上路径即为：`/root/susan-lu4esm-wallpaper-1492_512.gif`  
LVGL 源字符串写：`"A:/root/susan-lu4esm-wallpaper-1492_512.gif"`。

> 若你改了 `LV_FS_STDIO_PATH`（例如设成 `"/root/"`），则代码里应写成 `"A:susan-....gif"`（相对该前缀）。**路径规则要与 G1 配置一致。**  
> 说明：工程目录 `GIF/` 只是宿主机归档位置；运行时不直接从该目录读（除非你改 `GIF_PATH` / 把 `GIF/` 也挂进 rootfs）。
---

## G3 改 `main.c`：创建 GIF 控件

文件：`lvgl_port_linux_framebuffer/main.c`

### 3.1 思路

- 保留：`lv_init` / fbdev / evdev / `lv_timer_handler` 主循环  
- 可选：先注释掉 `lv_demo_widgets()`，避免占内存、挡画面  
- 新增：全屏或居中播放 GIF

### 3.2 最小示例（替换 demo）

在 `#include` 区可保留现有头文件；GIF 接口在 `lvgl.h` 开启 `LV_USE_GIF` 后可用。

在原 `lv_demo_widgets();` 处改为（示例）：

```c
/* 不再跑 widgets，专心播 GIF */
lv_obj_t * gif = lv_gif_create(lv_scr_act());
lv_gif_set_src(gif, "A:/root/susan-lu4esm-wallpaper-1492_512.gif");
lv_obj_center(gif);   /* 1024×600 上把 512×512 摆中间 */
```

需要重播时：`lv_gif_restart(gif);`。

### 3.3 可选：缩放铺满（按需）

`lv_gif` 基于 `lv_img`。若要把 512 放大贴近 600 高：

```c
/* 256 = 1.0 倍；512 = 2.0 倍。600/512≈1.17 → zoom ≈ 300 */
lv_img_set_zoom(gif, 300);
lv_obj_center(gif);
```

先 **zoom=256 原尺寸** 验收播放，再调缩放，便于区分「解码失败」和「缩放问题」。

### 3.4 光标

若仍创建鼠标光标图标，可能叠在 GIF 上；验收阶段可暂时不 `lv_indev_set_cursor`，减少干扰。

---

## G4 重新交叉编译并部署

```bash
cd ~/linux-imx6ull/gengtao_linux_frambuffer_lvgl/lvgl_port_linux_framebuffer

./build_lvgl.sh
./copy_lvgl_build_file.sh   # 同时拷 demo + GIF/*.gif → NFS /root/
```

确认：

```bash
file lvgl_build_output/demo          # ARM ELF
ls -lh ~/linux-imx6ull/nfs/rootfs/root/demo
ls -lh GIF/susan-lu4esm-wallpaper-1492_512.gif
ls -lh ~/linux-imx6ull/nfs/rootfs/root/susan-lu4esm-wallpaper-1492_512.gif
```

---

## G5 板上验收

```bash
# 板端（NFS 根文件系统已挂好时）
cd /root
ls -l demo susan-lu4esm-wallpaper-1492_512.gif
./demo
```

| 期望 | 说明 |
|------|------|
| 屏上出现动图 | 帧在切换（不是静止一帧） |
| 大致居中 | 用了 `lv_obj_center` |
| 循环播放 | gifdec/LVGL 默认按文件循环；异常见附录 |

结束：`Ctrl+C`（若未屏蔽信号）或杀进程。

---

## 附录

### A. 路线 B：转成 C 数组（不依赖文件）

1. 打开 [LVGL Image Converter](https://lvgl.io/tools/imageconverter)  
2. 上传工程内 `GIF/susan-lu4esm-wallpaper-1492_512.gif`  
3. Color format 选 **Raw**；Output 选 **C array**  
4. 得到 `xxx.c` / 声明，放入工程并加入 `CMakeLists.txt` 的 `demo` 源文件列表  
5. 代码：

```c
LV_IMG_DECLARE(susan_lu4esm_wallpaper_1492_512); /* 以转换器生成名为准 */
lv_obj_t * gif = lv_gif_create(lv_scr_act());
lv_gif_set_src(gif, &susan_lu4esm_wallpaper_1492_512);
lv_obj_center(gif);
```

可不启 `LV_USE_FS_STDIO`；`demo` 体积会明显变大。

### B. 排错

| 现象 | 排查 |
|------|------|
| 黑屏 / 无图 | `LV_USE_GIF` 是否为 1；是否仍被 `lv_demo_widgets` 盖住；路径是否带盘符 `A:` |
| 有控件无动画 | `lv_timer_handler` 是否在跑；tick（`custom_tick_get`）是否正常 |
| 打开失败 / 断言内存 | 走 G1 内存方案；`W×H` 是否远超 512 |
| `fopen` 失败 | 板上 `ls` 文件是否存在；`LV_FS_STDIO_LETTER` 与 `"A:"` 是否一致；`LV_FS_STDIO_PATH` 前缀是否重复/缺失 |
| 花屏色偏 | `LV_COLOR_DEPTH` 必须与 fb **16** 一致 |
| 编不过 | `./build_lvgl.sh clean` 后再编；确认 CMake 已链上 `lvgl`（GIF 源在 lvgl 库内，一般不用手加 `.c`） |

### C. 与现有文档关系

| 文档 | 关系 |
|------|------|
| **01** 移植落地 | fb / 触摸 / 首次出 `demo` |
| **02** Makefile→CMake | 怎么编 |
| **03（本文）** | 在已能跑 `demo` 的基础上播 GIF |

### D. 建议实施顺序（打勾用）

```text
[ ] G0 确认 GIF/ 下素材宽高与体积，算 4*W*H
[ ] G1 lv_conf：内存 + FS_STDIO('A') + LV_USE_GIF
[ ] G2 素材在工程 GIF/（部署用 ./copy_lvgl_build_file.sh 一并拷）
[ ] G3 main.c：lv_gif_create + set_src + center（先关 widgets）
[ ] G4 ./build_lvgl.sh && ./copy_lvgl_build_file.sh
[ ] G5 板上 ./demo 见循环动画
[ ] （可选）再调 zoom / 恢复光标或其它 UI
```
