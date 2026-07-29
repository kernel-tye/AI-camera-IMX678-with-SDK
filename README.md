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

### 当前状态（2026-07-29）

- **flash 里(物理板子在跑)= `d77a8d3f`**(`user.squashfs` 即此版, 12.27MB / mtd5 上限 22020096,
  还剩约 9.3MB):**人脸检测 + 眼控对焦 + 取景辅助已固化并重启验证**
  (回读 md5 与镜像一致 → reboot → `/opt/bin/sample_venc` = `d77a8d3f`,
  YuNet 开机自动从 `/opt/model/` 加载, 全接口正常)
- `/opt/model/` 现有三个模型:`yolov8.om` / `yolov7.om` / **`yunet_face_aipp.om`**,网页可热切换

#### 本轮固化进去的（2026-07-29）
- **人脸检测**:`yunet_face_aipp.om`(YuNet, 265KB)—— 裸输出,解码 + NMS 在 A55 上做。
  **推理仅 2-3ms**(yolov7 是 24ms)。载入时按输出张量名**自动判型**(`cls_8/bbox_8/obj_8/kps_8` → YuNet),
  换模型不用改代码。实测检出率 146/150 帧,置信度 ~60%
- **眼控对焦**:YuNet 给 5 个关键点,前两个是双眼 → AF 统计窗缩到双眼中点附近(叠加层画青框)。
  实测焦点值从 7300 → 19341,跟焦确实生效
- **AIPP**:转模型时 `--insert_op_conf=tools_atc/aipp_yvu.cfg` 让 NPU **片上**做 YVU420SP→BGR,
  输入仍是 uint8 YUV(614400 字节 stride 640)→ **沿用零拷贝喂法**,不用在 A55 上每帧转 4.9MB float
- **峰值对焦描边**(梯度超门槛描红,灵敏度 1-100)+ **过曝斑马纹**(Y 超门槛打 45° 斜纹)。
  两者与检测共用 VPSS 扩展通道(640x360,多消费者),抓图肉眼验证正确
- **修 `cam_qparam()` 后缀撞车**:裸 `strstr("键=")` 会让 `th` 撞上 `zth`、`us` 撞上 `maxus`
  (`/setexp?maxus=5000` 会连手动快门一起改掉)。改为校验匹配点前一字符是 `?` 或 `&`
- **`MANIFEST` 与 `src/` 对齐**:补上了 `venc/Makefile` + `sample_common_svp_npu{,_model}.{c,h}`
  —— 之前 `apply.sh` 覆盖纯净 SDK 时会漏掉它们,编不出 NPU 版

#### 更早已固化的（`97e58889`, 2026-07-26 起就在里面）
- **NPU 检测 + 相机手动挡**(以下功能自 `97e58889` 起就已固化并重启验证过)
  - **曝光 P/S/M 三挡**:S 只锁快门(增益仍自动), M 快门+增益全锁死(实测 1/1000s ISO 100 平均亮度掉到 1);曝光补偿 / 自动挡最长快门可调;读回当前快门/增益/ISO/帧率/平均亮度
  - **亮度直方图**(ISP 硬件 1024bin 归 64 格, `/hist`)
  - **对焦区跟 AI 主体走**:AF 统计从固定中心 3x3 改成跟检测框(映射到 15x17 网格), 画面上黄框实时跟随 —— 换人脸模型即是眼控对焦
  - YOLOv8n / YOLOv7 两个模型进 flash(`/opt/model/`),网页「AI 检测」页可**热切换**(约 2 秒)
  - 推理 **16ms**(yolov8n) / **24ms**(yolov7),每 3 帧一次,load 与不开检测基本持平
  - 框+类别名+置信度**画进画面**(720p 预览/MJPEG/DSI 屏都可见),置信度门槛 / NMS / 类别白名单网页可调
  - **letterbox 修正**:4K(16:9) 等比缩到 640x360 再补灰边进 640x640,不再横向压扁(之前灯会被认成牙刷)
  - 输出张量名**自动识别**(配对 `X`/`X_`),换任何 yolo 系 `.om` 都不用改代码
- **模型转换**:`.om` 是 PICO 格式离线产物,**只能 PC 端转**(SVP_PC 里的 CANN `atc` + `amct`,x86_64);
  `svp_acl` 运行时只有 load 接口,板子上转不了
- **下一步**:把上面那批已验证的改动**固化到 mtd5**;白平衡手动 / RAW 抓拍 / EXIF / 录像

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

### Current state (2026-07-26)
- **On the physical board (flashed) = `97e58889`** (`user.squashfs` is this build, 11.6MB): **NPU detection + manual camera controls are in flash and reboot-verified**
  - **P/S/M exposure modes** (S locks shutter only, M locks shutter+gain: verified 1/1000s → ISO 100, mean luma 1), AE compensation, max auto shutter, and live read-back of shutter/gain/ISO/fps/mean-luma
  - **Luma histogram** (ISP 1024-bin hardware stats binned to 64, `/hist`)
  - **AF window follows the AI subject**: the AF statistics window moves from the fixed centre 3x3 onto the detection box (mapped to the 15x17 grid); a yellow box tracks it on screen — swap in a face model and this becomes eye AF
  - YOLOv8n + YOLOv7 both shipped in flash (`/opt/model/`), **hot-swappable** from the web "AI" tab (~2s)
  - **16ms** per inference (yolov8n) / **24ms** (yolov7), every 3rd frame; load is the same as with detection off
  - Boxes + class names + scores are **drawn into the video** (720p preview / MJPEG / DSI panel); score threshold, NMS and a class whitelist are all web-adjustable
  - **letterbox fix**: the 4K 16:9 frame is scaled to 640x360 and grey-padded into 640x640 instead of being squashed (a lamp used to come out as "toothbrush")
  - Output tensor names are **auto-detected** (the `X`/`X_` pair), so any yolo-family `.om` drops in without code changes
- **Model conversion**: `.om` is a PICO-format offline artifact — **PC-side only** (CANN `atc` + `amct` from SVP_PC, x86_64). The `svp_acl` runtime only has load entry points; the board cannot compile models.
- **Next**: task-specific models (face / pets) + moving the ISP AF statistics window onto the detection box (step 1 toward focus tracking)

### Key rules
- Must build with `SENSOR0_TYPE=SONY_IMX678_MIPI_8M_30FPS_12BIT` (default is the wrong sensor → no image)
- **Never** write mtd0 (U-Boot) / mtd2 (bl31) / mtd3 (kernel) / mtd4 (rootfs) — only mtd5 (user) + mtd6 (param); un-brickable
- Before flashing, `dd` read-back and compare md5; reboot only if it matches. mtdwrite erases 21 MB (~1–2 min) — run it backgrounded, don't kill it
