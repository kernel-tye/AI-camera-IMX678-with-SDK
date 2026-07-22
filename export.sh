#!/bin/sh
# 从一份已改好的 SDK 抽取本项目全部改动到 ./src/ (用来刷新 github 仓库内容)
# 用法: ./export.sh <SDK 的 .../a55_linux/source 路径>
set -e
SRC=${1:?用法: ./export.sh <a55_linux/source 路径>}
HERE=$(cd "$(dirname "$0")" && pwd)
DST=$HERE/src
rm -rf "$DST"; mkdir -p "$DST"
grep -v '^#' "$HERE/MANIFEST" | while read -r p; do
  [ -z "$p" ] && continue
  mkdir -p "$DST/$(dirname "$p")"
  cp -a "$SRC/$p" "$DST/$(dirname "$p")/"
done
find "$DST" -name '*.bak-*' -delete    # 去掉传感器目录里的调试备份
echo "已导出 -> $DST"
