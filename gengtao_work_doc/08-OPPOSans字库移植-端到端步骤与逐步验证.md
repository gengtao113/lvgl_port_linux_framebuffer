# 08 OPPO Sans 字库移植：端到端详细步骤与逐步验证

> 目标工程：`lvgl_port_linux_framebuffer`
> 日期：2026-09-06
> 定位：这是**可照着做的操作手册**。每一步 = 做什么 → 怎么验证 → 预期结果 → 失败查什么。
> 配套：[06 操作步骤](06-移植OPPOSans中文字库-离线转C字库.md)、[07 处理复盘](07-字库移植复盘-从TTF到C字库的处理过程.md)

---

## 一、总览：6 步链路 + 每步验证点

| 步 | 做什么 | 验证命令 | 预期结果 |
|----|--------|---------|---------|
| 0 | 装工具 | `npx --yes lv_font_conv --version` | 输出 `1.5.3` |
| 1 | 转字库 | `ls -lh oppo_sans_20.c` + `grep "const lv_font_t"` | 21MB + 变量名 `oppo_sans_20` |
| 2 | **开 `LV_FONT_FMT_TXT_LARGE`** | `grep LV_FONT_FMT_TXT_LARGE lv_conf.h` | 值为 `1`（**全字集必开，否则编译 #error**） |
| 3 | 放工程 + 改 CMake | `cmake` 配置 + `cmake --build` | 配置/编译无报错 |
| 4 | main.c 加中文示例 | （随步 3 一起编译） | 编译通过 |
| 5 | 交叉编译 | `./build_lvgl.sh` + `file demo` + `size demo` | ARM ELF，体积增约 3.3MB |
| 6 | 上板验证 | 屏显 + 改文本 | 中文正常显示、无方框 |

> **核心原则**：每步验证要有「明确的预期结果」，不能只看「没报错」。比如步 1 要看「变量名对、大小对」，步 2 要看「值是 1」，步 6 要「真的看到中文」。

---

## 二、每一步详解

### 步 0：装工具

**做什么**：确认 `lv_font_conv` 可用（纯 JS 工具，只需 Node.js，无需 freetype/gcc）。

```bash
lv_font_conv --version        # 已全局安装，直接裸命令 → 1.5.3
# 没装全局的话：npx --yes lv_font_conv --version
```

**验证**：输出 `1.5.3`。

**失败查什么**：报 `command not found` → 没装 node；报网络错误 → npx 首次下载失败，重试或换 npm 源。

---

### 步 1：转字库

**做什么**：

```bash
cd /home/gengtao/linux-imx6ull/gengtao_linux_frambuffer_lvgl/lvgl_port_linux_framebuffer

lv_font_conv \
  --font "/home/gengtao/linux-imx6ull/gengtao_linux_frambuffer_lvgl/OPPO_Sans_4.0/OPPO_Sans_4.0/OPPO Sans 4.0.ttf" \
  --size 20 --bpp 4 --format lvgl \
  -r 0x20-0x7F -r 0x3000-0x303F -r 0xFF00-0xFFEF -r 0x4E00-0x9FA5 \
  --lv-include "lvgl/lvgl.h" \
  -o oppo_sans_20.c
```

**验证（三条，缺一不可）**：

```bash
ls -lh oppo_sans_20.c                          # 预期：约 21MB
grep -n "const lv_font_t" oppo_sans_20.c       # 预期：末尾有 const lv_font_t oppo_sans_20
tail -3 oppo_sans_20.c                          # 预期：看到 #error 检查（正是步 2 要解决的）
```

**失败查什么**：`.c` 明显偏小（几百 KB）→ 字符范围 `-r` 写错，确认 `0x4E00-0x9FA5` 那行；变量名不是 `oppo_sans_20` → `-o` 文件名没对上。

---

### 步 2：开 `LV_FONT_FMT_TXT_LARGE`（关键，实测发现）

**为什么必须有这步**：全字集字库的位图数据超过 1MB，而 `lv_conf.h` 里 `LV_FONT_FMT_TXT_LARGE 0` 时，`lv_font_fmt_txt.h` 的 `bitmap_index` 是 **20 位位域**（最大 1MB）。不开就编译报错：

```
错误： #error "Too large font or glyphs in OPPO_SANS_20. Enable LV_FONT_FMT_TXT_LARGE in lv_conf.h"
```

**做什么**：改 [lv_conf.h](lv_conf.h) 第 372 行：

```c
#define LV_FONT_FMT_TXT_LARGE 1   /* 原为 0 */
```

**验证**：

```bash
grep -n "LV_FONT_FMT_TXT_LARGE" lv_conf.h   # 预期：= 1
```

**验证「确实生效」的最硬办法**：步 2 之前单独编译字库会 `#error`，步 2 之后编译 0 error（见步 5 的实测）。

**失败查什么**：改了但还是报 `#error` → 确认改的是**工程正在用的那个** `lv_conf.h`（本项目是工程根这份，CMake 里 `LV_CONF_PATH` 指到它）。

---

### 步 3：放工程 + 改 CMake

**做什么**：

```text
lvgl_port_linux_framebuffer/
└── fonts/
    └── oppo_sans_20.c      ← 把步 1 的产物放进来
```

改 [CMakeLists.txt](CMakeLists.txt)：

```cmake
add_executable(demo
    main.c
    mouse_cursor_icon.c
    fonts/oppo_sans_20.c     # ← 新增这行
)
```

**验证**：

```bash
cmake -S . -B lvgl_build_output -DCMAKE_SYSTEM_NAME=Linux \
      -DCMAKE_SYSTEM_PROCESSOR=arm \
      -DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc -DCMAKE_BUILD_TYPE=Release
# 预期：配置成功，无 CMake 错误
```

**失败查什么**：报 `oppo_sans_20.c` 找不到 → 文件名/路径不对；报 `LV_CONF_PATH` 相关 → 保持默认别动。

**附：这条 cmake 命令在做什么**（理解了它，后面看 `build_lvgl.sh` 就不懵）

一句话：用 ARM 交叉编译器，把工程配置成「目标 = ARM 平台的 Linux」，产物输出到 `lvgl_build_output`。

| 片段 | 含义 |
|------|------|
| `cmake` | 生成构建系统（Makefile），**自己不编译** |
| `-S .` | 源码目录 = 当前目录（`CMakeLists.txt` 所在处） |
| `-B lvgl_build_output` | 构建目录 = Makefile / 中间 `.o` / 最终 `demo` 都放这里 |
| `-DCMAKE_SYSTEM_NAME=Linux` | 目标系统 = Linux |
| `-DCMAKE_SYSTEM_PROCESSOR=arm` | 目标 CPU = ARM |
| `-DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc` | 用哪个 C 编译器 = 交叉编译器 |
| `-DCMAKE_BUILD_TYPE=Release` | 构建类型 = Release |

三个易混点：

1. **`-S`/`-B` 分离 = out-of-source build**：源码和产物分开，删 `lvgl_build_output` 即彻底清理。
2. **`-DXXX=yyy` 是给 CMake 传变量**，不是给 C 代码传宏（虽然长得像）。
3. **三个 `-D` 合起来才构成「交叉编译」**：编译器是谁 + 目标什么 OS + 目标什么 CPU。注意 `SYSTEM_NAME=Linux` 不是 `ARM`——因为板上跑的就是 Linux，只是 CPU 是 ARM。

这条命令**只配置、不编译**（所以秒级 `Configuring done`）；真正编译要再跑 `cmake --build lvgl_build_output`。`build_lvgl.sh` 内部就是「这条配置命令 + `cmake --build`」两步。

---

### 步 4：main.c 加中文示例

**做什么**：在 [main.c](main.c) 里加一个中文 label，验证字体能用：

```c
LV_FONT_DECLARE(oppo_sans_20);                 /* 声明外部字库变量 */

/* 在 main() 里，GIF 之外加： */
lv_obj_t * label = lv_label_create(lv_scr_act());
lv_obj_set_style_text_font(label, &oppo_sans_20, 0);
lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
lv_label_set_text(label, "你好，OPPO Sans 中文字体");
lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -8);
```

**验证**：随步 5 一起编译。编译过 = 声明/链接/头文件都对了。

---

### 步 5：交叉编译

**做什么**：

```bash
./build_lvgl.sh
```

**验证（三条）**：

```bash
file lvgl_build_output/demo                          # 预期：ARM 32-bit ELF
arm-linux-gnueabihf-size lvgl_build_output/demo      # 预期：text/data 比加字库前增约 3.3MB
# （实测：单个 oppo_sans_20.o 的 size = text 3395504 ≈ 3.24MB）
```

**失败查什么**：报 `#error "Too large font..."` → 步 2 没做对；报 `LV_FONT_DECLARE` 未声明 → 变量名拼错；报链接找不到 `oppo_sans_20` → CMake 没把 `.c` 加进去。

---

### 步 6：上板验证

**做什么**：拷到板子运行：

```bash
./copy_lvgl_build_file.sh
# 或 scp lvgl_build_output/demo root@<板子IP>:/root/
```

**验证（两条）**：

1. **静态**：屏幕底部出现「你好，OPPO Sans 中文字体」。
2. **动态（验证字符集真的够）**：把 `lv_label_set_text` 换成别的任意中文（如「设置 温度 速度 地图」），重编译再跑，**不应出现方框/问号**。

**失败查什么**：显示方框 → 那个字不在 `0x4E00-0x9FA5` 范围（生僻字），或步 1 字符范围没转全；显示乱码 → 源码不是 UTF-8 编码。

---

## 三、验证速查表（对着打勾）

| 验证项 | 命令 | 打勾 |
|--------|------|------|
| 工具可用 | `lv_font_conv --version` → `1.5.3` | ☐ |
| 字库生成 | `ls -lh oppo_sans_20.c` → ~21MB | ☐ |
| 变量名对 | `grep "const lv_font_t" oppo_sans_20.c` → `oppo_sans_20` | ☐ |
| 大字体开关 | `grep LV_FONT_FMT_TXT_LARGE lv_conf.h` → `1` | ☐ |
| CMake 配置 | `cmake -S . -B ...` 无报错 | ☐ |
| 交叉编译 | `./build_lvgl.sh` 通过 | ☐ |
| 是 ARM | `file demo` → ARM | ☐ |
| 体积增量 | `size demo` 增 ~3.3MB | ☐ |
| 中文显示 | 屏显「你好，OPPO Sans 中文字体」 | ☐ |
| 动态无方框 | 换任意中文仍正常 | ☐ |

---

## 四、这次实测暴露的坑（重点）

**全字集字库必须开 `LV_FONT_FMT_TXT_LARGE 1`**，这是 [06](06-移植OPPOSans中文字库-离线转C字库.md) 初版漏掉的。原因链：

1. `lv_font_conv` 生成的 `.c` 末尾自带检查：`#if LV_FONT_FMT_TXT_LARGE == 0 ... #error "Too large font..."`。
2. 全字集 bpp=4 的位图数据实测最大 `bitmap_index = 3051476`（> 1MB），超 20 位位域。
3. 不开 → 编译直接 `#error`；开了 → `bitmap_index` 变 32 位，编译 0 error，`.o` 3.3MB。

> 经验：**光「转换成功」不够，要「编译通过」才算数**。这次就是靠「编译验证」这一步，才逼出了这个非改不可的配置项。
