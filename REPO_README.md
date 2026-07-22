# 甜源创新 · Hi3519DV500 + IMX678 网络相机 —— 工程备份

**不含厂商 SDK 本体**(海思专有,勿公开)。本仓库只有"我们对 SDK 的改动 + 构建/烧写脚手架",
配一份纯净的 Hi3519DV500 SDK **V1.0.2.0** 即可复现整台相机固件。**建议私有仓**。

## 仓库内容
- `src/`            —— 本项目对 SDK 的全部改动(见 `MANIFEST`),路径镜像 a55_linux/source/
- `apply.sh`        —— 把 src/ 覆盖到一份纯净 SDK
- `export.sh`       —— 反向:从改好的 SDK 抽取改动刷新 src/(维护用)
- `MANIFEST`        —— 改动文件清单
- `build_firmware.sh` + `opt_stage/` + `rootfs_scripts/` + `mtdwrite.c` —— 构建 user.squashfs + 烧写工具
- `display/dsi_panel_43.c` —— 4.3" DSI 屏驱动参考(屏代码已内联进 sample_venc.c)

## 改动清单(相对 a55_linux/source/)
| 路径 | 说明 |
|---|---|
| mpp/cbb/isp/user/sensor/hi3519dv500/**sony_imx678/** | 新增传感器驱动(SDK 原本无);376 条寄存器表、VMAX/GAIN 地址修正 |
| mpp/sample/Makefile.param | +`libsns_imx678.a`(静态链,避开 no-SONAME rpath) |
| mpp/sample/common/sample_comm_vi.c | i2c bus_id=3、OB=20 |
| mpp/sample/common/sample_comm_isp.c | 裁剪 y=20、Bayer=RGGB |
| mpp/sample/venc/sample_venc.c | 整台相机载体:web/RTSP/寄存器/GPIO/ISP/对焦/OSD/屏/终端/SSH |

**内核/dtb 一行未改**(生产用厂商原版内核)。

## 复现固件
```sh
# 1) 解压厂商 SDK V1.0.2.0 -> 得到 .../a55_linux/source
# 2) 套用改动
./apply.sh /path/to/.../a55_linux/source
# 3) 构建(需交叉工具链在 PATH; 见 build_firmware.sh 顶部)
cd /path/to/.../a55_linux/source/mpp/sample/venc/tianyuan_pkg
./build_firmware.sh          # -> user.squashfs
# 4) 烧写: 交叉编译 mtdwrite.c, 传到板子, mtdwrite user.squashfs /dev/mtd5 (见 README.md)
```

## 公开分享注意
若要 **公开**,先删 `opt_stage/`(海思 .ko/.so 二进制)与 `src/mpp/cbb/.../imx678_cmos_ex.h`
(厂商 ISP 调参表)等厂商衍生内容;仅自己备份则私有仓保留全部即可一键复现。
