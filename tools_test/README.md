# 离线自测脚本（PC 上跑，不需要板子）

固件是交叉编译的，手边没有 aarch64 工具链时改完 C 只能干等。这几个脚本把**最容易错、
又不依赖硬件**的部分挪到 PC 上真跑一遍，省掉一次上板往返。只要 python3 + numpy。

| 脚本 | 验什么 |
|---|---|
| `yunet_decode_test.py` | YuNet 解码器（`cam_npu_parse_yunet`）的索引数学：三种张量摆法的取值、格点 col/row 推导、letterbox 反算、候选上限的顶替、贪心 NMS。把 C 的下标一比一移到 Python，喂合成张量，断言解出来的框和双眼坐标落在预期千分比上 |
| `peak_zebra_test.py` | 峰值描边/斑马纹（`cam_vis_analyze` + `cam_vis_blit`）：掩膜 bit 打包解包、斜纹周期与"斜不斜"、梯度门槛映射、640×360→1280×720 铺屏不越界、描边色压过斑马纹 |
| `cbalance.py` | 土法 C 词法扫描：剥掉注释和字符串/字符字面量后查 `{} () []` 配平。挡不住类型错误，但能挡住少个大括号、注释没闭合、字面量没收尾 |

```bash
python3 tools_test/yunet_decode_test.py
python3 tools_test/peak_zebra_test.py
python3 tools_test/cbalance.py src/mpp/sample/venc/sample_venc.c
```

**这些验不了什么**：编译期的类型/声明错误、MPP 与 svp_acl 的 API 语义、模型输出的真实语义
（YuNet 公式是否符合权重）、以及任何跟真实硬件相关的行为。它们只是把"逻辑写错"和
"低级语法错"提前挡掉，**不能替代上板验证**。
