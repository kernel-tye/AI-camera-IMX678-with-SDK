#!/usr/bin/env python3
"""把 cam_vis_analyze / cam_vis_blit 的位运算和索引一比一移到 Python 验一遍。
验: 掩膜 bit 打包/解包对得上、斑马纹斜纹周期、梯度门槛映射、铺到 1280x720 不越界。"""
import numpy as np

VIS_W, VIS_H = 640, 360
VIS_STRIDE = VIS_W // 8
DET_W, DET_H = 1280, 720


def analyze(y, stride, w, h, peak, zeb, peak_th, zth):
    pk = bytearray(VIS_STRIDE * VIS_H)
    zb = bytearray(VIS_STRIDE * VIS_H)
    gth = 4 + (100 - peak_th) * 76 // 100
    for r in range(1, h - 1):
        base = r * stride
        for c in range(1, w - 1):
            bit = 0x80 >> (c & 7)
            if zeb and y[base + c] >= zth and (((c + r) >> 3) & 1) == 0:
                zb[r * VIS_STRIDE + (c >> 3)] |= bit
            if peak:
                gx = abs(int(y[base + c + 1]) - int(y[base + c - 1]))
                gy = abs(int(y[base + stride + c]) - int(y[base - stride + c]))
                if gx + gy >= gth:
                    pk[r * VIS_STRIDE + (c >> 3)] |= bit
    return pk, zb, gth


def blit(pk, zb, mw, mh, peak, zeb, peak_col=0xFC00):
    canvas = np.zeros((DET_H, DET_W), dtype=np.uint16)
    sx, sy = max(1, DET_W // mw), max(1, DET_H // mh)
    hits = 0
    for r in range(mh):
        for c in range(mw):
            bit = 0x80 >> (c & 7)
            on_p = peak and (pk[r * VIS_STRIDE + (c >> 3)] & bit)
            on_z = zeb and (zb[r * VIS_STRIDE + (c >> 3)] & bit)
            if not on_p and not on_z:
                continue
            col = peak_col if on_p else 0xFFFF
            px, py = c * sx, r * sy
            assert 0 <= px and px + sx <= DET_W, "越界 x"
            assert 0 <= py and py + sy <= DET_H, "越界 y"
            canvas[py:py + sy, px:px + sx] = col
            hits += 1
    return canvas, hits, sx, sy


FAIL = []


def check(name, got, want):
    ok = got == want
    print("  %-34s got=%-10s want=%-10s %s" % (name, got, want, "OK" if ok else "**FAIL**"))
    if not ok:
        FAIL.append(name)


# ---- 造一帧: stride 比 width 大(模拟 VPSS 对齐), 左黑右白的竖直阶跃 + 一块过曝区 ----
stride = 704                       # 故意 != width, 验 stride 用对了没
h, w = VIS_H, VIS_W
y = bytearray(stride * h)
for r in range(h):
    for c in range(300, w):
        y[r * stride + c] = 255     # 右半边纯白 -> c=299/300 处有强边缘, 且整片过曝
for r in range(100, 140):           # 一块中等亮度区, 不该触发斑马纹
    for c in range(50, 100):
        y[r * stride + c] = 200

print("\n[描边] 在没有其它物体的第 200 行上, 竖直阶跃只该描 c=299,300")
pk, zb, gth = analyze(y, stride, w, h, True, False, 40, 235)
ROW = 200                       # 200 亮度的方块在 100..140 行, 这一行干净
cols = [c for c in range(1, w - 1) if pk[ROW * VIS_STRIDE + (c >> 3)] & (0x80 >> (c & 7))]
check("门槛映射 (灵敏度 40)", gth, 4 + 60 * 76 // 100)
check("干净行上描边的列", cols, [299, 300])
# 方块的上边沿(第 99/100 行)应该被横向梯度 gy 描出来 —— 描边确实是二维的
top = [c for c in range(50, 100) if pk[99 * VIS_STRIDE + (c >> 3)] & (0x80 >> (c & 7))]
check("方块上边沿也被描到", len(top) > 40, True)

print("\n[斑马纹] 过曝区打斜纹, 200 亮度区不打")
pk2, zb2, _ = analyze(y, stride, w, h, False, True, 40, 235)
row = 50
on = [c for c in range(1, w - 1) if zb2[row * VIS_STRIDE + (c >> 3)] & (0x80 >> (c & 7))]
first = next(c for c in range(300, w) if (((c + row) >> 3) & 1) == 0)
check("该行斑马纹起点(按公式)", min(on), first)
check("斜纹周期 8 像素", sorted(set(np.diff(sorted(on)).tolist())), [1, 9])
mid = [c for c in range(50, 100) if zb2[110 * VIS_STRIDE + (c >> 3)] & (0x80 >> (c & 7))]
check("亮度 200 的区域不打纹", len(mid), 0)
# 斜纹要真的"斜": 相邻行的起点应错开
on2 = [c for c in range(1, w - 1) if zb2[(row + 1) * VIS_STRIDE + (c >> 3)] & (0x80 >> (c & 7))]
check("相邻行错开(=斜纹不是竖条)", min(on2) != min(on), True)

print("\n[铺屏] 640x360 掩膜 -> 1280x720 画布")
cv_p, _, sx, sy = blit(pk, zb2, w, h, True, False)      # 只铺描边
cv_z, _, _, _ = blit(pk, zb2, w, h, False, True)        # 只铺斑马纹
cv_b, _, _, _ = blit(pk, zb2, w, h, True, True)         # 两个都铺
check("放大倍数", (sx, sy), (2, 2))
check("描边列 299 -> 画布 598/599 亮", bool(cv_p[ROW * 2, 598]) and bool(cv_p[ROW * 2, 599]), True)
check("描边列 301 -> 画布 602 不亮", int(cv_p[ROW * 2, 602]), 0)
check("一个 bit 铺满 2x2", int(cv_p[ROW * 2 + 1, 599]), 0xFC00)
check("画布没有越界写 (靠 assert)", True, True)
# 描边优先于斑马纹: c=300 两者都命中(300 过曝且是边缘), 合并铺时应取描边色
zb_at_300 = bool(zb2[ROW * VIS_STRIDE + (300 >> 3)] & (0x80 >> (300 & 7)))
check("c=300 处斑马纹也命中", zb_at_300, True)
check("重叠处取描边色", int(cv_b[ROW * 2, 600]), 0xFC00)
check("纯斑马纹处取白色", int(cv_z[row * 2, first * 2]), 0xFFFF)

print("\n================ %s ================" % ("全部通过" if not FAIL else "有 %d 项失败" % len(FAIL)))
for f in FAIL:
    print("  FAIL:", f)
