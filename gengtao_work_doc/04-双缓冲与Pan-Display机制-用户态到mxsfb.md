# LVGL framebuffer 双缓冲 + Pan Display 机制说明

> 日期：2026-09-05  
> 工程：`gengtao_linux_frambuffer_lvgl/lvgl_port_linux_framebuffer`  
> 用户态：`lv_drivers/display/fbdev.c` / `fbdev.h`，`main.c`（整屏双 draw buffer + `full_refresh`）  
> 内核：`drivers/video/fbdev/core/fbmem.c` + `drivers/video/fbdev/mxsfb.c`（i.MX LCDIF / mxsfb）  
> 关联：GIF 播放见 **03**；构建见 **02**

---

## 目录

| 节 | 内容 |
|----|------|
| [0. 术语：pan 的中文业务含义](#0-术语pan-的中文业务含义) | 业务话怎么说 |
| [1. 要解决什么问题](#1-要解决什么问题) | 单缓冲边画边显的痛点 |
| [2. 当前实现概要](#2-当前实现概要) | 用户态做了什么 |
| [3. 是否等于 ping-pong](#3-是否等于-ping-pong) | 概念对应 |
| [4. 时序与会不会踩同一页](#4-时序与会不会踩同一页) | A/B 切换、CPU 慢会怎样 |
| [5. 调用链总览](#5-调用链总览) | 应用到内核到硬件 |
| [6. 用户态细节](#6-用户态细节) | `fbdev_init` / `fbdev_flush` / 统计 |
| [7. 内核 fb 通用层](#7-内核-fb-通用层) | `FBIOPAN_DISPLAY` → `fb_pan_display` |
| [8. mxsfb pan 实现](#8-mxsfb-pan-实现) | `NEXT_BUF`、帧完成中断、阻塞返回 |
| [9. 和帧率的关系](#9-和帧率的关系) | pan 不提速 |
| [10. 文件与验收要点](#10-文件与验收要点) | 改了哪些、板上怎么看 |

---

## 0. 术语：pan 的中文业务含义

英文 **pan / pan display**（ioctl：`FBIOPAN_DISPLAY`）在本工程里的业务说法：

| 英文 | 推荐中文（业务） | 一句话 |
|------|------------------|--------|
| **pan display** | **显示翻页** / **切换显示页** | 告诉 LCD：「下一帧请去扫另一块已经画好的显存」 |
| **pan**（动词） | **翻页**、**切页** | 把「正在给屏幕看的那一页」换成另一页 |
| **page flip** | **页翻转** | 与 pan 同义场景：双页之间切换给屏幕看的那一页 |
| **yoffset** | **显示起始行偏移** | 选上半页还是下半页（页 A / 页 B） |

### 业务场景怎么讲（对产品/联调）

> 显存里准备了**两整屏画面**（上下两页）。  
> UI/动画在**背面那一页**慢慢画完；画完后做一次 **显示翻页（pan）**，屏幕立刻改去看画好的那一页。  
> 原来那一页腾出来给下一帧继续画。  
> 这样屏幕始终在看「完整的一帧」，而不是边画边看半成品，从而减轻**画面撕裂、半新半旧**。

### 和相近说法的边界

| 说法 | 是否等于本工程的 pan | 说明 |
|------|----------------------|------|
| 刷屏 / flush | 否 | flush 是把像素**写进**某页显存；pan 是**改 LCD 去看哪一页** |
| 拷屏 / memcpy | 否 | 拷屏是填后台页的内容；pan 不拷像素，只改扫描起点 |
| 双缓冲 | 相关但不等同 | 双缓冲 = 有两页可轮流用；**pan = 决定哪一页给屏幕看** |
| DMA | 否 | 本工程 pan 不搬数据；DMA 若有，多用于加速拷屏 |

### 叠字里的 `dblbuf+pan`

- **dblbuf（双缓冲）**：有两页显存可轮流画/显  
- **pan（显示翻页）**：每帧画完后切换「屏幕看哪一页」  

合起来：**双缓冲 + 显示翻页**。

---

## 1. 要解决什么问题

官方默认 `fbdev_flush` 是**单缓冲**：`mmap` 一页 framebuffer，LVGL 用 `memcpy` 往**正在给 LCD 扫的那块内存**写像素。

动画（如 GIF）时常见问题：

- **撕裂 / 半新半旧**：扫描还在读这一页，CPU 又在改同一页  
- **难观察**：不清楚实际 FPS、脏区大小、是否已启用双缓冲  

本次改动目标：

1. **显示完整性**：画和显分开（双页 + **显示翻页/pan**）  
2. **可观测**：叠字显示 FPS、模式（`dblbuf+pan` / `single`）、脏区尺寸  

> 注意：双缓冲 + **显示翻页（pan）** **主要解决撕裂与完整性**，**不负责提高帧率**。

---

## 2. 当前实现概要

| 层级 | 做法 |
|------|------|
| `fbdev_init` | `yres_virtual = 2 × yres`（`FBIOPUT_VSCREENINFO`），`mmap` 两页；失败则回退单缓冲 |
| `fbdev_flush` | 像素只写入**后台页**；本帧最后一块 flush 时 `ioctl(FBIOPAN_DISPLAY)` 翻页 |
| `main.c` | LVGL 侧两块整屏 draw buffer；`full_refresh = 1`，保证翻页前后台页是完整一帧 |
| 统计 | `fbdev_get_perf()` / `fbdev_dblbuf_enabled()` 供左上角 label 显示 |

数据路径（概念）：

```text
LVGL 画完一帧
  → memcpy 到后台页（framebuffer 虚拟高度的另一半）
  → FBIOPAN_DISPLAY（内核 mxsfb 在帧边界切换扫描）
  → 下一帧画到另一页
```

---

## 3. 是否等于 ping-pong

**可以理解为典型的 ping-pong（乒乓）缓冲**，显示侧常叫 **page flip / 双页翻页**。

```text
时刻 N：  前台页 A 正在显示  |  后台页 B 被画满 → pan 切到 B
时刻 N+1：前台页 B 正在显示  |  后台页 A 被画下一帧 → pan 切到 A
……
```

两页角色每帧互换：一边给 LCD 扫，一边给 CPU 写。

与「GPU 交换链直接渲染到前/后台」相比，本工程多一步：LVGL 先在自己的 draw buffer 里画，再 **`memcpy` 进后台页**，然后 pan。本质仍是两页轮流当前后台，中间多一次拷贝。

---

## 4. 时序与会不会踩同一页

### 4.1 约定

```text
前台 = 正在给 LCD 扫的页（s_front_page）
后台 = 1 - s_front_page

每一块 flush：只 memcpy → 后台页
本帧最后一块（lv_disp_flush_is_last）：
  后台已是完整一帧 → FBIOPAN → 交换 s_front_page
```

**正常工作时，不会出现「LCD 正在扫的那一页，CPU 还在往里慢慢写」。**

### 4.2 CPU 写得慢会怎样

| 情况 | 结果 |
|------|------|
| CPU 画一帧很慢（例如 ~6fps） | LCD 一直扫上一帧已 pan 出去的前台页；画面停在上一完整帧，等画完再翻 |
| CPU 写后台、LCD 扫前台 | 两页物理分离，不冲突 |
| 旧单缓冲边扫边写同一页 | 会撕裂——正是改 ping-pong 要避免的 |

慢只影响**帧率**，不破坏「显一页、画另一页」的隔离。

### 4.3 时序简图

```text
时间 →

LCD:  [==== 扫 A ====][==== 扫 A ====][==== 扫 B ====][==== 扫 B ====]
CPU:  [---- 写 B ----]  pan→B          [---- 写 A ----]  pan→A
                 ↑ 整帧写完才翻              ↑
```

A/B 切换的同步点是：**整帧写完 + `FBIOPAN_DISPLAY`（内核里再对齐硬件帧边界）**，不是边写边翻。

### 4.4 前台扫描 vs 后台绘制：哪个更耗时

关键认知：这两件事是**并行发生**的（不是先后），所以「哪个更短」等价于问「**谁是瓶颈**」——帧率由两者中较慢的那个决定。

| | 前台 A「正在显示」 | 后台 B「被画满」 |
|------|------------------|----------------|
| 本质 | LCD 硬件扫描一整页 | CPU 软渲 + memcpy 填一整页 |
| 决定因素 | 屏幕刷新率（pixclock/时序），**固定** | A7 算力 + 脏区大小，**可变** |
| 代号 | `T_scan` | `T_render` |
| 量级（本板） | 60Hz ≈ **16.7ms** | 6fps ≈ **167ms** |

**三种判定方法（从快到细）**

1. **最快：看 FPS 是否顶到刷新率**（叠字已显示 FPS，零成本）
   - `FPS ≈ 刷新率` → `T_render ≤ T_scan`（后台画得比前台扫得快，即「画得比看得快」），**LCD 是瓶颈**，CPU 在等下一次翻页
   - `FPS < 刷新率` → `T_render > T_scan`，**CPU 是瓶颈**，LCD 空扫同一页干等
2. **量 `T_scan`（硬件常数）**，三种拿法：
   - `FBIOGET_VSCREENINFO` 的 `pixclock` + `left/right_margin` + `hsync_len` + `upper/lower_margin` + `vsync_len` 套标准公式算一帧时间
   - 内核 [mxsfb.c](drivers/video/fbdev/mxsfb.c) 的 `CUR_FRAME_DONE` 中断里打两次时间戳，间隔即 `T_scan`
   - 连调两次 `MXCFB_WAIT_FOR_VSYNC`，时间差即 `T_scan`
3. **量 `T_render`（用户态打点）**，在 `fbdev_flush` 里：
   - 本帧**第一块** flush 进来时记 `t0`
   - 本帧**最后一块** memcpy 结束、**发 pan 之前**记 `t1`
   - `T_render = t1 - t0`（软渲 + memcpy，不含等翻页）

> ⚠️ 别用「pan ioctl 的返回时间」当渲染时间——它阻塞到当前前台页扫完（见 8.1），混进了 `T_scan` 的尾巴，量出来会偏大。

**本板 6fps 结论**：`T_render ≈ 167ms` 远大于 `T_scan ≈ 16.7ms`，即

> **后台画满（整屏软渲 + 整屏 memcpy + GIF 软解）才是瓶颈，前台扫描几乎不耗时、全程在等 CPU。**

与第 9 节一致：抬帧率应去砍 `T_render`（局部脏区、缩小 GIF、上 PXP/G2D 拷屏），而非动 `T_scan`。

---

## 5. 调用链总览

```text
应用 fbdev.c
  ioctl(FBIOPAN_DISPLAY, &vinfo)     // yoffset = 0 或 yres
        │
        ▼
drivers/video/fbdev/core/fbmem.c
  case FBIOPAN_DISPLAY:
    → fb_pan_display(info, &var)
        │  校验 yoffset / ypanstep / yres_virtual
        ▼
drivers/video/fbdev/mxsfb.c
  mxsfb_ops.fb_pan_display = mxsfb_pan_display
        │
        ▼
写 LCDIF NEXT_BUF = smem_start + line_length * yoffset
开 CUR_FRAME_DONE 中断并 wait_for_completion
        │
        ▼
本帧扫完 → IRQ complete → ioctl 返回用户态
应用更新 s_front_page
```

**谁控制「何时切换」：** 应用只提「下一页是谁」；**真正改扫描、对齐场/帧，是内核 mxsfb + LCDIF。**

---

## 6. 用户态细节

### 6.1 初始化（申请双页）

`fbdev_init` 中（Linux 路径）：

1. `FBIOGET_VSCREENINFO` / `FSCREENINFO`  
2. `yres_virtual = yres * 2`，`FBIOPUT_VSCREENINFO`  
3. 再读回；若 `yres_virtual >= 2*yres` 则 `s_dblbuf = 1`，否则回退单缓冲  
4. `mmap` 整段 `smem_len`，`memset` 清两页  

### 6.2 flush（写后台 + 最后一块 pan）

- 计算 `back_page = 1 - s_front_page`，`y_base = back_page * yres`  
- 按 bpp 把脏区 `memcpy` 到 `fbp` 上对应行（叠加 `y_base`）  
- `lv_disp_flush_is_last(drv)` 为真时：  
  - `vinfo.yoffset = back_page * yres`  
  - `ioctl(FBIOPAN_DISPLAY, &vinfo)`（**阻塞直到内核翻页完成或超时**）  
  - 成功则 `s_front_page = back_page`  

### 6.3 对外接口（`fbdev.h`）

| API / 结构 | 作用 |
|------------|------|
| `fbdev_perf_t` | fps、最近脏区宽高、每帧 flush 次数、是否曾整屏脏区、mode_partial |
| `fbdev_get_perf()` | 给 UI 叠字 |
| `fbdev_dblbuf_enabled()` | 1：已启用 yres_virtual×2 + pan |

叠字示例：`FPS:6  mode:dblbuf+pan  last:1024x600  flushes/f:1`

### 6.4 FBIOPAN_DISPLAY 用法与注意事项（应用侧）

**前置准备（一次性）**

1. 申请双页：`vinfo.yres_virtual = 2 * yres` → `ioctl(FBIOPUT_VSCREENINFO)`，读回确认 `yres_virtual >= 2*yres`。
2. `mmap` 整段 `smem_len`（两页连续），`memset` 清两页。

**每帧翻页（flush 最后一块时）**

```c
vinfo.yoffset = back_page * yres;   // 目标页起始行：0 或 yres
vinfo.xoffset = 0;                  // mxsfb 不支持 x pan
if (ioctl(fd, FBIOPAN_DISPLAY, &vinfo) == 0)
    s_front_page = back_page;       // 成功后才更新前后台状态
```

阻塞语义：ioctl 返回时内核已在帧边界完成翻页（或超时），应用不用自己猜「切完没」。

**注意事项**

| # | 事项 | 说明 |
|---|------|------|
| 1 | 时机 | 整帧画完（`lv_disp_flush_is_last`）才 pan，否则翻过去是半成品 → 撕裂 |
| 2 | yoffset 合法 | `yoffset + yres ≤ yres_virtual`，且页对齐（本工程只取 `0` / `yres`） |
| 3 | xoffset=0 | mxsfb 不支持 x panning，`>0` 直接失败 |
| 4 | 阻塞 | 睡到当前帧扫完，最长 `HZ/2`(0.5s)；别在中断上下文调用；超时串口打 `mxs wait for pan flip timeout` |
| 5 | 查返回值 | 失败（越界 `-EINVAL`、blank `-EBUSY`）时**不要**更新 `s_front_page`，否则记录与硬件错位 |
| 6 | blank 禁止 | blank 时 mxsfb 拒绝 pan |
| 7 | 不提帧率 | 翻页不拷像素、不改扫描时序；切换时刻由内核 mxsfb + LCDIF 对齐 |
| 8 | 后台已写完 | pan 前确认后台页 memcpy 完成，否则读到脏/旧数据 |
| 9 | 应用只提「下一页是谁」 | 维护 `s_front_page`，扫描时序交给驱动 |

**一句话**：应用侧只做「选好下一页 + 整帧画完后发一次 `FBIOPAN_DISPLAY` + 成功才更新 `s_front_page`」；参数校验、帧边界对齐、阻塞等待全在内核 mxsfb。

---

## 7. 内核 fb 通用层

路径：`drivers/video/fbdev/core/fbmem.c`

```c
case FBIOPAN_DISPLAY:
    /* copy_from_user var */
    ret = fb_pan_display(info, &var);
```

`fb_pan_display()` 主要：

1. 检查 `ypanstep`、`yoffset` 是否合法（`yoffset ≤ yres_virtual - yres`）  
2. 调用 `info->fbops->fb_pan_display(var, info)`  
3. 成功后写回 `info->var.xoffset / yoffset`  

对本板，`fbops` 指向 **mxsfb**。

---

## 8. mxsfb pan 实现

路径：`drivers/video/fbdev/mxsfb.c`  
硬件：i.MX LCDIF；寄存器含 `CUR_BUF` / `NEXT_BUF`（v3/v4 偏移不同）。

### 8.1 `mxsfb_pan_display`（核心）

要点（与树内代码一致）：

1. blank 时不允许 pan  
2. **不支持 x panning**（`xoffset > 0` 直接失败）  
3. 校验 `yoffset + yres ≤ yres_virtual`  
4. `offset = line_length * yoffset`  
5. **`writel(smem_start + offset, NEXT_BUF)`** —— 注释：`update on next VSYNC`  
6. 使能 `CTRL1_CUR_FRAME_DONE_IRQ`  
7. **`wait_for_completion_timeout(flip_complete, HZ/2)`** —— 进程睡眠直到帧完成 IRQ  

含义：

- 写的是 **NEXT_BUF**（下一帧用的扫描缓冲），不是当场打断当前行扫描  
- LCDIF 在**帧边界**把下一缓冲装入当前扫描路径  
- 驱动用 **当前帧完成中断** 唤醒等待中的 `ioctl`，而不是让应用自己猜何时切完  

### 8.2 中断里谁 `complete`

同一文件 IRQ 处理中：

- `CTRL1_CUR_FRAME_DONE_IRQ` → `complete(&host->flip_complete)`（**pan 路径用这个**）  
- `CTRL1_VSYNC_EDGE_IRQ` → `complete(&host->vsync_complete)`（给 `MXCFB_WAIT_FOR_VSYNC` / `mxsfb_wait_for_vsync` 用）  

两条都是场/帧相关同步；**本工程 pan 走的是 FRAME_DONE，不是单独再调 WAIT_FOR_VSYNC ioctl。**

### 8.3 和用户态时序拼在一起

```text
T0  LCD 扫页 A；CPU memcpy 写页 B
T1  整帧写完 B；ioctl(PAN)，yoffset→B
T2  内核写 NEXT_BUF=页B；进程 sleep
T3  本帧 A 扫完 → CUR_FRAME_DONE IRQ → complete
T4  硬件按新缓冲扫 B；ioctl 返回
T5  应用 s_front_page=B；下一帧 CPU 写 A
```

| 层级 | 职责 |
|------|------|
| LVGL / fbdev.c | 画后台、设 `yoffset`、发 `FBIOPAN_DISPLAY` |
| fbmem | 参数检查、转到驱动 |
| **mxsfb（内核）** | 写 `NEXT_BUF`、等帧完成、与扫描同步 |
| LCDIF 硬件 | 帧边界真正切换扫描缓冲 |

---

## 9. 和帧率的关系

当前常见约 **6fps** 时：

- 叠字若为 `mode:dblbuf+pan` 且 `last` 接近整屏 → pan 路径在工作  
- 慢的主因通常是：`full_refresh` 整屏软渲 + 整屏 `memcpy` + GIF 软解，A7 算力不足  
- **`LV_DISP_DEF_REFR_PERIOD=30`（理论上限 ~33fps）不是瓶颈**；单帧耗时已到百毫秒级  

若要抬帧率：优先考虑局部脏区、缩小 GIF、或后续 PXP/G2D 拷屏——与「是否 pan」正交。

---

## 10. 文件与验收要点

### 涉及文件

| 文件 | 变更要点 |
|------|----------|
| `lv_drivers/display/fbdev.c` | 双页 init、后台写、`FBIOPAN_DISPLAY`、perf 统计 |
| `lv_drivers/display/fbdev.h` | `fbdev_perf_t`、`fbdev_get_perf`、`fbdev_dblbuf_enabled` |
| `main.c` | 整屏双 draw buffer、`full_refresh=1`、叠字显示 mode/FPS |

### 板上快速验收

```bash
cd /root && ./demo
```

| 现象 | 含义 |
|------|------|
| `mode:dblbuf+pan` | `yres_virtual×2` 与 pan 已启用 |
| `mode:single` | `FBIOPUT` 双倍虚拟高失败或驱动不支持，已回退 |
| GIF 比单缓冲更少「撕开感」 | pan 目标达成（帧率仍可能不高） |

### 内核排查（可选）

- 串口是否有 `mxs wait for pan flip timeout`  
- `/sys/class/graphics/fb0/virtual_size`、`yres_virtual` 是否 ≥ `2 * yres`  
- 确认在用 `mxsfb`：`dmesg | grep -i mxs` / `fb0`

---

## 附录：一句话结论

> **显示翻页（pan）的切换时刻由内核 mxsfb 按 LCDIF 帧完成来控制；应用只负责选下一页，并在整帧写完后发起 `FBIOPAN_DISPLAY`。**  
> 这就是 ping-pong 能与 LCD 扫线对齐、且 CPU 慢也不会写穿正在显示那一页的原因。
