#!/usr/bin/env python3
"""固件冒烟测试: 登录 -> 逐个打接口 -> 出一张通过/失败表。

换完二进制先跑这个, 再决定要不要固化。板子重启后也跑一遍确认固化生效。

用法: python3 verify.py [板子IP]
"""
import http.cookiejar
import json
import sys
import urllib.request

CAM = "http://%s:8080" % (sys.argv[1] if len(sys.argv) > 1 else "192.168.1.160")
cj = http.cookiejar.CookieJar()
op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))

rows = []


def get(path, timeout=25):
    return op.open(CAM + path, timeout=timeout).read()


def check(name, path, want_json=None, min_bytes=None):
    try:
        body = get(path)
    except Exception as e:
        rows.append((name, path, "**失败**", str(e)[:60]))
        return None
    note = "%d 字节" % len(body)
    ok = True
    j = None
    if want_json:
        try:
            j = json.loads(body.decode("utf-8", "replace"))
            note = ", ".join("%s=%s" % (k, j.get(k)) for k in want_json if k in j)
            miss = [k for k in want_json if k not in j]
            if miss:
                ok, note = False, "缺字段 " + ",".join(miss)
        except ValueError:
            ok, note = False, "不是 JSON"
    if min_bytes and len(body) < min_bytes:
        ok, note = False, "%s (少于 %d)" % (note, min_bytes)
    rows.append((name, path, "OK" if ok else "**失败**", note))
    return j


get("/login?user=admin&pass=admin")

check("状态", "/info", ["mmz"])
check("预览快照 720p", "/snap", min_bytes=20000)
check("4K 抓拍", "/down4k", min_bytes=500000)
check("时间戳/对焦叠加", "/getosd", ["en"])
check("对焦值", "/focus", ["fv", "ok"])
check("曝光", "/exp", ["iso"])
check("直方图", "/hist")
check("屏幕", "/getscr", ["en"])
check("网络", "/getnet", ["ip"])
check("模型列表", "/models", ["cur", "list"])
npu = check("AI 检测", "/npu", ["en", "ready", "kind", "n", "det"])

print("\n%-18s %-14s %-8s %s" % ("项目", "接口", "结果", "说明"))
print("-" * 78)
for r in rows:
    print("%-18s %-14s %-8s %s" % r)

if npu:
    print("\n--- AI 检测细节 ---")
    print("模型 %s (%s), 推理 %s ms, 目标 %s 个"
          % (npu.get("model"), npu.get("kind"), npu.get("ms"), npu.get("n")))
    for i, d in enumerate(npu.get("det", [])):
        kp = d.get("kp") or []
        eyes = (" 双眼 %s %s" % (kp[0], kp[1])) if len(kp) >= 2 else ""
        print("  %d. %s %s%% @ %s,%s %sx%s%s"
              % (i + 1, d.get("n"), d.get("s"), d.get("x"), d.get("y"),
                 d.get("w"), d.get("h"), eyes))
    if npu.get("kind") == "face" and npu.get("n") and not (npu["det"][0].get("kp")):
        print("  ⚠ 人脸模型却没吐关键点 —— 眼控对焦用不了, 查 kps_* 张量是否被认出来")

bad = [r for r in rows if r[2] != "OK"]
print("\n%s" % ("全部通过" if not bad else "有 %d 项失败" % len(bad)))
raise SystemExit(1 if bad else 0)
