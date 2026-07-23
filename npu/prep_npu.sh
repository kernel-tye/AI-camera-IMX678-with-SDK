#!/bin/sh
# prep_npu.sh — 在 VM 上为 NPU/YOLO 集成做准备(拷 helper + cam_npu + 改 Makefile)
# 用法: ./prep_npu.sh <SDK .../a55_linux/source>
# 例:   ./prep_npu.sh ~/hi3519/SMP_Linux_GCC_musl/01.software/board/Hi3519DV500_SDK_V1.0.2.0/smp/a55_linux/source
set -e

SRC="$1"
if [ -z "$SRC" ] || [ ! -d "$SRC/mpp/sample/venc" ]; then
    echo "用法: $0 <SDK a55_linux/source 目录>"; exit 1
fi
HERE=$(cd "$(dirname "$0")" && pwd)
VENC="$SRC/mpp/sample/venc"
SVPCOMMON="$SRC/mpp/sample/svp/common"

echo "== 1) 拷 cam_npu.{c,h} 进 venc/ =="
cp -v "$HERE/cam_npu.c" "$HERE/cam_npu.h" "$VENC/"

echo "== 2) 拷 vendor SVP helper 进 venc/(就近, 免改 -I) =="
for f in sample_common_svp_npu.c sample_common_svp_npu.h \
         sample_common_svp_npu_model.c sample_common_svp_npu_model.h; do
    if [ -f "$SVPCOMMON/$f" ]; then
        cp -v "$SVPCOMMON/$f" "$VENC/"
    else
        echo "  !! 缺 $SVPCOMMON/$f —— 确认 SDK 带 svp 示例; 有可能在 svp/npu/ 下"
    fi
done

echo "== 3) 剥掉 model.c 里的 HRNET/VGS 相关函数(依赖 VGS, venc 里不需要且会引额外链) =="
echo "   >> 手动检查: 打开 venc/sample_common_svp_npu_model.c,"
echo "      注释/删除 hrnet_* 及任何 ss_mpi_vgs_* 调用的函数(约在文件尾 ~1382 行起)。"
echo "      (脚本不自动删, 避免误伤; grep 定位:)"
grep -n "hrnet\|vgs\|_vgs_\|ss_mpi_vgs" "$VENC/sample_common_svp_npu_model.c" 2>/dev/null | head -20 || true

echo "== 4) 改 venc/Makefile: TARGET 固定为 sample_venc =="
MK="$VENC/Makefile"
if grep -q 'TARGET := \$(SMP_SRCS' "$MK"; then
    sed -i 's/^TARGET := \$(SMP_SRCS.*/TARGET := sample_venc/' "$MK"
    echo "   已改 TARGET := sample_venc"
else
    echo "   !! 没匹配到原 TARGET 行, 请手动确认 Makefile 里 TARGET := sample_venc"
    grep -n "TARGET" "$MK" | head
fi

echo "== 5) 提醒: Makefile.param 打开 svp 静态库, 加 -lsvp_aicpu, 放 libsvp_aicpu.so =="
echo "   参见 INTEGRATION.md Block H。"
echo ""
echo "== 完成。接着按 INTEGRATION.md 贴 Block A/B/C/E/F/G(Phase1)到 sample_venc.c, 核对 ⚠VERIFY, 再 build。 =="
