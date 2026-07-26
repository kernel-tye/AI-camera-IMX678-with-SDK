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
| `--insert_op_conf` | `aipp_yuv.cfg` | AIPP：让 NPU 片上做 YUV→RGB，输入就能直接吃 VPSS 的 YVU420SP |

**校准数据格式有两套规则，别搞混**：

- **不开 AIPP**：`--image_list="<输入名>:<数据文件>"` —— 直接给**裸数据文件**，
  内容必须 **float32**（1×3×640×640 = 4915200 字节），**扩展名必须是 `.bgr`**
  （其它扩展名会被当文本解析）
- **开 AIPP**：`--image_list="<输入名>:<列表文件>"` —— 给**列着图片路径的文本文件**，
  图片只支持 `.jpg/.jpeg/.png/.bmp/.jp2`（解码和预处理由 AIPP 负责）

`make_calib.py` 从相机抓帧生成校准数据（用相机自己的画面校准量化，精度最好；
letterbox 灰底 114 与固件喂 NPU 的方式一致）。

`aipp_yuv.cfg` 里 `input_format` **只接受 `YUV420SP` / `YVU420SP`**
（`YUV420SP_U8`、`NV12`、`NV21` 等一概报 invalid）。本项目 VPSS 扩展通道输出
`OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420` → 用 **`YVU420SP`**。

## 示例：转 YuNet 人脸检测

```bash
source atc_env.sh
wget -O yunet.onnx https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx
python3 make_calib.py 16 640          # 抓 16 帧相机画面做校准
atc --model=yunet.onnx --framework=5 --output=yunet_face_aipp \
    --soc_version=Hi3519DV500 --input_shape="input:1,3,640,640" \
    --insert_op_conf=aipp_yvu.cfg --image_list="input:calib_jpg_list.txt"
```

`yunet_face.om`（本目录里那个，259KB）是**不带 AIPP** 的版本，已验证能被板子加载，
但它的输入是 float32（stride 2560），固件那边零拷贝喂法不适用 —— 所以要 AIPP 版。

## 厂商 `.om` 为什么自带 NMS

SDK 里 yolov5/7/8 的 `.om` 输出是 `X` / `X_` 一对（目标数 + 框坐标），
那是 atc 的 **RPN 检测输出层**产生的，相关参数：
`--nms_threshold`、`--low_score_threshold`、`--min_height/--min_width`、
`--max_roi_frame_cnt`（默认 **300**，正好对上 yolov8.om 的 roi stride 1200 字节 = 300 个目标；
yolov7 用了 32）、`--detection_out_accuracy`、`--generate_anchors_file`。

要产生这对输出，**模型里得有 RPN/检测输出层**；具体算子名在 ATC 文档里，
而那批 PC 端文档（`ReleaseDoc/zh/01.Software/pc/SVP_NPU/`）**没随包交付**。
所以自己训的模型走另一条路：转成裸输出，解码 + NMS 在板子上用 C 写。
