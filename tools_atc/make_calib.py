#!/usr/bin/env python3
"""Grab frames from the camera and turn them into int8-quantisation calibration
data for atc.  Using the camera's own frames (same lens, same ISP, same scene
statistics) is the right calibration set for this NPU.

Output: /home/ty/models/calib/*.bgr  (3x640x640 uint8 planar BGR) + list.txt
"""
import io, os, sys, time, urllib.request, http.cookiejar
import numpy as np
from PIL import Image

CAM = "http://192.168.1.160:8080"
N = int(sys.argv[1]) if len(sys.argv) > 1 else 16
SIZE = int(sys.argv[2]) if len(sys.argv) > 2 else 640
OUT = "/home/ty/models/calib"

cj = http.cookiejar.CookieJar()
op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))
op.open(CAM + "/login?user=admin&pass=admin", timeout=10).read()

os.makedirs(OUT, exist_ok=True)
paths = []
for i in range(N):
    jpg = op.open(CAM + "/snap?%d" % time.time_ns(), timeout=20).read()
    im = Image.open(io.BytesIO(jpg)).convert("RGB")
    # letterbox to SIZExSIZE the same way the firmware feeds the NPU: keep
    # aspect, grey pad top/bottom — calibration must match runtime input
    w, h = im.size
    cw, ch = SIZE, max(2, int(round(SIZE * h / w)) & ~1)
    if ch > SIZE:
        ch = SIZE
    im = im.resize((cw, ch), Image.BILINEAR)
    canvas = Image.new("RGB", (SIZE, SIZE), (114, 114, 114))
    canvas.paste(im, (0, (SIZE - ch) // 2))
    a = np.asarray(canvas, dtype=np.uint8)            # HWC RGB
    bgr = a[:, :, ::-1]                               # -> BGR
    planar = np.ascontiguousarray(bgr.transpose(2, 0, 1))   # -> CHW
    p = os.path.join(OUT, "cal_%02d.bgr" % i)
    planar.tofile(p)
    paths.append(p)
    time.sleep(0.4)

with open("/home/ty/models/calib_list.txt", "w") as f:
    f.write("\n".join(paths) + "\n")
print("wrote %d files, each %d bytes" % (len(paths), 3 * SIZE * SIZE))
print("list: /home/ty/models/calib_list.txt")
