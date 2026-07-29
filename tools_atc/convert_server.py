#!/usr/bin/env python3
"""VM 上的自助转模型页面 —— 浏览器拖个 .onnx 进来, 转成 .om, 直接推到相机。

为什么要有它: atc 只能在 x86 上跑(板子上转不了, .om 是 PICO 格式的离线产物),
所以模型转换天然要经过这台 VM。而往 VM 拷文件是整个流程里最烦的一步 ——
这个页面把两头的拷贝都消掉: 上传走浏览器, 产物直接推到相机的 /tmp/model 并热切过去。

顺带把 tools_atc/README.md 里那串坑**固化进代码**, 不用再记:
  * 开 AIPP  -> --image_list 要给"列 JPEG 路径的文本文件"
  * 不开AIPP -> --image_list 要给 float32 裸数据文件(.bgr)
  * aipp cfg 里**不能写注释**(atc 见到 ;; 直接报 Unknow ;;)
  * src_image_size 必须跟模型输入尺寸一致 -> 这里按 shape 自动生成
  * --soc_version 只有 Hi3516DV500 / Hi3519DV500

跑法:  python3 convert_server.py [端口=8090]
然后浏览器开  http://<VM 的 IP>:8090/

⚠ 这是个内网实验工具: 没有登录, 且会对上传的文件跑编译器。只在自己的局域网里开。
"""
import html
import json
import os
import re
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.environ.get("CONVERT_WORK", "/home/ty/models/webconv")
ATC_ENV = os.environ.get("ATC_ENV", "/home/ty/atc_env.sh")
CAM = os.environ.get("CAM", "http://192.168.1.160:8080")
SOC = "Hi3519DV500"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8090

os.makedirs(WORK, exist_ok=True)

JOB = {"running": False, "log": os.path.join(WORK, "convert.log"),
       "om": None, "rc": None, "stage": "空闲"}
LOCK = threading.Lock()


# ---------------------------------------------------------------- multipart
def parse_multipart(body, boundary):
    """够用的 multipart 解析: 返回 {字段名: (文件名 or None, 原始字节)}。
    没用 cgi.FieldStorage —— 它在新版 python 里已经删了。"""
    out = {}
    sep = b"--" + boundary
    for part in body.split(sep):
        if not part.strip() or part.strip() == b"--":
            continue
        head, _, data = part.partition(b"\r\n\r\n")
        if not _:
            continue
        m = re.search(br'name="([^"]*)"', head)
        if not m:
            continue
        name = m.group(1).decode()
        fm = re.search(br'filename="([^"]*)"', head)
        out[name] = (fm.group(1).decode() if fm else None, data.rstrip(b"\r\n"))
    return out


# ---------------------------------------------------------------- 转换流程
def log_write(f, s):
    f.write(s if s.endswith("\n") else s + "\n")
    f.flush()


def aipp_cfg(path, w, h, fmt):
    """按模型输入尺寸生成 AIPP 配置。**一行注释都不能有** —— atc 的解析器见到
    ';;' 开头会直接报 Unknow ;;; rbuv_swap_switch 这个 token 本版也不认。
    矩阵是 BT.601 limited range 定点 x256, 输出通道顺序 BGR。"""
    with open(path, "w") as f:
        f.write("aipp_op {\n")
        for k, v in (("aipp_mode", "static"), ("related_input_rank", 0),
                     ("input_format", fmt), ("src_image_size_w", w),
                     ("src_image_size_h", h), ("crop", "false"),
                     ("csc_switch", "true"),
                     ("matrix_r0c0", 298), ("matrix_r0c1", 516), ("matrix_r0c2", 0),
                     ("matrix_r1c0", 298), ("matrix_r1c1", -100), ("matrix_r1c2", -208),
                     ("matrix_r2c0", 298), ("matrix_r2c1", 0), ("matrix_r2c2", 409),
                     ("input_bias_0", 16), ("input_bias_1", 128), ("input_bias_2", 128),
                     ("mean_chn_0", 0), ("mean_chn_1", 0), ("mean_chn_2", 0),
                     ("min_chn_0", "0.0"), ("min_chn_1", "0.0"), ("min_chn_2", "0.0"),
                     ("var_reci_chn_0", "1.0"), ("var_reci_chn_1", "1.0"),
                     ("var_reci_chn_2", "1.0")):
            f.write("  %s: %s\n" % (k, v))
        f.write("}\n")


def run(cmd, f, env_src=None):
    """跑一条命令, 输出实时写进日志文件(页面轮询这个文件)。"""
    shell = "source %s >/dev/null 2>&1; %s" % (env_src, cmd) if env_src else cmd
    log_write(f, "$ " + cmd)
    p = subprocess.Popen(["bash", "-c", shell], stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT)
    for line in iter(p.stdout.readline, b""):
        f.write(line.decode("utf-8", "replace"))
        f.flush()
    p.wait()
    return p.returncode


def convert_job(onnx, name, inp, shape, use_aipp, ncalib, fmt):
    f = open(JOB["log"], "w")
    ok = False
    try:
        dims = [int(x) for x in shape.replace(" ", "").split(",")]
        h, w = dims[-2], dims[-1]
        log_write(f, "模型 %s  输入 %s:%s  (%dx%d)  AIPP=%s"
                     % (name, inp, shape, w, h, "开" if use_aipp else "关"))

        # 1) 校准数据 —— 用相机自己的画面, 同一颗镜头同一套 ISP, 量化精度最好
        with LOCK:
            JOB["stage"] = "抓相机帧做校准集"
        if run("python3 %s/make_calib.py %d %d %s"
               % (HERE, ncalib, w, WORK), f) != 0:
            log_write(f, "!! 抓校准帧失败 —— 相机 %s 通不通? 没有校准数据 atc 会直接拒绝"
                         "(这颗 NPU 跑 int8, 必须量化)" % CAM)
            return

        # 2) 两套校准数据规则完全不同, 按 AIPP 开关选对的那套
        if use_aipp:
            image_list = os.path.join(WORK, "calib_jpg_list.txt")
            cfg = os.path.join(WORK, "aipp_gen.cfg")
            aipp_cfg(cfg, w, h, fmt)
            extra = "--insert_op_conf=%s " % cfg
            log_write(f, "AIPP: NPU 片上做 %s -> BGR, 输入保持 uint8 YUV, "
                         "固件可以零拷贝喂 VPSS 扩展通道的帧" % fmt)
        else:
            image_list = os.path.join(WORK, "calib", "cal_00.bgr")
            extra = ""
            log_write(f, "不开 AIPP: 模型输入会是 float32 —— 注意固件的零拷贝喂法"
                         "不适用, 得先在 A55 上转换(很贵)")

        # 3) atc
        with LOCK:
            JOB["stage"] = "atc 转换中(要几分钟)"
        out = os.path.join(WORK, name)
        cmd = ('atc --model=%s --framework=5 --output=%s --soc_version=%s '
               '--input_shape="%s:%s" %s--image_list="%s:%s"'
               % (onnx, out, SOC, inp, shape, extra, inp, image_list))
        rc = run(cmd, f, env_src=ATC_ENV)
        om = out + ".om"
        if rc == 0 and os.path.exists(om):
            ok = True
            with LOCK:
                JOB["om"] = om
            log_write(f, "\n✅ 转换成功: %s (%d 字节)" % (om, os.path.getsize(om)))
            log_write(f, "下一步: 点「推到相机」直接试; 要让它重启还在, "
                         "得把 .om 放进 tools_atc/ 并加进 build_firmware.sh 的清单再固化。")
        else:
            log_write(f, "\n❌ atc 失败 (rc=%d)。常见原因: 输入名写错"
                         "(看上面日志里 atc 报的实际名字)、shape 不对、"
                         "或者这个模型有 NPU 不支持的算子。" % rc)
    except Exception as e:
        log_write(f, "!! 出错: %r" % e)
    finally:
        with LOCK:
            JOB["running"] = False
            JOB["rc"] = 0 if ok else 1
            JOB["stage"] = "完成" if ok else "失败"
        f.close()


def deploy_job(om):
    """把 .om 推到相机 /tmp/model 并热切过去 —— 复用 tools_deploy/board.py 那套
    (:23 调试通道, 已经把踩过的坑都写进去了)。"""
    f = open(JOB["log"], "a")
    try:
        with LOCK:
            JOB["stage"] = "推到相机"
        sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools_deploy"))
        import board
        base = os.path.basename(om)
        board.sh("mkdir -p /tmp/model", quiet=True)
        log_write(f, "\n推 %s -> 板子 /tmp/model/" % base)
        board.push(om, "/tmp/model/" + base)
        import urllib.request
        import http.cookiejar
        cj = http.cookiejar.CookieJar()
        op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))
        op.open(CAM + "/login?user=admin&pass=admin", timeout=10).read()
        op.open(CAM + "/setnpu?model=" + base, timeout=60).read()
        time.sleep(3)
        st = op.open(CAM + "/npu", timeout=15).read().decode()
        log_write(f, "相机已切过去: " + st[:300])
        log_write(f, "\n注意: /tmp 重启就没了。要常驻得把 .om 放进 tools_atc/ "
                     "并加进 build_firmware.sh 的清单, 重新固化。")
    except Exception as e:
        log_write(f, "!! 推送失败: %r" % e)
    finally:
        with LOCK:
            JOB["running"] = False
            JOB["stage"] = "完成"
        f.close()


# ---------------------------------------------------------------- 页面
PAGE = """<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>转模型 - Hi3519DV500 NPU</title><style>
*{box-sizing:border-box}
body{font-family:-apple-system,"Segoe UI",Roboto,"Helvetica Neue",sans-serif;
 margin:0;padding:24px;background:#14161a;color:#e6e8eb;line-height:1.6}
.wrap{max-width:860px;margin:0 auto}
h1{font-size:20px;margin:0 0 4px}
.sub{color:#9aa3ad;font-size:13px;margin-bottom:20px}
.card{background:#1c1f24;border:1px solid #2a2f36;border-radius:10px;padding:18px;margin-bottom:16px}
label{display:block;font-size:13px;color:#9aa3ad;margin:12px 0 4px}
input[type=text],input[type=number],select{width:100%;padding:8px 10px;background:#14161a;
 border:1px solid #333a42;border-radius:6px;color:#e6e8eb;font-size:14px;font-family:inherit}
input[type=file]{width:100%;padding:10px;background:#14161a;border:1px dashed #3a424b;
 border-radius:6px;color:#9aa3ad;font-size:13px}
.row{display:flex;gap:14px;flex-wrap:wrap}.row>div{flex:1;min-width:150px}
button{background:#2f6feb;color:#fff;border:0;border-radius:6px;padding:10px 18px;
 font-size:14px;font-weight:600;cursor:pointer;font-family:inherit}
button:disabled{background:#39404a;color:#79828c;cursor:not-allowed}
button.alt{background:#2a2f36;border:1px solid #3a424b}
.chk{display:flex;align-items:flex-start;gap:9px;margin-top:14px}
.chk input{margin-top:5px}.chk span{font-size:13px;color:#9aa3ad}
.chk b{color:#e6e8eb;font-weight:600}
pre{background:#0e1014;border:1px solid #2a2f36;border-radius:8px;padding:14px;
 font-size:12px;line-height:1.5;max-height:420px;overflow:auto;white-space:pre-wrap;
 word-break:break-all;margin:0;font-family:ui-monospace,Menlo,Consolas,monospace}
.stage{font-size:13px;color:#7fd18a;margin-bottom:10px}
.stage.err{color:#f2777a}
.note{font-size:12px;color:#79828c;margin-top:14px;border-top:1px solid #2a2f36;padding-top:12px}
a{color:#6ea8ff}
</style></head><body><div class="wrap">
<h1>转模型 → Hi3519DV500 NPU</h1>
<div class="sub">上传 ONNX，用相机自己的画面做 int8 量化校准，转成 <code>.om</code> 后可直接推到相机试跑。</div>

<div class="card"><form id="f">
  <label>ONNX 模型文件</label>
  <input type="file" name="onnx" accept=".onnx" required>
  <div class="row">
    <div><label>输出名（生成 &lt;名字&gt;.om）</label>
      <input type="text" name="name" value="my_model" required></div>
    <div><label>输入张量名</label>
      <input type="text" name="inp" value="input" required></div>
  </div>
  <div class="row">
    <div><label>输入 shape</label>
      <input type="text" name="shape" value="1,3,640,640" required></div>
    <div><label>校准帧数（从相机抓）</label>
      <input type="number" name="ncalib" value="16" min="1" max="64"></div>
  </div>
  <div class="chk">
    <input type="checkbox" name="aipp" id="aipp" checked>
    <span><b>开 AIPP（强烈建议）</b><br>
      让 NPU 片上做 YUV→BGR，模型输入保持 uint8 YUV —— 固件就能把 VPSS 扩展通道的帧
      <b>零拷贝</b>喂进去。关掉的话输入是 float32，得在 A55 上每帧转 4.9MB，很贵。</span>
  </div>
  <label>扩展通道像素格式（AIPP 用）</label>
  <select name="fmt">
    <option value="YVU420SP">YVU420SP（本相机 VPSS 输出的就是这个）</option>
    <option value="YUV420SP">YUV420SP</option>
  </select>
  <div style="margin-top:18px"><button id="go" type="submit">开始转换</button></div>
</form></div>

<div class="card">
  <div class="stage" id="stage">空闲</div>
  <pre id="log">（日志会显示在这里）</pre>
  <div style="margin-top:14px;display:flex;gap:10px;flex-wrap:wrap">
    <button class="alt" id="dl" disabled>下载 .om</button>
    <button id="dep" disabled>推到相机并切过去</button>
  </div>
  <div class="note">
    推到相机是放进 <code>/tmp/model</code> —— <b>重启就没了</b>。确认好用之后，把
    <code>.om</code> 放进仓库 <code>tools_atc/</code> 并加进
    <code>build_firmware.sh</code> 的模型清单，重新固化才会常驻。
  </div>
</div>

<script>
var busy=false;
function poll(){
  fetch('/status').then(function(r){return r.json()}).then(function(j){
    document.getElementById('stage').textContent=j.stage;
    document.getElementById('stage').className='stage'+(j.rc===1?' err':'');
    document.getElementById('go').disabled=j.running;
    document.getElementById('dl').disabled=!j.om;
    document.getElementById('dep').disabled=!j.om||j.running;
    busy=j.running;
    return fetch('/log');
  }).then(function(r){return r.text()}).then(function(t){
    var p=document.getElementById('log');
    var bottom=p.scrollTop+p.clientHeight>=p.scrollHeight-40;
    if(t){p.textContent=t; if(bottom)p.scrollTop=p.scrollHeight;}
  })['catch'](function(){});
}
setInterval(poll,1500); poll();

document.getElementById('f').onsubmit=function(e){
  e.preventDefault();
  document.getElementById('go').disabled=true;
  document.getElementById('log').textContent='上传中…';
  fetch('/convert',{method:'POST',body:new FormData(this)})
    .then(function(){setTimeout(poll,600)});
};
document.getElementById('dl').onclick=function(){location.href='/download'};
document.getElementById('dep').onclick=function(){
  this.disabled=true;
  fetch('/deploy',{method:'POST'}).then(function(){setTimeout(poll,600)});
};
</script></div></body></html>"""


class H(BaseHTTPRequestHandler):
    def _send(self, code, ctype, body):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path == "/":
            self._send(200, "text/html; charset=utf-8", PAGE)
        elif self.path.startswith("/status"):
            with LOCK:
                j = dict(JOB)
            j["om"] = os.path.basename(j["om"]) if j["om"] else None
            self._send(200, "application/json", json.dumps(j))
        elif self.path.startswith("/log"):
            try:
                t = open(JOB["log"]).read()[-60000:]
            except Exception:
                t = ""
            self._send(200, "text/plain; charset=utf-8", t)
        elif self.path.startswith("/download"):
            with LOCK:
                om = JOB["om"]
            if not om or not os.path.exists(om):
                return self._send(404, "text/plain", "还没有产物")
            body = open(om, "rb").read()
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Disposition",
                             'attachment; filename="%s"' % os.path.basename(om))
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self._send(404, "text/plain", "no")

    def do_POST(self):
        with LOCK:
            if JOB["running"]:
                return self._send(409, "text/plain", "还有任务在跑")

        if self.path.startswith("/deploy"):
            with LOCK:
                om = JOB["om"]
                if not om:
                    return self._send(400, "text/plain", "还没有产物")
                JOB["running"] = True
            threading.Thread(target=deploy_job, args=(om,), daemon=True).start()
            return self._send(200, "text/plain", "ok")

        if not self.path.startswith("/convert"):
            return self._send(404, "text/plain", "no")

        ctype = self.headers.get("Content-Type", "")
        m = re.search(r"boundary=(.+)", ctype)
        if not m:
            return self._send(400, "text/plain", "要 multipart")
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        parts = parse_multipart(body, m.group(1).strip('"').encode())

        fn, blob = parts.get("onnx", (None, b""))
        if not fn or not blob:
            return self._send(400, "text/plain", "没收到模型文件")
        val = lambda k, d="": (parts.get(k, (None, b""))[1] or b"").decode().strip() or d
        name = re.sub(r"[^A-Za-z0-9_.-]", "_", val("name", "my_model"))
        onnx = os.path.join(WORK, name + ".onnx")
        open(onnx, "wb").write(blob)

        with LOCK:
            JOB.update(running=True, om=None, rc=None, stage="准备")
        threading.Thread(target=convert_job, daemon=True, args=(
            onnx, name, val("inp", "input"), val("shape", "1,3,640,640"),
            "aipp" in parts, int(val("ncalib", "16")), val("fmt", "YVU420SP"),
        )).start()
        self._send(200, "text/plain", "ok")


if __name__ == "__main__":
    print("转模型页面: http://0.0.0.0:%d/   (工作目录 %s)" % (PORT, WORK))
    print("相机 = %s   atc 环境 = %s" % (CAM, ATC_ENV))
    ThreadingHTTPServer(("0.0.0.0", PORT), H).serve_forever()
