#!/usr/bin/env bash
# L3：交叉编译本目录 → ARM 可执行文件
# 产物统一在：lvgl_build_output/（.o + demo）
# 用法：
#   ./build_lvgl
#   ./build_lvgl clean   # 仅 clean
#   JOBS=4 ./build_lvgl  # 指定并行数
#
# 说明：官方 Makefile 默认 CFLAGS 含 -Wshift-negative-value 等新警告选项，
# Linaro 4.9 不支持。本脚本用兼容 CFLAGS 覆盖（不改 Makefile 里的默认 CFLAGS 列表）。

set -euo pipefail

PROJ_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${OUT_DIR:-${PROJ_DIR}/lvgl_build_output}"
BIN="${OUT_DIR}/demo"

TOOLCHAIN_BIN="${TOOLCHAIN_BIN:-/usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin}"
CROSS_CC="${CROSS_CC:-arm-linux-gnueabihf-gcc}"
JOBS="${JOBS:-$(nproc)}"

# GCC 4.9 可用的精简 CFLAGS（覆盖 Makefile 里偏新的 -W*；需 C99）
CROSS_CFLAGS="${CROSS_CFLAGS:--std=gnu99 -O3 -g0 -I${PROJ_DIR}/ -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-unused-function -fno-strict-aliasing}"

export PATH="${TOOLCHAIN_BIN}:${PATH}"

if ! command -v "${CROSS_CC}" >/dev/null 2>&1; then
	echo "ERROR: 找不到 ${CROSS_CC}"
	echo "  请检查工具链目录：${TOOLCHAIN_BIN}"
	echo "  或：TOOLCHAIN_BIN=/你的路径/bin ./build_lvgl"
	exit 1
fi

if [[ ! -f "${PROJ_DIR}/Makefile" ]]; then
	echo "ERROR: 本目录无 Makefile：${PROJ_DIR}"
	exit 1
fi

cd "${PROJ_DIR}"

echo "==> 工程：${PROJ_DIR}"
echo "==> 输出目录：${OUT_DIR}"
echo "==> 编译器：$(command -v "${CROSS_CC}")"
echo "==> CFLAGS：${CROSS_CFLAGS}"
echo "==> 并行：-j${JOBS}"

make clean BUILD_DIR="${OUT_DIR}"
if [[ "${1:-}" == "clean" ]]; then
	echo "==> 仅 clean，结束"
	exit 0
fi

make CC="${CROSS_CC}" CFLAGS="${CROSS_CFLAGS}" BUILD_DIR="${OUT_DIR}" -j"${JOBS}"

echo "==> 产物："
ls -lh "${BIN}"
file "${BIN}"

if ! file "${BIN}" | grep -qi 'ARM'; then
	echo "ERROR: ${BIN} 不是 ARM ELF，请确认 CC=${CROSS_CC}"
	exit 1
fi

echo "==> OK：可拷到板子运行（见 ../gengtao_doc/01-LVGL移植落地步骤-framebuffer.md L4）"
echo "    scp ${BIN} root@<板子IP>:/root/"
