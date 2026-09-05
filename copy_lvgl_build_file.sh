#!/usr/bin/env bash
# 将交叉编译产物 demo + 工程 GIF/ 素材拷到 NFS rootfs，便于板上直接运行
# 用法：
#   ./copy_lvgl_build_file.sh
#   NFS_ROOT=/其它/nfs/rootfs ./copy_lvgl_build_file.sh
#   DEST_NAME=lvgl_demo ./copy_lvgl_build_file.sh
#   COPY_GIF=0 ./copy_lvgl_build_file.sh   # 只拷 demo，不拷 GIF
#
# 素材默认：${PROJ_DIR}/GIF/*.gif → ${DEST_DIR}/（板上 /root/*.gif）
# 与 main.c 中 GIF_PATH "A:/root/susan-….gif" 对齐

set -euo pipefail

PROJ_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${SRC:-${PROJ_DIR}/lvgl_build_output/demo}"
NFS_ROOT="${NFS_ROOT:-/home/gengtao/linux-imx6ull/nfs/rootfs}"
DEST_DIR="${DEST_DIR:-${NFS_ROOT}/root}"
DEST_NAME="${DEST_NAME:-demo}"
DEST="${DEST_DIR}/${DEST_NAME}"
GIF_DIR="${GIF_DIR:-${PROJ_DIR}/GIF}"
COPY_GIF="${COPY_GIF:-1}"

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

echo "==> 已拷贝 demo："
ls -lh "${DEST}"
file "${DEST}"

if [[ "${COPY_GIF}" == "1" ]]; then
	if [[ ! -d "${GIF_DIR}" ]]; then
		echo "WARN: 无 GIF 目录：${GIF_DIR}（跳过素材拷贝）"
	else
		shopt -s nullglob
		gifs=("${GIF_DIR}"/*.gif "${GIF_DIR}"/*.GIF)
		shopt -u nullglob
		if [[ ${#gifs[@]} -eq 0 ]]; then
			echo "WARN: ${GIF_DIR}/ 下没有 .gif（跳过）"
		else
			echo "==> 已拷贝 GIF（→ ${DEST_DIR}/）："
			for g in "${gifs[@]}"; do
				cp -f "${g}" "${DEST_DIR}/"
				ls -lh "${DEST_DIR}/$(basename "${g}")"
			done
		fi
	fi
fi

echo "==> 板上运行：/root/${DEST_NAME}"
echo "    GIF 示例：/root/susan-lu4esm-wallpaper-1492_512.gif"
