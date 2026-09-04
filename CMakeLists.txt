#
# CMakeLists.txt — 替代原 Makefile
# 产物默认在 out-of-tree 构建目录（建议：lvgl_build_output/demo）
#
# 本机构建：
#   cmake -S . -B lvgl_build_output
#   cmake --build lvgl_build_output -j$(nproc)
#
# 交叉编译（或直接 ./build_lvgl.sh）：
#   cmake -S . -B lvgl_build_output \
#     -DCMAKE_SYSTEM_NAME=Linux \
#     -DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc \
#     -DCMAKE_BUILD_TYPE=Release
#   cmake --build lvgl_build_output -j$(nproc)
#

cmake_minimum_required(VERSION 3.12)
project(lvgl_linux_framebuffer C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 可执行文件输出到构建目录根（lvgl_build_output/demo）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# 在 add_subdirectory(lvgl) 之前指定本工程 lv_conf.h
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH
    "Path to lv_conf.h" FORCE)

# 与旧 Makefile 接近的基础优化选项（不含 GCC 新版本才有的 -W*）
add_compile_options(-O3 -g0 -Wall -Wextra
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -Wno-unused-function
    -fno-strict-aliasing)

add_subdirectory(lvgl)
add_subdirectory(lv_drivers)

# lv_drv_conf.h / lv_conf.h 位于工程根；确保 drivers 与 demo 都能找到
target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(lv_drivers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Threads REQUIRED)

add_executable(demo
    main.c
    mouse_cursor_icon.c
)

target_include_directories(demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(demo PRIVATE
    lvgl
    lvgl_demos
    lv_drivers
    m
    Threads::Threads
)

# 兼容旧习惯：cmake --build … --target clean 已由 CMake 提供
# 额外 target：彻底删掉推荐构建目录外的散落产物（可选）
add_custom_target(distclean
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Remove build directory ${CMAKE_BINARY_DIR}"
)
