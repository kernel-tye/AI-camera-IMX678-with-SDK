/*
 * cam_npu.c — NPU/YOLO 后端实现 (Hi3519DV500 SVP / svp_acl 运行时)
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  ⚠  离线草稿 (板子/VM 在家不可达时写就)。控制流、数据结构、COCO 类名表、    │
 * │     yolov8 输出解析逻辑是准的; 但对 vendor SVP helper 的**函数签名**是凭   │
 * │     研读记忆重建的 —— 每个此类调用都标了 `⚠VERIFY`。回到 VM 后:            │
 * │       1) 把 SDK 里 sample_common_svp_npu.{c,h}、sample_common_svp_npu_    │
 * │          model.{c,h} 拷进 venc/ (prep_npu.sh 会做, 并剥掉 HRNET/VGS)。     │
 * │       2) 用真头文件核对每个 ⚠VERIFY 处的函数名/参数, 有出入就改这一处。    │
 * │       3) 编译 → 修剩余小错 → 跑。整体流程无需重想。                        │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * 参考: SDK mpp/sample/svp/npu/sample_svp_npu_process.c 的 e2e_yolo 主循环,
 *       及 sample_common_svp_npu_model.c 的 load/create/execute/get_roi。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cam_npu.h"

/* ⚠VERIFY: 头文件名/路径以 SDK 为准(拷进 venc/ 后一般就是这两个) */
#include "sample_common_svp_npu.h"
#include "sample_common_svp_npu_model.h"

/* ---- 内部状态 ------------------------------------------------------------ */
static int              g_npu_inited = 0;
static int              g_show_w = 1280, g_show_h = 720;

/* ⚠VERIFY: 模型/输入输出句柄的类型名以 sample_common_svp_npu_model.h 为准。
 * vendor 里通常是一个描述模型的结构体(含 model_id、输入/输出 dataset 指针)。
 * 这里用 vendor 的类型 svp_npu_model / svp_npu_data 占位, 名字对不上就改这里。*/
static svp_npu_model    g_model;          /* ⚠VERIFY 类型名 */
static int              g_model_ok = 0;

/* yolov8 检测头输出配置(带内置 NMS 的 .om): 输出张量按 4 行 xmin/ymin/xmax/ymax
 * 组织。取自 SDK g_svp_npu_yolo_roi_info。⚠VERIFY 字段名。*/
/* (若 vendor helper 通过 model 内部已知 roi 布局, 则不需要显式传, 保留注释即可) */

/* ---- COCO-80 类名(与 SDK 无关, 准确) ----------------------------------- */
static const char *g_coco80[] = {
    "person","bicycle","car","motorbike","aeroplane","bus","train","truck","boat",
    "traffic light","fire hydrant","stop sign","parking meter","bench","bird","cat",
    "dog","horse","sheep","cow","elephant","bear","zebra","giraffe","backpack",
    "umbrella","handbag","tie","suitcase","frisbee","skis","snowboard","sports ball",
    "kite","baseball bat","baseball glove","skateboard","surfboard","tennis racket",
    "bottle","wine glass","cup","fork","knife","spoon","bowl","banana","apple",
    "sandwich","orange","broccoli","carrot","hot dog","pizza","donut","cake","chair",
    "sofa","pottedplant","bed","diningtable","toilet","tvmonitor","laptop","mouse",
    "remote","keyboard","cell phone","microwave","oven","toaster","sink","refrigerator",
    "book","clock","vase","scissors","teddy bear","hair drier","toothbrush"
};

const char *cam_npu_class_name(int cls)
{
    if (cls < 0 || cls >= (int)(sizeof(g_coco80) / sizeof(g_coco80[0]))) { return "?"; }
    return g_coco80[cls];
}

/* ---- 打开: 初始化运行时 + 加载模型 --------------------------------------- */
int cam_npu_open(const char *om_path, int show_w, int show_h)
{
    int ret;
    if (g_npu_inited) { return 0; }
    g_show_w = show_w > 0 ? show_w : 1280;
    g_show_h = show_h > 0 ? show_h : 720;

    /* 1) svp_acl 运行时初始化。⚠VERIFY: vendor 里一般是
     *    sample_common_svp_npu_acl_init(NULL 或某 acl.json 路径)。
     *    底层 = svp_acl_init/svp_acl_rt_set_device(0)/create_context+stream。*/
    ret = sample_common_svp_npu_acl_init(NULL);            /* ⚠VERIFY 名/参 */
    if (ret != 0) { printf("[npu] acl_init fail %#x\n", ret); return -1; }
    g_npu_inited = 1;

    /* 2) 从文件加载 .om 到内存并 load_model。⚠VERIFY: vendor 通常提供
     *    sample_common_svp_npu_create_model_from_file(path, &g_model) 或
     *    load_model(&g_model, buf, len)。取其一。*/
    ret = sample_common_svp_npu_load_model(om_path, &g_model);   /* ⚠VERIFY 名/参 */
    if (ret != 0) { printf("[npu] load_model %s fail %#x\n", om_path, ret); cam_npu_close(); return -1; }

    /* 3) 按模型描述创建输入/输出 dataset(内部据模型 IO 尺寸分配)。⚠VERIFY */
    ret = sample_common_svp_npu_create_input(&g_model);    /* ⚠VERIFY */
    if (ret != 0) { printf("[npu] create_input fail %#x\n", ret); cam_npu_close(); return -1; }
    ret = sample_common_svp_npu_create_output(&g_model);   /* ⚠VERIFY */
    if (ret != 0) { printf("[npu] create_output fail %#x\n", ret); cam_npu_close(); return -1; }

    g_model_ok = 1;
    printf("[npu] model up: %s  (boxes -> %dx%d)\n", om_path, g_show_w, g_show_h);
    return 0;
}

/* ---- 推理一帧 ------------------------------------------------------------ */
int cam_npu_infer_frame(unsigned long long phys_addr, int img_w, int img_h, int stride,
                        cam_det_t *out, int max, float conf)
{
    int ret, roi_num, i, n = 0;
    (void)img_w; (void)img_h; (void)stride;   /* AIPP 在 .om 内做 resize, 一般只需 phys_addr;
                                                 若 vendor 需要 w/h/stride, 见下 ⚠VERIFY 处传入 */
    if (!g_model_ok || out == NULL || max <= 0) { return -1; }

    /* 1) 把这帧的物理地址挂到模型输入 dataset 的 data buffer(零拷贝)。⚠VERIFY:
     *    vendor 里是 update_input_data_buffer_info(&g_model, phys_addr, ...) 或
     *    svp_acl_update_data_buffer(buf, (void*)phys_addr, size)。*/
    ret = sample_common_svp_npu_update_input(&g_model, phys_addr, img_w, img_h, stride); /* ⚠VERIFY 名/参 */
    if (ret != 0) { return -1; }

    /* 2) 同步执行推理。⚠VERIFY: sample_common_svp_npu_model_execute(&g_model)。*/
    ret = sample_common_svp_npu_model_execute(&g_model);   /* ⚠VERIFY */
    if (ret != 0) { return -1; }

    /* 3) 取检测结果。yolov8 .om 内置 NMS, 输出即最终框。
     *    ⚠VERIFY: get_roi_num(&g_model, out_idx) 返回框数;
     *             get_roi(&g_model, i, &rect, &score, &cls) 逐个取。
     *    rect 已是 proc(模型输入)坐标, 需 * show/proc 缩放; 但若 vendor 的
     *    get_roi 已按 show_w/show_h 缩放(其入参含 show 尺寸), 则直接用。
     *    这里假设需要我们自己缩放, 用 g_show_w/g_show_h。*/
    roi_num = sample_common_svp_npu_get_roi_num(&g_model, 0);   /* ⚠VERIFY */
    if (roi_num < 0) { return -1; }
    if (roi_num > max) { roi_num = max; }

    for (i = 0; i < roi_num; i++) {
        int xmin, ymin, xmax, ymax, cls;
        float score;
        /* ⚠VERIFY: 参数顺序/类型以 helper 为准。若 helper 直接给像素 rect+score+cls 最省事。*/
        ret = sample_common_svp_npu_get_roi(&g_model, 0, i, &xmin, &ymin, &xmax, &ymax, &score, &cls); /* ⚠VERIFY */
        if (ret != 0) { continue; }
        if (score < conf) { continue; }
        /* 裁剪到显示范围 */
        if (xmin < 0) { xmin = 0; }
        if (ymin < 0) { ymin = 0; }
        if (xmax > g_show_w) { xmax = g_show_w; }
        if (ymax > g_show_h) { ymax = g_show_h; }
        if (xmax <= xmin || ymax <= ymin) { continue; }
        out[n].x = xmin; out[n].y = ymin;
        out[n].w = xmax - xmin; out[n].h = ymax - ymin;
        out[n].cls = cls; out[n].score = score;
        n++;
    }
    return n;
}

/* ---- 关闭 ---------------------------------------------------------------- */
void cam_npu_close(void)
{
    if (g_model_ok) {
        sample_common_svp_npu_destroy_input(&g_model);     /* ⚠VERIFY */
        sample_common_svp_npu_destroy_output(&g_model);    /* ⚠VERIFY */
        sample_common_svp_npu_unload_model(&g_model);      /* ⚠VERIFY */
        g_model_ok = 0;
    }
    if (g_npu_inited) {
        sample_common_svp_npu_acl_deinit();                /* ⚠VERIFY */
        g_npu_inited = 0;
    }
}
