#!/usr/bin/env python3
"""抓相机自己的画面做 int8 量化校准集 —— 同一颗镜头、同一套 ISP、同样的场景统计，
校准出来的量化精度最好。

atc 的校准数据有两套规则，这个脚本两套都生成：

  * 不开 AIPP:  --image_list="<输入名>:calib/cal_00.bgr"
        直接给**裸数据文件**，必须 float32、planar CHW BGR、扩展名 .bgr
        （640x640 => 3*640*640*4 = 4915200 字节；给 uint8 会报 data count 不够）
  * 开 AIPP:    --image_list="<输入名>:calib_jpg_list.txt"
        给**列着图片路径的文本文件**，图片只支持 .jpg/.jpeg/.png/.bmp/.jp2，
        解码 + 预处理由 AIPP 自己做；图片尺寸要等于 aipp cfg 里的 src_image_size

两种都按固件喂 NPU 的方式 letterbox（等比缩放 + 上下 114 灰边），校准输入必须和
运行时输入长一个样。

用法: python3 make_calib.py [张数=16] [边长=640] [输出目录=/home/ty/models]
"""
import io
import os
import sys
import time
import http.cookiejar
import urllib.request

import numpy as np
from PIL import Image

CAM = os.environ.get("CAM", "http://192.168.1.160:8080")
N = int(sys.argv[1]) if len(sys.argv) > 1 else 16
SIZE = int(sys.argv[2]) if len(sys.argv) > 2 else 640
ROOT = sys.argv[3] if len(sys.argv) > 3 else "/home/ty/models"

RAW = os.path.join(ROOT, "calib")          # float32 裸数据 (不开 AIPP 用)
JPG = os.path.join(ROOT, "calib_jpg")      # letterbox 后的 JPEG (开 AIPP 用)
os.makedirs(RAW, exist_ok=True)
os.makedirs(JPG, exist_ok=True)

cj = http.cookiejar.CookieJar()
op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))
op.open(CAM + "/login?user=admin&pass=admin", timeout=10).read()


def letterbox(im, size):
    """等比缩到 size 宽、垂直居中、上下补 114 灰 —— 和固件 cam_npu_letterbox 一致"""
    w, h = im.size
    cw, ch = size, max(2, int(round(size * h / w)) & ~1)
    if ch > size:
        ch = size
        cw = max(2, int(round(size * w / h)) & ~1)
    canvas = Image.new("RGB", (size, size), (114, 114, 114))
    canvas.paste(im.resize((cw, ch), Image.BILINEAR), ((size - cw) // 2, (size - ch) // 2))
    return canvas


raws, jpgs = [], []
for i in range(N):
    jpg = op.open(CAM + "/snap?%d" % time.time_ns(), timeout=20).read()
    canvas = letterbox(Image.open(io.BytesIO(jpg)).convert("RGB"), SIZE)

    p = os.path.join(JPG, "cal_%02d.jpg" % i)
    canvas.save(p, quality=95)                       # AIPP 路线: 直接给图片
    jpgs.append(p)

    a = np.asarray(canvas, dtype=np.uint8)           # HWC RGB
    planar = np.ascontiguousarray(a[:, :, ::-1].transpose(2, 0, 1))   # -> CHW BGR
    p = os.path.join(RAW, "cal_%02d.bgr" % i)
    planar.astype(np.float32).tofile(p)              # 无 AIPP 路线: float32 裸数据
    raws.append(p)

    time.sleep(0.4)

lst_raw = os.path.join(ROOT, "calib_list.txt")
lst_jpg = os.path.join(ROOT, "calib_jpg_list.txt")
with open(lst_raw, "w") as f:
    f.write("\n".join(raws) + "\n")
with open(lst_jpg, "w") as f:
    f.write("\n".join(jpgs) + "\n")

print("float32 裸数据 %d 个, 每个 %d 字节 -> %s" % (len(raws), 3 * SIZE * SIZE * 4, RAW))
print("letterbox JPEG %d 个 (%dx%d)      -> %s" % (len(jpgs), SIZE, SIZE, JPG))
print()
print("开 AIPP :  --image_list=\"input:%s\"" % lst_jpg)
print("不开AIPP:  --image_list=\"input:%s\"" % raws[0])
