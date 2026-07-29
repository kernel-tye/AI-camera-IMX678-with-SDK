#!/usr/bin/env python3
"""土法 C 词法扫描: 剥掉注释和字符串/字符字面量后检查 {} () [] 配平。
编不了的时候至少能挡住"少个大括号 / 注释没闭合 / 字面量没收尾"这类低级错误。
另外报告顶层函数在哪些行开始/结束, 方便核对新加的函数确实是顶层的。"""
import sys

path = sys.argv[1]
src = open(path, "r", encoding="utf-8").read()

out = []          # (char, line)
line = 1
i, n = 0, len(src)
state = "code"
str_start = 0
while i < n:
    c = src[i]
    nxt = src[i + 1] if i + 1 < n else ""
    if c == "\n":
        line += 1
    if state == "code":
        if c == "/" and nxt == "*":
            state, str_start, i = "block", line, i + 2
            continue
        if c == "/" and nxt == "/":
            state, i = "line", i + 2
            continue
        if c == '"':
            state, str_start, i = "str", line, i + 1
            continue
        if c == "'":
            state, str_start, i = "chr", line, i + 1
            continue
        if c in "{}()[]":
            out.append((c, line))
    elif state == "block":
        if c == "*" and nxt == "/":
            state, i = "code", i + 2
            continue
    elif state == "line":
        if c == "\n":
            state = "code"
    elif state in ("str", "chr"):
        if c == "\\":
            i += 2
            if i <= n and src[i - 1 : i] == "\n":
                line += 1
            continue
        if (state == "str" and c == '"') or (state == "chr" and c == "'"):
            state = "code"
    i += 1

if state != "code":
    print("**未闭合的 %s, 从第 %d 行开始**" % (state, str_start))
    raise SystemExit(1)

pairs = {"}": "{", ")": "(", "]": "["}
stack = []
depth_zero_lines = []
bad = 0
for ch, ln in out:
    if ch in "{([":
        stack.append((ch, ln))
    else:
        if not stack:
            print("**第 %d 行多出一个 %s**" % (ln, ch))
            bad += 1
            continue
        op, oln = stack.pop()
        if op != pairs[ch]:
            print("**第 %d 行的 %s 对不上第 %d 行的 %s**" % (ln, ch, oln, op))
            bad += 1
        if ch == "}" and not stack:
            depth_zero_lines.append(ln)

for op, oln in stack:
    print("**第 %d 行的 %s 一直没闭合**" % (oln, op))
    bad += 1

print("括号总数 %d, 顶层块 %d 个, 最后一个收在第 %d 行" %
      (len(out), len(depth_zero_lines), depth_zero_lines[-1] if depth_zero_lines else -1))
print("总行数 %d" % line)
print("结论: %s" % ("**有 %d 处问题**" % bad if bad else "配平 OK"))
raise SystemExit(1 if bad else 0)
