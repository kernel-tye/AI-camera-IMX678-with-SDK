# 模型转换（PC 端）—— ONNX → `.om`

板子上**转不了**模型：`svp_acl` 只有 load 接口，`.om` 是 **PICO** 格式的离线产物，
必须用 x86 的 CANN 工具链（`atc`）编译。这个目录放的是已经踩通的配方。

## 一次性安装（在 x86 Linux 上，本项目用那台编译 VM）

```bash
# 工具包在 SDK 交付件里: SVP_PC.part1.rar + part2.rar → SVP_PC/SVP_NNN_PC_V3.0.2.9.tgz (3.06GB)
pip3 install numpy attrs decorator sympy cffi pyyaml psutil protobuf scipy requests absl-py
cd SVP_NNN_PC_V3.0.2.9/MindStudio
./Ascend-cann-toolkit_6.10.*.run --install --quiet --install-path=$HOME/Ascend
```

`atc_env.sh` = 环境变量（`source` 一下就能用 `atc`）。
注意 `LD_LIBRARY_PATH` 必须含 `atc/third_party_lib`，否则 `libprotobuf.so.3.13.0.0` 找不到。

## 关键参数

| 参数 | 值 | 说明 |
|---|---|---|
| `--soc_version` | **`Hi3519DV500`** | 只支持 Hi3516DV500 / Hi3519DV500 |
| `--framework` | `5` | 5=ONNX，0=Caffe |
| `--image_list` | 见下 | **必须给**，这颗 NPU 跑 int8，要量化校准 |
| `--insert_op_conf` | `aipp_yvu.cfg` | AIPP：让 NPU 片上做 YUV→BGR，输入就能直接吃 VPSS 的 YVU420SP |

**校准数据格式有两套规则，别搞混**：

- **不开 AIPP**：`--image_list="<输入名>:<数据文件>"` —— 直接给**裸数据文件**，
  内容必须 **float32**（1×3×640×640 = 4915200 字节），**扩展名必须是 `.bgr`**
  （其它扩展名会被当文本解析）
- **开 AIPP**：`--image_list="<输入名>:<列表文件>"` —— 给**列着图片路径的文本文件**，
  图片只支持 `.jpg/.jpeg/.png/.bmp/.jp2`（解码和预处理由 AIPP 负责）

`make_calib.py` 从相机抓帧生成校准数据（用相机自己的画面校准量化，精度最好；
letterbox 灰底 114 与固件喂 NPU 的方式一致）。它**两套数据都生成**：
`calib_jpg/` + `calib_jpg_list.txt`（AIPP 用）和 `calib/*.bgr`（无 AIPP 用）。

`aipp_yvu.cfg` 里 `input_format` **只接受 `YUV420SP` / `YVU420SP`**
（`YUV420SP_U8`、`NV12`、`NV21` 等一概报 invalid）。本项目 VPSS 扩展通道输出
`OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420` → 用 **`YVU420SP`**。
配的矩阵输出通道顺序是 **BGR**（YuNet / OpenCV 系模型要的），不做归一化：

    行0 = 298*Y + 516*(U-128)               -> B
    行1 = 298*Y - 100*(U-128) - 208*(V-128) -> G
    行2 = 298*Y + 409*(V-128)               -> R

即 BT.601 limited range 定点 ×256。模型若要 RGB，把行0 与行2 对调。

⚠ **这个 cfg 文件里不能写注释**：解析器见到 `;;` 开头的行直接报
`Unknow ;;, please check it in --insert_op_conf=...`；`rbuv_swap_switch` 这个
token 本版也不认（`is not supported`）。所以配置保持"光秃秃"，说明都写在这份 README 里。

## 示例：转 YuNet 人脸检测

```bash
source atc_env.sh
wget -O yunet.onnx https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx
python3 make_calib.py 16 640          # 抓 16 帧相机画面做校准（两套数据都出）
atc --model=yunet.onnx --framework=5 --output=yunet_face_aipp \
    --soc_version=Hi3519DV500 --input_shape="input:1,3,640,640" \
    --insert_op_conf=aipp_yvu.cfg --image_list="input:/home/ty/models/calib_jpg_list.txt"
```

`yunet_face.om`（本目录里那个，259KB）是**不带 AIPP** 的版本，已验证能被板子加载，
但它的输入是 float32（stride 2560），固件那边零拷贝喂法不适用 —— 所以要 AIPP 版。
固件启动时会检查输入缓冲大小，不是 `stride*h*3/2` 就会打
`⚠ 这个 .om 多半没带 AIPP` 的告警，照着重转即可。

## 固件已经会解 YuNet 了

`sample_venc.c` 载入模型后按输出张量名自动判型：

* 输出成对 `X` / `X_` → 厂商 RPN 模型（yolov5/7/8），NMS 在片上，阈值张量 `rpn_data`
* 输出里有 `cls_8/obj_8/bbox_8/kps_8`… → **YuNet**，在 A55 上解码 + 自己做 NMS

所以转好的 `.om` 丢进板子 `/tmp/model`（或固化到 `/opt/model`），网页「AI 检测」页
切过去就能用，不用改代码。人脸模型会额外给 5 个关键点，前两个是双眼 →
勾上「对焦区跟主体」即为**眼控对焦**。`rpn_data` 设不上已改成非致命（裸输出模型本来就没有）。

## 厂商 `.om` 为什么自带 NMS

SDK 里 yolov5/7/8 的 `.om` 输出是 `X` / `X_` 一对（目标数 + 框坐标），
那是 atc 的 **RPN 检测输出层**产生的，相关参数：
`--nms_threshold`、`--low_score_threshold`、`--min_height/--min_width`、
`--max_roi_frame_cnt`（默认 **300**，正好对上 yolov8.om 的 roi stride 1200 字节 = 300 个目标；
yolov7 用了 32）、`--detection_out_accuracy`、`--generate_anchors_file`。

要产生这对输出，**模型里得有 RPN/检测输出层**；具体算子名在 ATC 文档里，
而那批 PC 端文档（`ReleaseDoc/zh/01.Software/pc/SVP_NPU/`）**没随包交付**。
所以自己训的模型走另一条路：转成裸输出，解码 + NMS 在板子上用 C 写。
