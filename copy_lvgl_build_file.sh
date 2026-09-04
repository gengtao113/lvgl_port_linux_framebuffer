#!/usr/bin/env bash
# 将交叉编译产物 demo 拷到 NFS rootfs，便于板上直接运行
# 用法：
#   ./copy_lvgl_build_file.sh
#   NFS_ROOT=/其它/nfs/rootfs ./copy_lvgl_build_file.sh
#   DEST_NAME=lvgl_demo ./copy_lvgl_build_file.sh

set -euo pipefail

PROJ_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${SRC:-${PROJ_DIR}/lvgl_build_output/demo}"
NFS_ROOT="${NFS_ROOT:-/home/gengtao/linux-imx6ull/nfs/rootfs}"
DEST_DIR="${DEST_DIR:-${NFS_ROOT}/root}"
DEST_NAME="${DEST_NAME:-demo}"
DEST="${DEST_DIR}/${DEST_NAME}"

if [[ ! -f "${SRC}" ]]; then
	echo "ERROR: 找不到产物：${SRC}"
	echo "  请先执行：./build_lvgl.sh"
	exit 1
fi

if [[ ! -d "${DEST_DIR}" ]]; then
	echo "ERROR: NFS 目标目录不存在：${DEST_DIR}"
	echo "  可用：NFS_ROOT=/你的/nfs/rootfs ./copy_lvgl_build_file.sh"
	exit 1
fi

cp -f "${SRC}" "${DEST}"
chmod +x "${DEST}"

echo "==> 已拷贝："
ls -lh "${DEST}"
file "${DEST}"
echo "==> 板上运行：/root/${DEST_NAME}"
