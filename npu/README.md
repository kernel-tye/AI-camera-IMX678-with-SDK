# npu/ — 离线草稿(已被真机版取代)

本目录是 2026-07-23 板子/VM 不可达时写的 NPU 集成**离线草稿**(cam_npu.c/.h +
INTEGRATION.md 锚点补丁 + prep_npu.sh)。架构与真机实现一致,可作参照。

**实际采用的是 VM 上早已写好并编译通过的内联集成**,现已并入仓库:
- `src/mpp/sample/venc/sample_venc.c` (5935 行, 含完整 Phase1 NPU/YOLOv8)
- `src/mpp/sample/venc/sample_common_svp_npu{,_model}.c/.h` (vendor helper,
  model.c 已剥掉 HRNET/VGS 的 `sample_common_svp_vgs_fill_joint_coords_lines`)
- `src/mpp/sample/venc/Makefile` (`TARGET:=sample_venc` + `-I../svp/common`)
- `src/mpp/sample/Makefile.param` (svp_acl/protobuf 静态库)
- 模型: SDK 自带 `mpp/sample/svp/svp_npu/data/model/yolov8.om`

差异要点(真机版 vs 我的草稿):按 model_index+task_info 而非 model 句柄;
喂帧用 mmap VB池 + update_input_data_buffer_info(虚拟地址);解析用 roi_to_rect。
