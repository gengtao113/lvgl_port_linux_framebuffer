# 构建体系：Makefile → CMake

> 日期：2026-09-04  
> 工程：`gengtao_linux_frambuffer_lvgl/lvgl_port_linux_framebuffer`  
> 背景：原根目录 `Makefile` 手工收集 `lvgl.mk` / `lv_drivers.mk` 源并输出到 `lvgl_build_output/`；现改为官方 submodule 自带的 CMake 集成。

## 业务目的

保持「交叉编出 ARM `demo` → NFS 上板验证」不变，只换构建前端：

- 仍产出：`lvgl_build_output/demo`
- 仍适配：Linaro GCC 4.9（不用新版 `-W*`）
- 仍部署：`./copy_lvgl_build_file.sh` → NFS `/root/demo`

## 做了什么

1. **新增 `CMakeLists.txt`**（替代根 `Makefile`）  
   - `LV_CONF_PATH` 指向工程根 `lv_conf.h`  
   - `add_subdirectory(lvgl)` / `add_subdirectory(lv_drivers)`  
   - 可执行目标 `demo`：`main.c` + `mouse_cursor_icon.c`  
   - 链接：`lvgl`、`lvgl_demos`、`lv_drivers`、`m`、`Threads`  
   - 工程根加入 include，保证找到 `lv_conf.h` / `lv_drv_conf.h`  
   - 编译选项精简为 GCC 4.9 可用集（`-O3 -g0 -Wall -Wextra` 等）

2. **删除根目录 `Makefile`**  
   源码旁散落 `.o` 的旧规则不再维护；一律 out-of-tree 构建。

3. **`build_lvgl.sh` 改为走 CMake**  
   ```text
   cmake -S . -B lvgl_build_output \
     -DCMAKE_SYSTEM_NAME=Linux \
     -DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc \
     -DCMAKE_BUILD_TYPE=Release
   cmake --build lvgl_build_output -jN
   ```  
   `./build_lvgl.sh clean` → 删除整个 `lvgl_build_output/`。

4. **附属调整**  
   - `copy_lvgl_build_file.sh`：提示改为 `./build_lvgl.sh`  
   - `.gitignore`：继续忽略 `lvgl_build_output/`、`/demo`，并加上常见 `/build/`、`/cmake-build-*/`

## 变更清单

| 文件 | 类型 | 说明 |
|------|------|------|
| `CMakeLists.txt` | 新增 | 根构建入口 |
| `Makefile` | 删除 | 逻辑已迁到 CMake |
| `build_lvgl.sh` | 修改 | `make` → `cmake` 配置 + 构建 |
| `copy_lvgl_build_file.sh` | 修改 | 构建命令提示同步 |
| `.gitignore` | 修改 | CMake 构建目录忽略项 |

## 怎么用

```text
# 推荐（交叉编译）
./build_lvgl.sh
./copy_lvgl_build_file.sh

# 等价手写
cmake -S . -B lvgl_build_output \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc \
  -DCMAKE_BUILD_TYPE=Release
cmake --build lvgl_build_output -j$(nproc)

# 清理
./build_lvgl.sh clean
```

## 与旧 Makefile 的对应关系

| 旧（Makefile） | 新（CMake） |
|----------------|-------------|
| `include lvgl.mk` / `lv_drivers.mk` | `add_subdirectory(lvgl/lv_drivers)` |
| `BUILD_DIR=lvgl_build_output` | `-B lvgl_build_output`（二进制目录） |
| `BIN=$(BUILD_DIR)/demo` | `add_executable(demo …)` + `CMAKE_RUNTIME_OUTPUT_DIRECTORY` |
| `make CC=arm-… CFLAGS=…` | `-DCMAKE_C_COMPILER=` + `add_compile_options` |
| `make clean` | `./build_lvgl.sh clean` 或删构建目录 |

## Makefile 与 CMakeLists.txt 优缺点

结合本工程（LVGL + lv_drivers、交叉编译、产物进 `lvgl_build_output/`）对比。

### Makefile

| | 说明 |
|--|------|
| **优点** | 无额外工具依赖，板上/精简环境也能直接 `make`；规则写死、行为直观，改一行 `CC=`/`CFLAGS=` 立刻生效；与官方 port 自带的 `lvgl.mk` / `lv_drivers.mk` 天然契合，上手快。 |
| **缺点** | 源文件靠 `.mk` + `VPATH`/`foreach` 手工拼，工程一大就难维护；out-of-tree、多配置（Debug/Release、本机/交叉）要自己写规则；官方默认 `CFLAGS` 常夹带新版 `-W*`，Linaro 4.9 易踩坑；IDE / CI 集成弱，可移植性靠人抄命令。 |

### CMakeLists.txt

| | 说明 |
|--|------|
| **优点** | LVGL / lv_drivers 官方已提供 CMake，`add_subdirectory` 即可复用，少维护一长串源列表；天然 out-of-tree（`-B lvgl_build_output`），源码树干净；交叉编译用 `-DCMAKE_C_COMPILER=`（或 toolchain 文件）更规范；生成 Ninja/Make 皆可，IDE、CI 友好；目标/依赖（`lvgl`、`lvgl_demos`、`Threads`）表达清晰。 |
| **缺点** | 依赖宿主机安装 CMake（≥3.12）；多一层「配置 → 生成 → 编译」，排错时要分清是 CMake 阶段还是编译阶段；脚本/文档要从 `make …` 改成 `cmake -S/-B` + `cmake --build`；极简环境若无 CMake 则编不了（本机交叉场景通常可接受）。 |

### 本工程为何选 CMake

```text
目标不变：交叉编出 ARM demo → NFS 上板
手段变化：少维护 mk 拼装，跟上游 CMake 对齐，产物目录仍用 lvgl_build_output/
代价：多依赖一个 cmake；由 build_lvgl.sh 包掉命令差异
```

| 维度 | Makefile（旧） | CMake（现） |
|------|----------------|-------------|
| 源码收集 | 自维护 mk / 规则 | 跟官方 submodule |
| 交叉编译 | `CC=` + 手写兼容 `CFLAGS` | `CMAKE_C_COMPILER` + `add_compile_options` |
| 产物位置 | `BUILD_DIR` | `-B` 构建目录 |
| 环境依赖 | 仅需 make + 交叉 gcc | 另需 cmake |
| 长期维护 | 规则易腐 | 更贴近上游 |

本板屏参 / 触摸节点（`main.c`、`lv_conf.h`、`lv_drv_conf.h`）未因本次构建切换而改动。
