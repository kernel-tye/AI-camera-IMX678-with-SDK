#!/usr/bin/env python3
"""把 sample_venc.c 里新写的 YuNet 解码器 (cam_npu_tensor / cam_t_get /
cam_npu_parse_yunet / cam_lb_x / cam_lb_y / cam_face_iou) 一比一移植到 Python,
用合成张量验证索引数学。板子不在手边时这是唯一能真跑的验证。

验的是: 三种张量摆法的取值、格点 col/row 推导、letterbox 反算、候选上限的
"顶掉最低分"、贪心 NMS。验不了: YuNet 解码公式本身是否符合模型语义
(那要真模型跑一遍)。
"""
import math

# ---- 与固件一致的常量 ----
MW = MH = 640          # 模型输入
CW, CH = 640, 360      # 内容区 (等比 16:9)
LB_Y = 140             # 内容区在输入里的 y 偏移
NPU_KPS = 5
NPU_MAX_DET = 16
YUNET_CAND = 64
STRIDES = (8, 16, 32)


# ---- cam_lb_x / cam_lb_y ----
def lb_x(x):
    v = int(x * 1000.0 / CW)
    return 0 if v < 0 else (1000 if v > 1000 else v)


def lb_y(y):
    v = int((y - LB_Y) * 1000.0 / CH)
    return 0 if v < 0 else (1000 if v > 1000 else v)


# ---- cam_tensor / cam_npu_tensor / cam_t_get ----
class Tensor(object):
    """d = float 列表; stride 以字节计 (和 svp_acl 一样), fs = stride/4"""

    def __init__(self, data, stride_bytes, size_bytes, na, per):
        self.d = data
        self.fs = stride_bytes // 4
        self.rows = size_bytes // stride_bytes
        self.per = per
        if self.rows <= 1:
            self.layout = 0
            self.bad = self.fs < na * per
        elif self.rows >= na:
            self.layout = 1
            self.bad = self.fs < per
        else:
            self.layout = 2
            self.bad = self.rows < per or self.fs < na

    def get(self, i, k):
        if self.layout == 0:
            return self.d[i * self.per + k]
        if self.layout == 1:
            return self.d[i * self.fs + k]
        return self.d[k * self.fs + i]


def iou(a, b):
    ix0, iy0 = max(a[0], b[0]), max(a[1], b[1])
    ix1, iy1 = min(a[2], b[2]), min(a[3], b[3])
    iw, ih = ix1 - ix0, iy1 - iy0
    if iw <= 0.0 or ih <= 0.0:
        return 0.0
    inter = iw * ih
    uni = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter
    return inter / uni if uni > 0.0 else 0.0


def parse_yunet(scales, score_pct, nms_pct):
    """scales: {stride: (Tensor cls, obj, bbox, kps or None)}"""
    thr = score_pct / 100.0
    iou_th = nms_pct / 100.0
    cand = []          # (x0,y0,x1,y1,sc,kx[],ky[])
    for s in STRIDES:
        if s not in scales:
            continue
        tc, to, tb, tk = scales[s]
        cols, rws = MW // s, MH // s
        na = cols * rws
        for t in (tc, to, tb):
            assert not t.bad, "张量尺寸自检不过 (固件里会 continue 跳过该尺度)"
        has_kp = tk is not None and not tk.bad
        for i in range(na):
            cs = min(max(tc.get(i, 0), 0.0), 1.0)
            os_ = min(max(to.get(i, 0), 0.0), 1.0)
            sc = math.sqrt(cs * os_)
            if sc < thr:
                continue
            col, row = i % cols, i // cols
            cx = (col + tb.get(i, 0)) * s
            cy = (row + tb.get(i, 1)) * s
            w = math.exp(tb.get(i, 2)) * s
            h = math.exp(tb.get(i, 3)) * s
            if not (w > 1.0 and h > 1.0 and w <= MW * 2 and h <= MH * 2):
                continue
            kx = ky = None
            if has_kp:
                kx = [(col + tk.get(i, 2 * k)) * s for k in range(NPU_KPS)]
                ky = [(row + tk.get(i, 2 * k + 1)) * s for k in range(NPU_KPS)]
            item = [cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, sc, kx, ky]
            if len(cand) >= YUNET_CAND:          # 顶掉分最低的那个
                worst = min(range(len(cand)), key=lambda j: cand[j][4])
                if cand[worst][4] >= sc:
                    continue
                cand[worst] = item
            else:
                cand.append(item)

    used = [False] * len(cand)
    out = []
    while len(out) < NPU_MAX_DET:
        best, bs = -1, 0.0
        for i in range(len(cand)):
            if not used[i] and cand[i][4] > bs:
                bs, best = cand[i][4], i
        if best < 0:
            break
        used[best] = True
        for i in range(len(cand)):
            if not used[i] and iou(cand[best], cand[i]) > iou_th:
                used[i] = True
        c = cand[best]
        box = {
            "x0": lb_x(c[0]), "y0": lb_y(c[1]), "x1": lb_x(c[2]), "y1": lb_y(c[3]),
            "score": min(int(c[4] * 100.0 + 0.5), 100),
            "kp": [(lb_x(c[5][k]), lb_y(c[6][k])) for k in range(NPU_KPS)] if c[5] else [],
        }
        out.append(box)
    return out


# =============== 造张量 ===============
def blank(na, per, layout):
    """按指定摆法造一块全 0 的张量; 返回 (list, setter, Tensor 工厂参数)"""
    if layout == 0:
        d = [0.0] * (na * per)
        return d, (lambda i, k, v: d.__setitem__(i * per + k, v)), (na * per * 4, na * per * 4)
    if layout == 1:
        fs = per                      # stride 就是 per 个 float
        d = [0.0] * (na * fs)
        return d, (lambda i, k, v: d.__setitem__(i * fs + k, v)), (fs * 4, na * fs * 4)
    fs = na                           # 每通道一行
    d = [0.0] * (per * fs)
    return d, (lambda i, k, v: d.__setitem__(k * fs + i, v)), (fs * 4, per * fs * 4)


def make_scale(s, faces, layout_bo, layout_cls):
    """faces: [(cx,cy,w,h,cls,obj,[(kx,ky)x5] or None)] —— 全部在模型输入坐标系"""
    cols, rws = MW // s, MH // s
    na = cols * rws
    dc, sc_set, cpar = blank(na, 1, layout_cls)
    do, ob_set, opar = blank(na, 1, layout_cls)
    db, bb_set, bpar = blank(na, 4, layout_bo)
    dk, kp_set, kpar = blank(na, NPU_KPS * 2, layout_bo)
    for (cx, cy, w, h, cls, obj, kps) in faces:
        col, row = int(cx // s), int(cy // s)
        i = row * cols + col
        sc_set(i, 0, cls)
        ob_set(i, 0, obj)
        bb_set(i, 0, cx / s - col)
        bb_set(i, 1, cy / s - row)
        bb_set(i, 2, math.log(w / s))
        bb_set(i, 3, math.log(h / s))
        if kps:
            for k, (kx, ky) in enumerate(kps):
                kp_set(i, 2 * k, kx / s - col)
                kp_set(i, 2 * k + 1, ky / s - row)
    return (Tensor(dc, cpar[0], cpar[1], na, 1),
            Tensor(do, opar[0], opar[1], na, 1),
            Tensor(db, bpar[0], bpar[1], na, 4),
            Tensor(dk, kpar[0], kpar[1], na, NPU_KPS * 2))


# =============== 用例 ===============
FAIL = []


def check(name, got, want, tol=1):
    ok = abs(got - want) <= tol
    print("  %-28s got=%-6s want=%-6s %s" % (name, got, want, "OK" if ok else "**FAIL**"))
    if not ok:
        FAIL.append(name)


def test_one_face(layout_bo, layout_cls, tag):
    print("\n[张量摆法 bbox/kps=%d, cls/obj=%d] %s" % (layout_bo, layout_cls, tag))
    # 脸: 中心(320,300) 80x100 -> x0..y1 = 280,250,360,350; 双眼 (300,285) (340,285)
    kps = [(300, 285), (340, 285), (320, 305), (302, 325), (338, 325)]
    faces = [(320, 300, 80, 100, 0.95, 0.98, kps)]
    sc = {16: make_scale(16, faces, layout_bo, layout_cls)}
    out = parse_yunet(sc, 35, 45)
    print("  检出 %d 个" % len(out))
    if not out:
        FAIL.append(tag + ":无检出")
        return
    b = out[0]
    check("x0 (千分比)", b["x0"], lb_x(280))
    check("y0 (千分比)", b["y0"], lb_y(250))
    check("x1 (千分比)", b["x1"], lb_x(360))
    check("y1 (千分比)", b["y1"], lb_y(350))
    check("置信度 %", b["score"], int(math.sqrt(0.95 * 0.98) * 100 + 0.5))
    check("右眼 x", b["kp"][0][0], lb_x(300))
    check("右眼 y", b["kp"][0][1], lb_y(285))
    check("左眼 x", b["kp"][1][0], lb_x(340))
    # 眼中点 = 对焦窗中心, 这是眼控对焦真正用到的量
    exp_mid = (lb_x(300) + lb_x(340)) // 2
    check("双眼中点 x", (b["kp"][0][0] + b["kp"][1][0]) // 2, exp_mid)


def test_nms():
    print("\n[NMS] 一张脸被三个格点重复检出 + 另一张远处的脸")
    kps = [(300, 285), (340, 285), (320, 305), (302, 325), (338, 325)]
    faces = [
        (320, 300, 80, 100, 0.95, 0.98, kps),      # 主
        (326, 306, 82, 102, 0.80, 0.85, kps),      # 重叠 -> 应被压掉
        (312, 294, 78, 98, 0.75, 0.80, kps),       # 重叠 -> 应被压掉
        (120, 200, 60, 70, 0.90, 0.90, kps),       # 远处 -> 应保留
    ]
    out = parse_yunet({16: make_scale(16, faces, 1, 1)}, 35, 45)
    check("NMS 后个数", len(out), 2, 0)
    if len(out) >= 1:
        check("最高分排第一", out[0]["score"], int(math.sqrt(0.95 * 0.98) * 100 + 0.5))


def test_multiscale():
    print("\n[三尺度] 8/16/32 各放一张不重叠的脸")
    kps = [(300, 285), (340, 285), (320, 305), (302, 325), (338, 325)]
    sc = {
        8:  make_scale(8,  [(100, 200, 40, 48, 0.9, 0.9, kps)], 1, 1),
        16: make_scale(16, [(320, 300, 80, 100, 0.95, 0.98, kps)], 1, 1),
        32: make_scale(32, [(520, 400, 160, 180, 0.85, 0.9, kps)], 0, 0),
    }
    out = parse_yunet(sc, 35, 45)
    check("三尺度各出一个", len(out), 3, 0)


def test_cand_cap():
    print("\n[候选上限] 70 个候选, 分数递增, 上限 64 —— 最高的那批必须留下")
    kps = None
    faces = []
    for n in range(70):                       # 摆成一行, 互不重叠
        cx = 40 + n * 8
        faces.append((cx, 80, 6, 6, 0.30 + n * 0.01, 1.0, kps))
    # 注意: cx 要落在不同格点上 -> 用 stride 8, 每格 8 像素
    out = parse_yunet({8: make_scale(8, faces, 1, 1)}, 35, 45)
    got = sorted([b["score"] for b in out], reverse=True)
    want = sorted([int(math.sqrt(min(max(0.30 + n * 0.01, 0), 1)) * 100 + 0.5)
                   for n in range(70)], reverse=True)[:NPU_MAX_DET]
    check("返回个数", len(out), NPU_MAX_DET, 0)
    print("  最高 16 个分数 got=%s" % got)
    print("                want=%s" % want)
    if got != want:
        FAIL.append("候选上限: 最高分被顶掉了")


test_one_face(1, 1, "每格点一行")
test_one_face(0, 0, "单行连续")
test_one_face(2, 0, "bbox/kps 每通道一行, cls/obj 单行")
test_nms()
test_multiscale()
test_cand_cap()

print("\n================ %s ================" % ("全部通过" if not FAIL else "有 %d 项失败" % len(FAIL)))
for f in FAIL:
    print("  FAIL:", f)
