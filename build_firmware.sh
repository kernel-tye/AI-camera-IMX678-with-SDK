#!/bin/bash
# ============================================================================
#  甜源创新 IMX678 相机 —— 一键构建应用层固件 (user 分区 /opt 的 squashfs)
#
#  产物: user.squashfs  —— 烧到 flash 的 "user" 分区 (mtd5)
#  内容: /opt/bin/sample_venc (本次编译) + /opt/ko (内核模块) + /opt/lib
#
#  用法:  ./build_firmware.sh
#  烧写:  见 README.md (交叉编译 mtdwrite.c, 传到板子, mtdwrite user.squashfs /dev/mtd5)
# ============================================================================
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
VENC="$(cd "$HERE/.." && pwd)"                       # mpp/sample/venc (sample_venc.c 在这)
SAMPLE="$(cd "$HERE/../.." && pwd)"                  # mpp/sample
SRC="$(cd "$HERE/../../../.." && pwd)"               # a55_linux/source
SNS="$SRC/mpp/cbb/isp/user/sensor/hi3519dv500/sony_imx678"
SENSOR="SONY_IMX678_MIPI_8M_30FPS_12BIT"             # 必须: 否则默认 os04a10, 出不了图
TC="$HOME/hi3519/SMP_Linux_GCC_musl/gcc-20240819-aarch64-v01c01-linux-musl/aarch64-v01c01-linux-musl-gcc/bin"

[ -d "$TC" ] && export PATH="$TC:$PATH"
command -v aarch64-v01c01-linux-musl-gcc >/dev/null || { echo "!! 工具链不在 PATH: $TC"; exit 1; }
command -v mksquashfs >/dev/null || { echo "!! 需要 mksquashfs (apt install squashfs-tools)"; exit 1; }

echo "[1/4] 编译 IMX678 传感器库 (imx678_sensor_ctl.c/imx678_cmos.c 的改动) ..."
( cd "$SNS" && { make clean >/dev/null 2>&1 || true; } && make )   # → $SRC/out/lib/libsns_imx678.a

echo "[2/4] 编译 sample_venc (sensor=$SENSOR ; 含 sample_comm_vi/isp 改动) ..."
( cd "$SAMPLE" && make SENSOR0_TYPE="$SENSOR" venc )

echo "[3/4] 组装 /opt 目录树 ..."
rm -rf "$HERE/opt_build"
cp -a "$HERE/opt_stage" "$HERE/opt_build"            # ko + lib (known-good)
mkdir -p "$HERE/opt_build/bin"
cp "$VENC/sample_venc" "$HERE/opt_build/bin/sample_venc"
chmod +x "$HERE/opt_build/bin/sample_venc"

# NPU 运行库 + 模型: 直接从 SDK 取(仓库里不放, 省 12MB). libsvp_acl 是静态链进
# sample_venc 的, 但它运行时会 dlopen libsvp_aicpu.so, 所以这几个 .so 必须进 /opt/lib
cp -P "$SRC"/out/lib/libsvp_acl.so "$SRC"/out/lib/libsvp_acl_sec.so \
      "$SRC"/out/lib/libsvp_aicpu.so "$SRC"/out/lib/libprotobuf-c.so* "$HERE/opt_build/lib/"
mkdir -p "$HERE/opt_build/model"
# 厂商模型(从 SDK 取, 仓库不放 —— 单个就好几 MB)
for m in yolov8 yolov7; do
    cp "$SRC/mpp/sample/svp/svp_npu/data/model/$m.om" "$HERE/opt_build/model/"
done
# 自己用 atc 转的模型(小, 就放在仓库 tools_atc/ 里, 见那边的 README)。
# 只列**带 AIPP** 的版本: 不带 AIPP 的输入是 float32, 固件的零拷贝喂法不适用,
# 放进去只会在网页模型列表里挂着骗人。
for m in yunet_face_aipp; do
    if [ -f "$HERE/tools_atc/$m.om" ]; then
        cp "$HERE/tools_atc/$m.om" "$HERE/opt_build/model/"
    else
        echo "  !! 缺 tools_atc/$m.om —— 按 tools_atc/README.md 用 atc 转一个再来"
        exit 1
    fi
done

echo "[4/4] 打包 squashfs (xz, 256K block —— 与板子一致) ..."
rm -f "$HERE/user.squashfs"
mksquashfs "$HERE/opt_build" "$HERE/user.squashfs" -comp xz -b 262144 -noappend -no-progress

SZ=$(stat -c%s "$HERE/user.squashfs")
echo "-----------------------------------------------------------"
echo "  DONE:  $HERE/user.squashfs   ($SZ bytes, mtd5 上限 22020096)"
echo "  sample_venc md5: $(md5sum "$HERE/opt_build/bin/sample_venc" | cut -d' ' -f1)"
echo "  下一步烧写见 README.md"
echo "-----------------------------------------------------------"
