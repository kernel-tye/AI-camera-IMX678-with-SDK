# 甜源创新 · Hi3519DV500 + Sony IMX678 网络相机 / Network Camera

**中文** ｜ [English below](#english)

海思 **Hi3519DV500** + **Sony IMX678** 的完整网络相机固件工程。单文件 `sample_venc.c` 承载:
双 RTSP(4K H.265 `/main` + 1080p H.264 `/sub`)· 720p 网页预览 · 4K 抓拍 · IMX678 寄存器实时读 ·
GPIO 监视 · ISP 调节 · **OSD 时间水印** · **对焦助手(点对焦 + 中心对焦框)** · **4.3″ DSI 屏(默认关)** ·
设 IP/网络 · MMZ 内存 · 网页终端 · dropbear SSH。全部登录鉴权,配置持久化到 flash `param` 分区。

> ⚠️ **私有仓**。含海思 SDK 衍生的二进制(`opt_stage/` 的 `.ko`/`.so`)与厂商 ISP 调参表,请勿公开。

### 本仓 = 对厂商 SDK 的「差异/覆盖层」,不含 SDK 本体
厂商 SDK 有 **4.2GB**、多个 >100MB 的文件(GitHub 单文件上限 100MB)、且专有 —— 不进 GitHub。
**本仓只放我们自己改/写的东西**;配一份**对应版本的纯净 SDK(`Hi3519DV500_SDK_V1.0.2.0`)即可直接开编**。

**直接开编(三步):**
```sh
# 1) 解压厂商 SDK V1.0.2.0 → 得到 .../a55_linux/source
# 2) 把本仓的改动覆盖上去(src/ 镜像 SDK 目录结构)
./apply.sh /path/to/.../a55_linux/source
# 3) 一键编译 → user.squashfs (需交叉工具链在 PATH, 见 build_firmware.sh 顶部)
cd /path/to/.../a55_linux/source/mpp/sample/venc/tianyuan_pkg && ./build_firmware.sh
```

**只想重刷相机、不改代码?** 光靠 `user.squashfs` + `mtdwrite` 即可,**不需要 SDK**:板子进 port-23 后门或串口 →
传这两个文件 → `mtdwrite user.squashfs /dev/mtd5` → reboot。

### 当前状态（2026-07-22）
- **flash 里(物理板子在跑)= `ccd32603`**:屏幕(默认关)+ OSD 时间戳 + 对焦助手(全画幅)
- **本仓最新 = `e6968f3f`**(`user.squashfs` 即此版):加了 **点对焦 + 中心对焦框 + 焦点值叠加到画面**;已编译 + /tmp 实测,**未固化**
- **下一步 = NPU / YOLO**:板上 NPU 驱动已就绪,SDK 自带 yolov8/v5 `.om` 模型 + `svp_acl` 运行时,待集成

### 目录 / Layout
| 路径 | 说明 |
|---|---|
| `src/` | 对 SDK 的全部改动(见 `MANIFEST`),路径镜像 `a55_linux/source/` —— **即"差异层"** |
| `apply.sh` / `export.sh` | 覆盖到纯净 SDK / 反向从 SDK 抽取刷新 `src/` |
| `build_firmware.sh` | 一键编译 → `user.squashfs`(烧 mtd5) |
| `opt_stage/` · `rootfs_scripts/` | 内核模块+库 / 开机脚本(S99cam、S90tcpsvd) |
| `mtdwrite.c` + `mtdwrite` | mtd5 烧写工具(源码 + aarch64 静态 ELF) |
| `user.squashfs` | **当前构建产物** —— 它 + `mtdwrite` 即可重刷相机(**不需要 SDK**) |
| `docs/` | 移植排错实录.html + DSI 屏驱动参考 |

### 关键约定
- 编译必须传 `SENSOR0_TYPE=SONY_IMX678_MIPI_8M_30FPS_12BIT`(否则默认错传感器,出不了图)
- **永不写** mtd0(U-Boot)/mtd2(bl31)/mtd3(kernel)/mtd4(rootfs)—— 只动 mtd5(user)+ mtd6(param),砖不了
- 烧写前 `dd` 回读比 md5,一致才 reboot;mtdwrite 擦 21MB 约 1–2 分钟,后台跑别掐断

---

## English

Complete network-camera firmware for **HiSilicon Hi3519DV500** + **Sony IMX678**. One file, `sample_venc.c`,
carries: dual RTSP (4K H.265 `/main` + 1080p H.264 `/sub`), 720p web preview, 4K snapshot, live IMX678
register readout, GPIO monitor, ISP tuning, **OSD timestamp**, **focus assist (center-spot + focus box)**,
**4.3″ DSI screen (off by default)**, set-IP, MMZ memory, web terminal, and dropbear SSH. All auth-gated;
config persisted to the flash `param` partition.

> ⚠️ **Private repo.** Contains SDK-derived binaries (`opt_stage/` `.ko`/`.so`) and vendor ISP tuning tables — do not make public.

### This repo = the DELTA / overlay on top of the vendor SDK (SDK not included)
The vendor SDK is **4.2 GB**, has several files >100 MB (GitHub's per-file cap), and is proprietary — so it
does **not** go on GitHub. This repo holds **only our own changes**. Drop in a clean matching
**`Hi3519DV500_SDK_V1.0.2.0`** and it builds directly:

```sh
# 1) Unpack vendor SDK V1.0.2.0 -> .../a55_linux/source
# 2) Overlay our changes (src/ mirrors the SDK tree)
./apply.sh /path/to/.../a55_linux/source
# 3) Build -> user.squashfs (cross toolchain on PATH; see top of build_firmware.sh)
cd /path/to/.../a55_linux/source/mpp/sample/venc/tianyuan_pkg && ./build_firmware.sh
```

**Just reflash the camera (no rebuild)?** `user.squashfs` + `mtdwrite` are enough — **no SDK needed**: get a
shell on the board (port-23 backdoor or serial), transfer both, `mtdwrite user.squashfs /dev/mtd5`, reboot.

### Current state (2026-07-22)
- **On the physical board (flashed) = `ccd32603`**: screen (off by default) + OSD timestamp + focus assist (full-frame)
- **Latest in this repo = `e6968f3f`** (`user.squashfs` is this build): adds **center-spot focus + focus box + focus value drawn on the video**; compiled + tested on /tmp, **not yet flashed**
- **Next = NPU / YOLO**: the NPU driver is already up on the board; the SDK ships yolov8/v5 `.om` models + the `svp_acl` runtime — integration pending

### Key rules
- Must build with `SENSOR0_TYPE=SONY_IMX678_MIPI_8M_30FPS_12BIT` (default is the wrong sensor → no image)
- **Never** write mtd0 (U-Boot) / mtd2 (bl31) / mtd3 (kernel) / mtd4 (rootfs) — only mtd5 (user) + mtd6 (param); un-brickable
- Before flashing, `dd` read-back and compare md5; reboot only if it matches. mtdwrite erases 21 MB (~1–2 min) — run it backgrounded, don't kill it
