# 甜源创新 · Hi3519DV500 + Sony IMX678 网络相机

海思 **Hi3519DV500** + **IMX678** 的完整网络相机固件工程。单文件 `sample_venc.c` 承载:
双 RTSP(4K H265 /main + 1080p H264 /sub)· 720p 网页预览 · 4K 抓拍 · IMX678 寄存器实时读 ·
GPIO 监视 · ISP 调节 · **OSD 时间水印** · **对焦助手(点对焦+对焦框)** · **4.3″ DSI 屏(默认关)** ·
设 IP/网络 · MMZ 内存 · 网页终端 · dropbear SSH。全部 auth-gated,配置持久化到 flash `param` 分区。

> ⚠ **私有仓**。含海思 SDK 衍生的二进制(`opt_stage/` 的 .ko/.so)与厂商 ISP 调参表,勿公开。
> **不含厂商 SDK 本体**(4.2GB、多个 >100MB 文件、专有)—— SDK 单独备份到网盘/硬盘。

## 当前状态（2026-07-22）
- **flash 里(物理板子在跑)= `ccd32603`**：屏幕(默认关)+ OSD 时间戳 + 对焦助手(全画幅)
- **本仓最新源码/squashfs = `e6968f3f`**：在 ccd32603 基础上加了 **点对焦 + 中心对焦框 + 焦点值叠加到画面**
  （已编译 + /tmp 实测,**未固化**;`user.squashfs` 即此版,刷进去即同步)
- **下一步 = NPU/YOLO**：板上 NPU 驱动已就绪,SDK 自带 yolov8/v5 .om 模型 + svp_acl 运行时,待集成

## 目录
| 路径 | 说明 |
|---|---|
| `src/` | 对厂商 SDK 的全部改动(见 `MANIFEST`),路径镜像 `a55_linux/source/` |
| `apply.sh` / `export.sh` | 套用改动到纯净 SDK / 反向抽取刷新 src |
| `build_firmware.sh` | 一键编译 → `user.squashfs`(烧 mtd5) |
| `opt_stage/` | 内核模块 + 库(组装 /opt) |
| `rootfs_scripts/` | S99cam / S90tcpsvd / S85netfix |
| `mtdwrite.c` + `mtdwrite` | mtd5 烧写工具(源码 + aarch64 静态 ELF) |
| `user.squashfs` | **当前构建产物**——光靠它 + mtdwrite 即可重刷相机(**不需要 SDK**) |
| `display/dsi_panel_43.c` | 4.3″ DSI 屏驱动参考(屏代码已内联进 sample_venc.c) |
| `docs/` | 移植排错实录.html + 屏驱动参考 |

## 两种恢复
- **只重刷相机**(不改代码):板子进 port-23 后门或串口 → 传 `user.squashfs` + `mtdwrite` → `mtdwrite user.squashfs /dev/mtd5` → reboot。**不需要 SDK。**
- **重新编译**:解压厂商 SDK V1.0.2.0 → `./apply.sh <.../a55_linux/source>` → `cd .../tianyuan_pkg && ./build_firmware.sh`。

## 关键约定
- 编译必须传 `SENSOR0_TYPE=SONY_IMX678_MIPI_8M_30FPS_12BIT`(否则默认错传感器,出不了图)
- 永不写 mtd0(U-Boot)/mtd2(bl31)/mtd3(kernel)/mtd4(rootfs)——只动 mtd5(user)+ mtd6(param),砖不了
- 烧写前 `dd` 回读比 md5,一致才 reboot;mtdwrite 擦 21MB 约 1-2 分钟,后台跑别掐断
