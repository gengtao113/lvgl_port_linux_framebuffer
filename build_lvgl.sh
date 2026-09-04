#!/usr/bin/env bash
# L3：交叉编译本目录 → ARM 可执行文件（CMake）
# 产物统一在：lvgl_build_output/（中间文件 + demo）
# 用法：
#   ./build_lvgl.sh
#   ./build_lvgl.sh clean   # 删除构建目录
#   JOBS=4 ./build_lvgl.sh  # 指定并行数
#
# 说明：用 CMake 构建，CFLAGS 仅用 GCC 4.9 可用选项（见 CMakeLists.txt），
# 避免官方旧 Makefile 里偏新的 -W* 导致 Linaro 4.9 报错。

set -euo pipefail

PROJ_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${OUT_DIR:-${PROJ_DIR}/lvgl_build_output}"
BIN="${OUT_DIR}/demo"

TOOLCHAIN_BIN="${TOOLCHAIN_BIN:-/usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin}"
CROSS_CC="${CROSS_CC:-arm-linux-gnueabihf-gcc}"
JOBS="${JOBS:-$(nproc)}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

export PATH="${TOOLCHAIN_BIN}:${PATH}"

if ! command -v cmake >/dev/null 2>&1; then
	echo "ERROR: 找不到 cmake，请先安装 CMake（>= 3.12）"
	exit 1
fi

if ! command -v "${CROSS_CC}" >/dev/null 2>&1; then
	echo "ERROR: 找不到 ${CROSS_CC}"
	echo "  请检查工具链目录：${TOOLCHAIN_BIN}"
	echo "  或：TOOLCHAIN_BIN=/你的路径/bin ./build_lvgl.sh"
	exit 1
fi

if [[ ! -f "${PROJ_DIR}/CMakeLists.txt" ]]; then
	echo "ERROR: 本目录无 CMakeLists.txt：${PROJ_DIR}"
	exit 1
fi

cd "${PROJ_DIR}"

echo "==> 工程：${PROJ_DIR}"
echo "==> 输出目录：${OUT_DIR}"
echo "==> 编译器：$(command -v "${CROSS_CC}")"
echo "==> 构建类型：${BUILD_TYPE}"
echo "==> 并行：-j${JOBS}"

if [[ "${1:-}" == "clean" ]]; then
	rm -rf "${OUT_DIR}"
	echo "==> 已删除 ${OUT_DIR}"
	exit 0
fi

# 每次重新配置，保证 CC / 选项变更生效
cmake -S "${PROJ_DIR}" -B "${OUT_DIR}" \
	-DCMAKE_SYSTEM_NAME=Linux \
	-DCMAKE_SYSTEM_PROCESSOR=arm \
	-DCMAKE_C_COMPILER="${CROSS_CC}" \
	-DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

cmake --build "${OUT_DIR}" -j"${JOBS}"

echo "==> 产物："
ls -lh "${BIN}"
file "${BIN}"

if ! file "${BIN}" | grep -qi 'ARM'; then
	echo "ERROR: ${BIN} 不是 ARM ELF，请确认 CMAKE_C_COMPILER=${CROSS_CC}"
	exit 1
fi

echo "==> OK：可拷到板子运行"
echo "    ./copy_lvgl_build_file.sh"
echo "    或：scp ${BIN} root@<板子IP>:/root/"
