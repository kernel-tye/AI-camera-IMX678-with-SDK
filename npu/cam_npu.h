/*
 * cam_npu.h — 天元 IMX678 相机 NPU/YOLO 目标检测后端接口
 *
 * 设计: 把所有 svp_acl / vendor SVP helper 调用隔离在 cam_npu.c 里,
 *       对 sample_venc.c 只暴露下面 4 个函数 + 1 个结构体。
 *       这样 sample_venc 侧(VPSS 喂帧 / 推理线程 / 画框 / web 控制)与
 *       AI 后端完全解耦, 换模型/换 SDK 只动 cam_npu.c。
 *
 * 坐标约定: cam_det_t 里的 x/y/w/h 已按 open() 传入的 show_w/show_h 缩放,
 *           直接就是"预览分辨率(1280x720)下的像素矩形", 画框时无需再换算。
 */
#ifndef CAM_NPU_H
#define CAM_NPU_H

#ifdef __cplusplus
extern "C" {
#endif

#define CAM_NPU_MAX_DET 32   /* 单帧最多回报的检测框数(够用, 防越界) */

typedef struct {
    int   x, y, w, h;   /* 检测框, 已缩放到 show_w x show_h 的像素坐标 */
    int   cls;          /* 类别 id (COCO-80: 0=person 2=car ...) */
    float score;        /* 置信度 0..1 */
} cam_det_t;

/*
 * 加载模型。om_path = /opt/model/yolov8.om ; show_w/show_h = 你希望检测框
 * 映射到的显示分辨率(传预览通道尺寸 1280x720)。成功返回 0, 失败 <0。
 * 非致命: 调用方失败时应继续跑相机, 只是不做检测。
 */
int  cam_npu_open(const char *om_path, int show_w, int show_h);

/*
 * 对一帧做检测。零拷贝: 直接把 VPSS 帧的物理地址喂给 NPU(AIPP 在 .om 内
 * 完成 色彩转换+缩放), 不做 CPU 拷贝。
 *   phys_addr = ot_video_frame_info.v_frame.phys_addr[0]
 *   img_w/img_h/stride = 该帧实际宽/高/行距
 *   out/max = 输出数组及容量 ; conf = 置信度阈值(如 0.35)
 * 返回检测到的框数(0..max), 出错返回 -1。线程安全由调用方保证(单线程调用)。
 */
int  cam_npu_infer_frame(unsigned long long phys_addr, int img_w, int img_h, int stride,
                         cam_det_t *out, int max, float conf);

/* 释放模型与运行时。 */
void cam_npu_close(void);

/* COCO-80 类别名(英文短名), 越界返回 "?"。用于 OSD/打印。 */
const char *cam_npu_class_name(int cls);

#ifdef __cplusplus
}
#endif

#endif /* CAM_NPU_H */
