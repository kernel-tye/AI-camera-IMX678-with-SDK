#!/bin/sh
# 把 ./src/ 的改动覆盖到一份纯净的厂商 SDK (先解压 Hi3519DV500 SDK V1.0.2.0)
# 用法: ./apply.sh <SDK 的 .../a55_linux/source 路径>
set -e
SRC=${1:?用法: ./apply.sh <a55_linux/source 路径>}
HERE=$(cd "$(dirname "$0")" && pwd)
[ -d "$HERE/src" ] || { echo '缺 src/ —— 先 ./export.sh 或从仓库拉取'; exit 1; }
grep -v '^#' "$HERE/MANIFEST" | while read -r p; do
  [ -z "$p" ] && continue
  mkdir -p "$SRC/$(dirname "$p")"
  cp -a "$HERE/src/$p" "$SRC/$(dirname "$p")/"
  echo "  覆盖 $p"
done
echo "完成. 下一步: cd $SRC/mpp/sample/venc/tianyuan_pkg && ./build_firmware.sh"
