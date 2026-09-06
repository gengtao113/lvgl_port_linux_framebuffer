# 09 中文空白 Debug 复盘：压缩字库 + `LV_USE_FONT_COMPRESSED`

> 目标工程：`lvgl_port_linux_framebuffer`
> 触发问题：步 4 加了中文 label 后上板，「完全空白、连方框都没有」，但 GIF 和 FPS 都正常
> 日期：2026-09-06
> 定位：这是**一次完整 bug 排查复盘**，记录「现象 → 逐层排除 → 锁定根因 → 修复 → 引出新问题」的全过程。与之配套：[06 操作步骤](06-移植OPPOSans中文字库-离线转C字库.md)、[08 端到端步骤](08-OPPOSans字库移植-端到端步骤与逐步验证.md)、[07 移植复盘](07-字库移植复盘-从TTF到C字库的处理过程.md)。

---

## 一、问题现象与结论（先看结果）

**现象**：把 `oppo_sans_20`（bpp=4）设到中文 label 上，上板后屏幕**底部完全空白**——不是方框、不是乱码，是「什么都没画」。同屏左上角 FPS（内置 `montserrat_14`）、居中 GIF 都正常。

**结论**：字库 `oppo_sans_20.c` 是 **RLE 压缩**格式（`bitmap_format = 1`），而 `lv_conf.h` 里 **`LV_USE_FONT_COMPRESSED = 0`**（禁用了压缩字体的解压）。LVGL 渲染压缩字库时走 `#else` 分支直接 `return NULL`，一个字都不画 → 空白。

**修复**：`lv_conf.h` 里 `LV_USE_FONT_COMPRESSED 0 → 1`，重新交叉编译。

---

## 二、为什么「FPS 正常、中文空白」恰好对得上（核心）

这是本次排查最关键的观察。两个 label 用的是两种字体，走的是**两条完全不同的渲染分支**：

| label | 字体 | 位图格式 | 渲染分支 | 结果 |
|-------|------|---------|---------|------|
| FPS（左上角） | 内置 `montserrat_14` | `PLAIN`（未压缩，`bitmap_format=0`） | 直接返回位图指针 | 正常显示 ✅ |
| 中文（底部） | `oppo_sans_20` | `COMPRESSED`（RLE 压缩，`bitmap_format=1`） | 需解压；但 `LV_USE_FONT_COMPRESSED=0` → `return NULL` | 空白 ❌ |

`lv_font_get_bitmap_fmt_txt()` 里的分叉（[lv_font_fmt_txt.c:92-140](../../lvgl/src/font/lv_font_fmt_txt.c)）：

```c
if(fdsc->bitmap_format == LV_FONT_FMT_TXT_PLAIN) {
    return &fdsc->glyph_bitmap[gdsc->bitmap_index];   /* 未压缩：直接返回 */
}
else {  /* 压缩位图 */
#if LV_USE_FONT_COMPRESSED
    ... decompress(...);                               /* 解压后返回 */
#else
    LV_LOG_WARN("Compressed fonts ... LV_USE_FONT_COMPRESSED is not enabled");
    return NULL;                                       /* ← 关了解压支持，直接返回空 */
#endif
}
```

**要点**：`bitmap_format` 是**字库数据自己声明的**（lv_font_conv 转换时写死在 `.c` 里），而 `LV_USE_FONT_COMPRESSED` 是**工程编译时**的开关。两者对不上时，不会报编译错误、不会报链接错误——它只在**运行时**静默地「不画」，非常隐蔽。

---

## 三、排查路径（时间顺序，我实际做了什么）

```
① 先排除「编译/数据/配置」层（这是最快的，全都能离线查）
   → 字库 21MB 存在、变量名 oppo_sans_20 ✓
   → LV_FONT_FMT_TXT_LARGE = 1 ✓
   → LV_TXT_ENC = UTF8 ✓
   → main.c 是 UTF-8、中文字节正确 ✓
   → demo 是 ARM ELF、含「你好」字符串、含 oppo_sans_20 符号 ✓
   → 字体位图 3.3MB 在 demo 的 .rodata 段 ✓
   → 字体元数据 line_height=23 / base_line=5 正常 ✓
   → 「你」(20320)「好」(22909) 都在 cmap 覆盖区间内 ✓
   → 分辨率 1024×600、label 位置在屏内不被 GIF 遮挡 ✓
   → 结论：编译/数据/配置全对，问题在「运行时渲染」

② 用提问锁定现象（关键一步，避免瞎猜）
   → 问「GIF/FPS 正常吗」→ 正常（说明 label 机制、fbdev、GIF 都没问题）
   → 问「中文位置看到什么」→ 完全空白、连方框都没有
   → 这条信息很值钱：方框 = 缺字形；乱码 = 编码错；空白 = 渲染返回空/透明
   → 排除「缺字形」「编码错」，指向「字体渲染根本没出像素」

③ 找「两个 label 的差异」→ 字体位图格式不同
   → FPS 用内置 montserrat_14（PLAIN 未压缩）
   → 中文用 lv_font_conv 生成的 oppo_sans_20（bpp=4，默认 RLE 压缩）
   → 差异锁定在「压缩 vs 未压缩」

④ 读 LVGL 渲染源码，找到 return NULL 的分支
   → lv_font_fmt_txt.c 的 lv_font_get_bitmap_fmt_txt()
   → 看到 #if LV_USE_FONT_COMPRESSED ... #else return NULL

⑤ 核对两个字库事实，根因闭合
   → 字库 .c 的 font_dsc：.bitmap_format = 1（= LV_FONT_FMT_TXT_COMPRESSED）
   → lv_conf.h：LV_USE_FONT_COMPRESSED = 0
   → 两个条件同时满足 → 必空白

⑥ 修复 + 编译验证
   → LV_USE_FONT_COMPRESSED 0→1
   → 重编通过，demo 里出现 _lv_font_decompr_buf 符号（解压缓冲被编入 = 开关生效）
```

---

## 四、查证的关键事实清单

真正让根因闭合的，是下面这些**被主动查证**的事实，缺一个都定位不到：

1. **字库 `.bitmap_format = 1`**（`oppo_sans_20.c` 第 455466 行）→ 字库是压缩的，不是猜测。
2. **`LV_USE_FONT_COMPRESSED = 0`**（`lv_conf.h` 第 375 行）→ 工程禁用了压缩解压。
3. **枚举值**（`lv_font_fmt_txt.h:149-151`）：`LV_FONT_FMT_TXT_PLAIN=0`、`LV_FONT_FMT_TXT_COMPRESSED=1` → 确认 `bitmap_format=1` 就是「压缩」。
4. **渲染分支**（`lv_font_fmt_txt.c:92-140`）：压缩位图在 `LV_USE_FONT_COMPRESSED=0` 时 `return NULL`。
5. **现象区分**（用户答）：FPS 正常 + 中文**空白（非方框）** → 排除了缺字形和编码两条路，直指「渲染没出像素」。

> 教训：**「空白」和「方框」是两个完全不同的信号**。方框是「字体加载了但没这个字形」，空白是「字体位图没被画出来」。排查前先问清是哪个，能省掉一大半弯路。

---

## 五、根因链条（一图看懂）

```
lv_font_conv 转字库（默认 RLE 压缩）
        │
        ▼
oppo_sans_20.c 里 .bitmap_format = 1（压缩）
        │
        │    lv_conf.h 里 LV_USE_FONT_COMPRESSED = 0（关了解压）
        ▼
渲染时 lv_font_get_bitmap_fmt_txt()
        │
        ▼
bitmap_format != PLAIN → 进压缩分支 → #else → return NULL
        │
        ▼
字没画出来 = 完全空白（连方框都没有）
```

**两个开关的对比**（这次一共踩了两个坑，都要开）：

| 开关 | 作用 | 不开的后果 |
|------|------|-----------|
| `LV_FONT_FMT_TXT_LARGE` | `bitmap_index` 位域 20→32 位 | **编译期** `#error "Too large font or glyphs..."` |
| `LV_USE_FONT_COMPRESSED` | 启用 RLE 解压代码 | **运行期** `return NULL` → 空白（不报错！） |

第一个坑「编译期」能靠报错暴露，第二个坑「运行期」完全静默，只能靠「现象观察 + 读源码」定位。

---

## 六、修复与验证

**修复**：[lv_conf.h](lv_conf.h) 第 375 行

```c
#define LV_USE_FONT_COMPRESSED 1   /* 原为 0 */
```

**验证**：

```bash
./build_lvgl.sh
# 预期：编译通过，demo 里出现解压缓冲符号
arm-linux-gnueabihf-nm lvgl_build_output/demo | grep _lv_font_decompr_buf
# 预期：005e8720 B _lv_font_decompr_buf  ← BSS 段，说明解压代码已编入
```

> `_lv_font_decompr_buf` 是 RLE 解压的临时缓冲区，只有 `LV_USE_FONT_COMPRESSED=1` 时才被定义。它的出现 = 开关真的生效了（不只是改了配置没重新编译）。

---

## 七、遗留：修复后上板出现段错误（未解决）

开 `LV_USE_FONT_COMPRESSED=1` 重编后，上板运行 `/root/demo` 直接 **`Segmentation fault`**（启动即崩）。

这是本次修复引出的**新问题**，尚未定位。排查进展：

**已排除**（读源码查证，非越界、非初始化）：
- `decompress` / `bits_write` / `get_bits`：bpp=4 时 `wrp` 每次 +4bit，`bits_write` 明确「不跨字节边界」，无越界写；
- 解压缓冲 `_lv_font_decompr_buf`：是 BSS 段普通全局变量（`LV_GC_ROOT(x)=x`，见 `lv_gc.h:73`），加载时清零 → 初始 NULL，首次 `realloc(NULL,size)` 正常。

**待验证方向**：
1. `lv_mem_realloc` 分配失败（`LV_MEM_CUSTOM=1` 走 `malloc`）触发 `LV_ASSERT_MALLOC`；
2. 内存总量：demo bss ≈ 2.4MB + 字体 rodata 5.4MB（两个字号）+ LVGL 堆，看板上是否够；
3. 需 dmesg 崩溃地址精确定位。

**定位手段**（demo `not stripped`、带 `debug_info`）：
- 板上 `dmesg | tail` 拿 `ip`（崩溃指令地址）+ `error`（错误码）；
- 宿主机 `arm-linux-gnueabihf-addr2line -e lvgl_build_output/demo -f -C <ip>` 反查到函数/行；
- `error` 码粗判：`4`=读非法地址（空指针读）、`6`=写非法地址、`7`=写只读页。

> 这条记录到文档里，是为了**如实标记「中文空白已解决、但解压路径还有段错误待排查」**，避免把「修好了」说满。

---

## 八、FPS 中文：第二个坑（「方框」≠「空白」）

修好 `LV_USE_FONT_COMPRESSED` 后，又在 FPS 那行加了中文「帧率/模式」。这次中文显示成**方框**——和底部的「空白」是**两种不同的故障**：

| 现象 | 含义 | 根因 |
|------|------|------|
| 空白（连方框都没有） | 字体位图根本没画出来 | 压缩字库 + 关了解压 → `return NULL` |
| 方框 □ | 字体加载了，但这个字查不到字形 | 字体**字符集覆盖不够** |

FPS 的 `s_perf_label` 没设字体，用的默认 `montserrat_14`——它是**纯英文**字体，只有 ASCII 字形，没有「帧率/模式」这几个汉字，所以显示方框。

**修复**：给 `s_perf_label` 单独设一份**小号中文**字体：

```c
s_perf_label = lv_label_create(lv_layer_top());
lv_obj_set_style_text_font(s_perf_label, &oppo_sans_14, 0);   /* 小号中文，中文才能显示 */
```

**教训**：排查显示问题时先分清「空白 / 方框 / 乱码」三个信号——分别指向「没画 / 缺字形 / 编码错」三条完全不同的路。这次恰好前两个坑各踩一个。

---

## 九、oppo_sans_14：第二个字号（验证「每字号一份」）

FPS 是调试信息，不该用 20 号（太大），所以再转一份 14 号。这实际验证了 [06](06-移植OPPOSans中文字库-离线转C字库.md) 里「每个字号转一份」的策略：

```bash
lv_font_conv \
  --font ".../OPPO Sans 4.0.ttf" \
  --size 14 --bpp 4 --format lvgl \
  -r 0x20-0x7F -r 0x3000-0x303F -r 0xFF00-0xFFEF -r 0x4E00-0x9FA5 \
  --lv-include "lvgl/lvgl.h" \
  -o fonts/oppo_sans_14.c
```

| 字号 | 产物 | 变量名 | 用途 |
|------|------|--------|------|
| 20 | 21MB | `oppo_sans_20` | 正文 / 底部中文 label |
| 14 | 13MB | `oppo_sans_14` | FPS / 小字 |

集成三步（与 20 号一致）：CMake 加 `fonts/oppo_sans_14.c` → `LV_FONT_DECLARE(oppo_sans_14)` → 设给 `s_perf_label`。编译后 demo 5.6MB，两个字体符号都在。

---

## 十、内存泄漏澄清

排查段错误时顺带确认了「会不会泄漏」——**不会累积泄漏**。唯一每 500ms 动态分配的点是 `perf_timer_cb` 里的 `lv_label_set_text`，查证 `lv_label.c` 用的是 `lv_mem_realloc(label->text, len)` **复用**旧 buffer，不重复 new；对象销毁时也会 `lv_mem_free`。

**「常驻」≠「泄漏」**：双缓冲 2.4MB（static）、字体位图 5.4MB（`.rodata`）、解压缓冲（单例复用）都是固定占用，不随运行增长。真正要警惕的是「每帧/每次回调都 new 且不 free」，当前代码没有。

> 顺带澄清：段错误不是泄漏导致的——泄漏是「内存越用越多、慢慢卡死」，段错误是「立刻崩溃」，机制不同。

---

## 十一、可复用的方法论（下次遇到「显示不出来」照这个走）

1. **先离线排除「编译/数据/配置」层**：文件在不在、变量名对不对、开关值对不对、产物里有没有符号/字符串/数据。这些全都能在宿主机查，最快也最确定。
2. **问清现象再动手**：空白 ≠ 方框 ≠ 乱码，三种现象指向三种根因。用一两个精准提问锁定，而不是猜。
3. **找「正常 vs 异常」的对照组差异**：这次是「FPS 正常、中文空白」这个对照，直接把问题圈到了「字体位图格式」。
4. **读渲染/消费方的源码**：数据本身没错时，去读「谁在消费这份数据」，往往能一眼看到 `return NULL` 或 `if(!...)` 的静默失败点。
5. **验证「开关真的生效」要有物证**：改完配置，编译产物里要有对应符号/代码段变化（这次是 `_lv_font_decompr_buf`），不能只信「我改了」。

---

## 十二、关联文档

| 文档 | 内容 |
|------|------|
| [06-移植OPPOSans中文字库](06-移植OPPOSans中文字库-离线转C字库.md) | 转换命令、CMake、使用方式、风险 |
| [08-端到端步骤](08-OPPOSans字库移植-端到端步骤与逐步验证.md) | 6 步操作手册 + 逐步验证（本次坑应补进这里） |
| [07-移植复盘](07-字库移植复盘-从TTF到C字库的处理过程.md) | 从 TTF 到 C 字库的方案决策复盘 |
