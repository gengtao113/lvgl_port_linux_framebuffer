# 06 移植 OPPO Sans 中文字库（lv_font_conv 离线转全字集 C 字库）

> 目标工程：`lvgl_port_linux_framebuffer`
> 字库来源：`OPPO_Sans_4.0/OPPO Sans 4.0.ttf`（22MB TrueType，含 CJK 全字集）
> 日期：2026-09-06
> 一句话：用 `lv_font_conv` 把 OPPO Sans 转成 LVGL 的 `.c` 字库、覆盖 GB2312 全字集，编译进 ELF，运行时零依赖显示任意中文。

---

## 一、结论

**方案**：离线转 C 字库（不是 freetype 运行时加载）。

**为什么这么选**（对比另一条路）：

| | 离线转 C 字库 ✅ | freetype 运行时加载 ❌ |
|---|---|---|
| 运行时依赖 | 零 | 要 freetype 交叉库 |
| 工具链 | 不动（Linaro 4.9.4 照旧） | Linaro 4.9.4 sysroot 里**没有** freetype，要换/配工具链 |
| 字符集 | 覆盖 GB2312 全字集（转时指定） | 22MB 任意中文 |
| 内存/CPU | 纯查表，零光栅化开销 | 每遇新字符要光栅化（有缓存） |
| 集成 | 加一个 `.c` 进 CMake，和 GIF 一致 | 链 freetype + 放 TTF 到板子 |

代价是**字号固定**（每个字号转一份）和**字符集固定**（超出 GB2312 的生僻字会显示成缺字方框）。对「界面显示中文」这个场景，两者都够。

---

## 二、现状盘点

- LVGL v8，[lv_conf.h](lv_conf.h) 里 `LV_USE_FREETYPE 0`、`LV_FONT_DEFAULT &lv_font_montserrat_14`（默认只有英文）。
- [main.c](main.c) 目前只显示 FPS 数字，没有中文。
- 工具链 Linaro 4.9.4 `arm-linux-gnueabihf-gcc`（[build_lvgl.sh](build_lvgl.sh)）。
- 宿主机 `npx lv_font_conv` 已验证可用（v1.5.3）。

---

## 三、字符范围设计（覆盖「动态中文」）

「动态文字」要求覆盖**任意常用中文**，所以不是只转几个词，而是转整个 CJK 基本区：

| 范围 | 内容 | 说明 |
|------|------|------|
| `0x20-0x7F` | 基本 ASCII | 英文、数字、常见英文标点 |
| `0x3000-0x303F` | CJK 标点 | 、。〈〉《》「」『』等 |
| `0xFF00-0xFFEF` | 全角形式 | 全角逗号/问号/冒号、全角字母数字 |
| `0x4E00-0x9FA5` | CJK 统一表意文字基本区 | 20902 字，**覆盖 GB2312 全部 6763 字**，常用汉字全在内 |

`0x4E00-0x9FA5` 是「中文动态文字」的核心——GB2312 的一二级汉字（6763 个）全部落在这一区间，所以转这一区就能显示任何 GB2312 范围内的中文。

---

## 四、落地步骤

### 步骤 0：安装 lv_font_conv（只需 Node.js）

`lv_font_conv` 是纯 JS 工具（依赖 opentype.js 等纯 JS 包），**不需要系统 freetype、不需要 gcc**。前置条件只有 Node.js + npm/npx。

两种安装方式，二选一：

```bash
# 方式 A：npm 全局安装（需 sudo，装一次后直接用裸命令，本机已装）
sudo npm install -g lv_font_conv
lv_font_conv --version        # → 1.5.3

# 方式 B：npx 临时运行（零安装），没装全局时用，命令前加 npx --yes
npx --yes lv_font_conv --version
```

### 步骤 1：转换（宿主机，npx 跑 lv_font_conv）

```bash
cd /path/to/lvgl_port_linux_framebuffer

lv_font_conv \
  --font "/home/gengtao/linux-imx6ull/gengtao_linux_frambuffer_lvgl/OPPO_Sans_4.0/OPPO_Sans_4.0/OPPO Sans 4.0.ttf" \
  --size 20 --bpp 4 --format lvgl \
  -r 0x20-0x7F -r 0x3000-0x303F -r 0xFF00-0xFFEF -r 0x4E00-0x9FA5 \
  --lv-include "lvgl/lvgl.h" \
  -o oppo_sans_20.c
```

参数说明：

| 参数 | 值 | 说明 |
|------|-----|------|
| `--font` | TTF 路径 | 源字体 |
| `--size` | 20 | 像素字号（**每个字号转一份**） |
| `--bpp` | 4 | 位深：4 = 16 级灰度抗锯齿（中文推荐，比 1/2 漂亮，比 8 省） |
| `--format` | lvgl | 输出 LVGL 格式 |
| `-r` | 上表范围 | 可多次，每次一个区间 |
| `--lv-include` | `lvgl/lvgl.h` | 生成 `.c` 里 include 什么（对齐 main.c 的写法） |
| `-o` | `oppo_sans_20.c` | 输出文件，**变量名 = 文件名 basename** → `oppo_sans_20` |

### 步骤 1.5：开 `LV_FONT_FMT_TXT_LARGE`（全字集必做）

全字集字库的位图数据超过 1MB，而 `lv_conf.h` 里 `LV_FONT_FMT_TXT_LARGE 0` 时，`lv_font_fmt_txt.h` 的 `bitmap_index` 是 20 位位域（最大 1MB），编译会报 `#error`。改 [lv_conf.h](lv_conf.h)：

```c
#define LV_FONT_FMT_TXT_LARGE 1   /* 原为 0 */
```

验证：`grep LV_FONT_FMT_TXT_LARGE lv_conf.h` → `1`。实测开启后编译 0 error、字库 `.o` 3.3MB。

### 步骤 2：放进工程 + 改 CMake

把 `oppo_sans_20.c` 放进工程（建议建 `fonts/` 目录）：

```text
lvgl_port_linux_framebuffer/
└── fonts/
    └── oppo_sans_20.c
```

[CMakeLists.txt](CMakeLists.txt) 的 `add_executable(demo ...)` 加进该文件：

```cmake
add_executable(demo
    main.c
    mouse_cursor_icon.c
    fonts/oppo_sans_20.c
)
```

> 生成 `.c` 里是 `#include "lvgl/lvgl.h"`，而工程根已在 include path（`target_include_directories(demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})`），所以无论放 `fonts/` 还是根目录都能解析。

### 步骤 3：使用字体

**方式 A：某个控件单独用**（推荐先用这个验证）：

```c
LV_FONT_DECLARE(oppo_sans_20);   /* 声明外部字库变量 */

lv_obj_t * label = lv_label_create(lv_scr_act());
lv_obj_set_style_text_font(label, &oppo_sans_20, 0);
lv_label_set_text(label, "你好，OPPO Sans 中文字体");
```

**方式 B：设为全局默认字体**（所有 label 默认用中文），改 [lv_conf.h](lv_conf.h)：

```c
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(oppo_sans_20)
#define LV_FONT_DEFAULT &oppo_sans_20
```

> 注意：方式 B 需要该字库在 `lvgl` 库内部可见（`LV_FONT_CUSTOM_DECLARE` 在 lvgl 编译单元里展开），如果你的字库 `.c` 是编译进 `demo` 而不是 `lvgl` 库，链接时会报找不到符号。**建议先用方式 A**（per-widget），跑通后再决定要不要全局默认。

### 步骤 4：交叉编译 + 上板

```bash
./build_lvgl.sh          # 交叉编译，产物 lvgl_build_output/demo
./copy_lvgl_build_file.sh
```

板上运行后，label 应显示中文。

---

## 五、字号策略

一个界面通常要 2~3 个字号（标题大、正文中、辅助小）。建议：

| 字号 | 用途 |
|------|------|
| 16 | 辅助/注释/小字 |
| 20~24 | 正文/按钮 |
| 32 | 标题/大数字 |

**每个字号重复步骤 1 转一份**，命名 `oppo_sans_16.c` / `oppo_sans_24.c` / `oppo_sans_32.c`，变量名 `oppo_sans_16` 等。

---

## 六、大小与内存（实测）

已实测（bpp=4、size=20、`0x4E00-0x9FA5` 全 CJK 基本区 + ASCII + 标点）：

| 项 | 实测值 |
|----|--------|
| 转换耗时 | 约 81 秒（bpp=4）/ 约 63 秒（bpp=2） |
| 生成 `.c` 源码 | **bpp=4 → 21MB**；**bpp=2 → 13MB**（含位图 hex + 2 万字 glyph 描述） |
| 编译后只读段增量 | **实测 3.3MB**（bpp=4，`size` 显示 3395504 字节）；bpp=2 约 2MB（hex 文本编译回二进制会缩小） |

**要点**：
- 源 TTF 22MB 只用来转换，**不上板**。
- 21MB 是 `.c` 源码体积（hex 文本），编译进 ELF 后只读段实际增量约 3.3MB（实测 `.o`）；但源码大意味着交叉编译这个文件会明显变慢（几十秒~几分钟），属正常。
- i.MX6ULL 一般 256MB/512MB DDR3，3.3MB 一个字号的只读段放得下；但若上 3 个字号（16/24/32），总量约 10MB 级，注意规划。

**缩小体积的杠杆（按收益排序）**：

1. **降 bpp**：4 → 2，实测 21MB → 13MB（减约 38%，因 glyph 描述结构不变、只有位图减半），略毛边，中文仍可读；4 → 1 再减但小字号中文发虚，不推荐。
2. **缩字符范围**：如果不用全 GB2312，`-r 0x4E00-0x9FA5` 换成常用 3500 字（用 `--symbols` 指定），体积大幅下降。
3. **少上几个字号**：每个字号独立一份，只转界面真用的 2~3 个。

---

## 七、风险与注意

1. **超出范围的字符会变缺字方框**：`0x4E00-0x9FA5` 覆盖 GB2312 全字集，但 GBK 扩展区（生僻字、部分符号）不在内。若遇到显示不出的字，扩展 `-r` 范围或 `--symbols` 单独补。
2. **bpp 越大越漂亮也越大**：中文抗锯齿推荐 4；空间紧可降到 2（略毛边），8 是彩色图标用，普通文字不必。
3. **编译时间**：单文件几 MB 的 `.c`，交叉编译会比现在慢（几十秒~几分钟），属正常。
4. **保持默认 RLE 压缩**：不要加 `--no-compress`，中文压缩收益很大。
5. **变量名 = 输出文件名 basename**：`-o oppo_sans_20.c` → 变量 `oppo_sans_20`，`LV_FONT_DECLARE(oppo_sans_20)` 要与之一致。
6. **UTF-8 源码**：`.c` 里写中文字符串要保证文件是 UTF-8 编码，且 LVGL 需开启 UTF-8 支持（LVGL v8 默认支持 UTF-8 label，检查 lv_conf.h 的 `LV_TXT_ENC`，默认 `LV_TXT_ENC_UTF8`）。
7. **全字集必开 `LV_FONT_FMT_TXT_LARGE 1`**：全字集位图数据 > 1MB，不开编译报 `#error "Too large font or glyphs..."`（见步骤 1.5）。

---

## 八、验收清单

| 项 | 验收标准 |
|----|---------|
| 转换 | `oppo_sans_20.c` 生成，`ls -lh` 看大小 |
| 编译 | `./build_lvgl.sh` 通过，`file` 显示 ARM ELF |
| 显示 | 板上 label 显示中文「你好，OPPO Sans 中文字体」 |
| 动态 | 改 label 文本为任意 GB2312 中文，都能正确显示（不出现方框） |
