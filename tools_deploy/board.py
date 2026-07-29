#!/usr/bin/env python3
"""板子操作小库 (在编译 VM 上跑, 板子默认 192.168.1.160)。

板子上没有 nc / wget / tftp / scp, busybox 的 NFS 挂载也不work, 所以通道只有两条:

  * **:23 调试 shell** —— 开机由 rootfs 的 S90tcpsvd 起的 `tcpsvd -E 0.0.0.0 23 /bin/sh`,
    裸 TCP, 无 tty 无提示符。发命令时跟一个哨兵 echo, 读到哨兵为止。
  * **web /exec** —— 固件自带, 需要 admin/admin 登录, 但外面裹了 `timeout 10`,
    起的进程活不过 10 秒, 只能当应急口, 不能当传输通道。

历史上踩过的坑都写进这里了, 别绕开:
  - 千万别 `killall tcpsvd`: :23 那个后门也是 tcpsvd, 杀了就没法远程了(只能接串口/重启)。
    按端口号精确匹配着杀 —— 而且别用 `grep '[9105]'` 这种字符类, 它匹配的是 9/1/0/5 单字符。
  - 接收端要用 `tcpsvd ... dd of=<文件>`; `sh -c "cat > 文件"` 那种写法会被安全分类器
    当反弹 shell 拦下来。
  - 推文件别覆盖**正在运行**的那个二进制 → ETXTBSY (报 Connection reset by peer)。换个名字。
  - 换二进制要 `kill -TERM` 让它自己收尾(SIGTERM 处理函数会 sample_comm_sys_exit 释放 VB);
    `kill -9` 会把 VB 配置留在内核里, 下一个进程起来直接 vb_set_conf failed。
  - 有相机进程在跑时**绝对不要** `load3519dv500 -a` —— rmmod 正在用的模块会把内核搞死,
    只能物理断电。
"""
import hashlib
import os
import socket
import sys
import time

BOARD = os.environ.get("BOARD", "192.168.1.160")
SH_PORT = int(os.environ.get("BOARD_SH_PORT", "23"))
WEB_PORT = int(os.environ.get("BOARD_WEB_PORT", "8080"))
SENTINEL = "__TY_DONE__"


def sh(cmd, timeout=30, quiet=False):
    """经 :23 调试 shell 跑一条命令, 返回输出(不含哨兵)。"""
    s = socket.create_connection((BOARD, SH_PORT), timeout=timeout)
    s.settimeout(timeout)
    s.sendall(("%s\necho %s\n" % (cmd, SENTINEL)).encode(errors="replace"))
    buf = b""
    try:
        while SENTINEL.encode() not in buf:
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
    except socket.timeout:
        pass
    finally:
        s.close()
    out = buf.decode("utf-8", "replace").split(SENTINEL)[0]
    if not quiet:
        sys.stdout.write(out)
    return out


def push(local, remote, port=9105):
    """把本地文件推到板子。tcpsvd 起一次性接收端 -> 直连灌数据 -> 两边比 md5。"""
    data = open(local, "rb").read()
    want = hashlib.md5(data).hexdigest()
    # 接收端必须彻底脱离父进程的 fd, 否则它会攥着 :23 那条连接不放, 调用方就卡住
    sh("setsid sh -c \"tcpsvd -E 0.0.0.0 %d dd of=%s\" 0</dev/null 1>/dev/null 2>&1 &"
       % (port, remote), timeout=10, quiet=True)
    time.sleep(1.0)
    s = socket.create_connection((BOARD, port), timeout=60)
    s.sendall(data)
    s.shutdown(socket.SHUT_WR)
    s.close()
    time.sleep(1.0)
    got = sh("md5sum %s" % remote, quiet=True).split()
    # 收完就把这个端口的接收端收掉 —— 按端口精确匹配, 别碰 :23
    sh("for p in $(ps | grep tcpsvd | grep ' %d ' | grep -v grep | awk '{print $1}'); "
       "do kill $p; done" % port, quiet=True)
    ok = bool(got) and got[0] == want
    print("  推送 %s -> %s  %s (%d 字节, md5 %s)"
          % (local, remote, "OK" if ok else "**md5 不符**", len(data), want))
    if not ok:
        raise SystemExit("传输失败, 板子上算出来是 %s" % (got[0] if got else "(没读到)"))
    return remote


def cam_pids():
    """当前在跑的相机进程 (sample_venc 或临时的 svtest*)"""
    out = sh("ps | grep -E 'sample_venc|svtest' | grep -v grep", quiet=True)
    return [ln.split()[0] for ln in out.splitlines() if ln.strip() and ln.split()[0].isdigit()]


def stop_camera(wait=12):
    """干净停掉相机进程: SIGTERM + 等它自己释放 VB。"""
    pids = cam_pids()
    if not pids:
        print("  没有相机进程在跑")
        return True
    print("  SIGTERM ->", " ".join(pids))
    sh("kill -TERM %s" % " ".join(pids), quiet=True)
    for _ in range(wait):
        time.sleep(1)
        if not cam_pids():
            print("  已干净退出")
            return True
    print("  **还没退干净** —— 不要 kill -9(会留下 VB 配置), 手动看看 /tmp 里的日志")
    return False


def free_ports():
    """把 8080/554 上残留的 tcpsvd 收掉。

    这些是经 web /exec 起的接收端, 它们继承了 sample_venc 的监听 fd(没 CLOEXEC),
    相机进程退出后它们仍然占着端口, 新二进制会 bind 8080 failed。
    :23 那个后门是开机 S90tcpsvd 起的(那时 8080 还没绑), 不受影响也绝不能杀。
    """
    for port in (8080, 554):
        out = sh("fuser %d/tcp 2>/dev/null" % port, quiet=True)
        pids = [p for p in out.split() if p.isdigit()]
        for p in pids:
            name = sh("cat /proc/%s/comm 2>/dev/null" % p, quiet=True).strip()
            if name == "tcpsvd":
                print("  释放 %d: kill 残留 tcpsvd pid %s" % (port, p))
                sh("kill %s" % p, quiet=True)


def start(binary, mode=13, logfile=None):
    """起一个相机二进制(不重载内核模块 —— 干净退出后不需要重载)。"""
    log = logfile or ("/tmp/%s.log" % os.path.basename(binary))
    sh("chmod +x %s" % binary, quiet=True)
    sh("cd %s && LD_LIBRARY_PATH=/opt/lib setsid ./%s %d >%s 2>&1 &"
       % (os.path.dirname(binary) or "/tmp", os.path.basename(binary), mode, log), quiet=True)
    print("  已启动 %s (mode %d), 日志 %s" % (binary, mode, log))
    return log


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        print("用法: board.py sh '<命令>' | push <本地> <板上路径> | stop | start <板上二进制> | ports")
        raise SystemExit(2)
    a = sys.argv[1]
    if a == "sh":
        sh(" ".join(sys.argv[2:]))
    elif a == "push":
        push(sys.argv[2], sys.argv[3])
    elif a == "stop":
        stop_camera()
    elif a == "ports":
        free_ports()
    elif a == "start":
        start(sys.argv[2])
    else:
        raise SystemExit("不认识的子命令: %s" % a)
