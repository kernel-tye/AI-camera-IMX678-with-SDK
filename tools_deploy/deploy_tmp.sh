#!/bin/sh
# 把刚编出来的 sample_venc 推到板子 /tmp 试跑 (不碰 flash), 顺带把要试的 .om 一起推。
# 在编译 VM 上跑。用法:
#   ./deploy_tmp.sh <sample_venc 路径> [模型.om ...]
#
# 每次都用一个新的文件名(svtestN), 因为覆盖正在运行的二进制会 ETXTBSY。
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=${1:?用法: ./deploy_tmp.sh <sample_venc 路径> [模型.om ...]}
shift || true
NAME=svtest$(date +%H%M%S)
PY=${PY:-python3}

echo "== 1/5 停掉在跑的相机进程 (SIGTERM, 让它自己释放 VB) =="
$PY "$HERE/board.py" stop

echo "== 2/5 释放 8080/554 上残留的 tcpsvd =="
$PY "$HERE/board.py" ports

echo "== 3/5 推二进制 =="
$PY "$HERE/board.py" push "$BIN" "/tmp/$NAME"

for m in "$@"; do
  echo "== 推模型 $(basename "$m") =="
  $PY "$HERE/board.py" sh 'mkdir -p /tmp/model'
  $PY "$HERE/board.py" push "$m" "/tmp/model/$(basename "$m")"
done

echo "== 4/5 启动 =="
$PY "$HERE/board.py" start "/tmp/$NAME"
sleep 12

echo "== 5/5 日志开头 + 冒烟测试 =="
$PY "$HERE/board.py" sh "head -60 /tmp/$NAME.log"
$PY "$HERE/verify.py" || true

echo
echo "跑起来的是 /tmp/$NAME —— 重启就没了。确认没问题后再按 README 固化到 mtd5。"
echo "看 NPU 相关日志:  python3 $HERE/board.py sh \"grep -a '\\[npu\\]' /tmp/$NAME.log\""
