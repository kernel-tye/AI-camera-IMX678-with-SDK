# NPU / YOLO 集成补丁 (sample_venc.c) — 精确锚点版

> 目的:把 `cam_npu.{c,h}` 接进成品相机 `sample_venc.c`。
> **不直接改金母本**——回到 VM 后按下面 8 个 Block 贴进去即可。锚点是母本里
> 已存在、唯一的代码行,搜到它、在其后/前插入对应块。
>
> 依赖:`cam_npu.c/.h` 已拷进 `venc/`;`prep_npu.sh` 已把 vendor SVP helper
> 拷进 `venc/` 并改好 Makefile(见本目录 `prep_npu.sh`)。
>
> 分阶段:**Phase 1** = Block A/B/C/E/F/G(推理 + /tmp 打印 + web JSON)。
> **Phase 2** = Block D(把框画到预览画面上)。可以先只贴 Phase 1 验证再贴 D。

---

## Block A — include + 全局状态
**锚点**:文件头 `#include "securec.h"`(第 14 行)之后加:
```c
#include "cam_npu.h"
```

**锚点**:OSD 全局区,`static int g_focus_spot = 1;`(约 3585 行)之后加:
```c
/* ---- NPU / YOLO 目标检测 ---- */
static int   g_npu_en   = 0;                 /* 总开关(默认关, 省电/省算力) */
static int   g_npu_draw = 1;                 /* 1=把框画到预览画面(Phase2) */
static int   g_npu_ok   = 0;                 /* 模型是否加载成功 */
static int   g_npu_period = 3;               /* 每 N 帧推一次(节流) */
static float g_npu_conf = 0.35f;             /* 置信度阈值 */
#define NPU_OM_PATH "/opt/model/yolov8.om"
#define NPU_FEED_CHN 4                        /* VPSS grp0 扩展通道号(4..7) */
static cam_det_t g_npu_dets[CAM_NPU_MAX_DET]; /* 最近一帧结果(画框+web 读) */
static int   g_npu_ndet = 0;
static unsigned long g_npu_infer_cnt = 0;
static pthread_mutex_t g_npu_lock = PTHREAD_MUTEX_INITIALIZER;
```

---

## Block B — VPSS 扩展通道喂帧
> grp0 的 4 个物理通道(chn0 4K / chn1 1080p / chn2 720p / chn3 4K快照)已占满,
> 故用**扩展通道 chn4**,bind 到 chn2(720p, 轻)。ext chn 输出 1280x720,
> 缩放到模型输入交给 .om 里的 AIPP。
>
> ⚠VERIFY:`ot_vpss_ext_chn_attr` 字段名以 SDK `ot_vpss.h` 为准(标准布局如下)。

**锚点**:放在 `cam_osd_init` 函数(约 3959 行)**之前**,新增:
```c
/* 打开 VPSS grp0 扩展通道 NPU_FEED_CHN, 作为 NPU 的帧源(源=物理 chn2 720p) */
static int cam_npu_feed_open(int show_w, int show_h)
{
    ot_vpss_ext_chn_attr ext = {0};   /* ⚠VERIFY 结构体名/字段 */
    td_s32 ret;
    ext.bind_chn      = 2;                          /* 源:预览 720p 通道 */
    ext.pixel_format  = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;  /* NV21/NV12, 与主链一致 */
    ext.dynamic_range = OT_DYNAMIC_RANGE_SDR8;
    ext.video_format  = OT_VIDEO_FORMAT_LINEAR;
    ext.compress_mode = OT_COMPRESS_MODE_NONE;      /* 给 NPU 的帧不压缩 */
    ext.frame_rate.src_frame_rate = -1;
    ext.frame_rate.dst_frame_rate = -1;
    ext.width  = show_w;                            /* 1280 */
    ext.height = show_h;                            /* 720  */
    ext.depth  = 2;                                 /* >0 才能 get_chn_frame */
    ret = ss_mpi_vpss_set_ext_chn_attr(0, NPU_FEED_CHN, &ext);   /* ⚠VERIFY 名 */
    if (ret != TD_SUCCESS) { printf("[npu] set_ext_chn_attr fail %#x\n", ret); return -1; }
    ret = ss_mpi_vpss_enable_chn(0, NPU_FEED_CHN);
    if (ret != TD_SUCCESS) { printf("[npu] enable ext chn fail %#x\n", ret); return -1; }
    printf("[npu] feed: vpss grp0 ext chn%d up (%dx%d, src chn2)\n", NPU_FEED_CHN, show_w, show_h);
    return 0;
}

static void cam_npu_feed_close(void)
{
    (td_void)ss_mpi_vpss_disable_chn(0, NPU_FEED_CHN);
}
```

---

## Block C — 推理线程 (Phase 1 核心: 打印检测结果)
**锚点**:放在 `cam_osd_thread`(约 4071 行)**之后**,新增:
```c
static void *cam_npu_thread(void *arg)
{
    ot_video_frame_info fi;
    cam_det_t dets[CAM_NPU_MAX_DET];
    (td_void)arg;
    while (1) {
        if (!g_npu_en || !g_npu_ok) { usleep(200000); continue; }
        /* 取一帧(阻塞式, 100ms 超时) */
        if (ss_mpi_vpss_get_chn_frame(0, NPU_FEED_CHN, &fi, 100) != TD_SUCCESS) {
            usleep(20000); continue;
        }
        {
            unsigned long long phys = fi.v_frame.phys_addr[0];
            int w = (int)fi.v_frame.width, h = (int)fi.v_frame.height;
            int stride = (int)fi.v_frame.stride[0];
            int n = cam_npu_infer_frame(phys, w, h, stride, dets, CAM_NPU_MAX_DET, g_npu_conf);
            (td_void)ss_mpi_vpss_release_chn_frame(0, NPU_FEED_CHN, &fi);
            if (n < 0) { usleep(20000); continue; }
            pthread_mutex_lock(&g_npu_lock);
            memcpy(g_npu_dets, dets, sizeof(cam_det_t) * (n > 0 ? n : 0));
            g_npu_ndet = n;
            g_npu_infer_cnt++;
            pthread_mutex_unlock(&g_npu_lock);
            /* Phase 1: 打印到 stdout(重定向到 /tmp/npu.log 看) */
            if (n > 0) {
                int i;
                printf("[npu] frame#%lu: %d obj:", g_npu_infer_cnt, n);
                for (i = 0; i < n; i++) {
                    printf("  %s %.2f @(%d,%d %dx%d)", cam_npu_class_name(dets[i].cls),
                           dets[i].score, dets[i].x, dets[i].y, dets[i].w, dets[i].h);
                }
                printf("\n"); fflush(stdout);
            }
        }
        /* 节流:每 g_npu_period 帧推一次(720p@某帧率, 这里粗略睡 period*33ms) */
        usleep((useconds_t)(g_npu_period * 33000));
    }
    return TD_NULL;
}
```

---

## Block D — 把框画到预览画面 (Phase 2)
> 独立全屏 region `NPU_HANDLE=2`(1280x720 ARGB1555)贴 chn2。每次刷新清空再画
> 当前 g_npu_dets 的 N 个空心框。类别名不上屏(现有点阵字库只有数字/冒号,
> 要中英文名需完整字库, 留后续);类别在 web /getnpu 的 JSON 里给。

**锚点**:`#define BOX_HANDLE 1` 那组宏(约 3905 行)之后加:
```c
#define NPU_HANDLE 2
static int g_npu_rgn_ready = 0;
static int g_npu_rgn_w = 1280, g_npu_rgn_h = 720;
```

**锚点**:`cam_focusbox_draw` 函数(约 3943 行)之后加:
```c
/* 在全屏 canvas 上画一个 2px 白+1px 黑边的空心矩形(内部透明) */
static void cam_npu_rect(unsigned char *base, int stride, int cw, int ch,
                         int x, int y, int w, int h)
{
    int t, i;
    if (x < 0) { x = 0; } if (y < 0) { y = 0; }
    if (x + w > cw) { w = cw - x; } if (y + h > ch) { h = ch - y; }
    if (w <= 0 || h <= 0) { return; }
    for (t = 0; t < 3; t++) {   /* 3 像素粗边: 外黑内白 */
        unsigned short col = (t == 0) ? 0x8000 : 0xFFFF;
        for (i = 0; i < w; i++) {
            if (y + t < ch)         { ((unsigned short *)(base + (y + t) * stride))[x + i] = col; }
            if (y + h - 1 - t >= 0) { ((unsigned short *)(base + (y + h - 1 - t) * stride))[x + i] = col; }
        }
        for (i = 0; i < h; i++) {
            if (x + t < cw)         { ((unsigned short *)(base + (y + i) * stride))[x + t] = col; }
            if (x + w - 1 - t >= 0) { ((unsigned short *)(base + (y + i) * stride))[x + w - 1 - t] = col; }
        }
    }
}

/* 刷新 NPU 检测框 region(Phase2). g_npu_en&&g_npu_draw 时画, 否则清空 */
static void cam_npu_draw(void)
{
    ot_rgn_canvas_info ci;
    unsigned char *base; int stride, i, n;
    cam_det_t d[CAM_NPU_MAX_DET];
    if (!g_npu_rgn_ready) { return; }
    if (ss_mpi_rgn_get_canvas_info(NPU_HANDLE, &ci) != TD_SUCCESS) { return; }
    base = (unsigned char *)ci.virt_addr; stride = (int)ci.stride;
    (td_void)memset_s(base, (size_t)stride * ci.size.height, 0, (size_t)stride * ci.size.height);
    if (g_npu_en && g_npu_draw && g_npu_ok) {
        pthread_mutex_lock(&g_npu_lock);
        n = g_npu_ndet; if (n > CAM_NPU_MAX_DET) { n = CAM_NPU_MAX_DET; }
        memcpy(d, g_npu_dets, sizeof(cam_det_t) * (n > 0 ? n : 0));
        pthread_mutex_unlock(&g_npu_lock);
        for (i = 0; i < n; i++) {
            cam_npu_rect(base, stride, (int)ci.size.width, (int)ci.size.height,
                         d[i].x, d[i].y, d[i].w, d[i].h);
        }
    }
    (td_void)ss_mpi_rgn_update_canvas(NPU_HANDLE);
}
```

**锚点**:`cam_osd_init` 函数体末尾(创建完 BOX region 的 `g_box_ready = 1; }` 之后,
函数结束 `}` 之前)加建 region 2:
```c
    /* region 2: NPU 检测框, 全屏贴 chn2(720p) */
    (td_void)memset_s(&ra, sizeof(ra), 0, sizeof(ra));
    ra.type = OT_RGN_OVERLAY;
    ra.attr.overlay.pixel_format = OT_PIXEL_FORMAT_ARGB_1555;
    ra.attr.overlay.bg_color = 0x0000;
    ra.attr.overlay.size.width  = g_npu_rgn_w;
    ra.attr.overlay.size.height = g_npu_rgn_h;
    ra.attr.overlay.canvas_num = 2;
    if (ss_mpi_rgn_create(NPU_HANDLE, &ra) == TD_SUCCESS) {
        (td_void)memset_s(&ca, sizeof(ca), 0, sizeof(ca));
        ca.is_show = TD_TRUE;
        ca.type = OT_RGN_OVERLAY;
        ca.attr.overlay_chn.point.x = 0;
        ca.attr.overlay_chn.point.y = 0;
        ca.attr.overlay_chn.fg_alpha = 255;
        ca.attr.overlay_chn.bg_alpha = 0;
        ca.attr.overlay_chn.layer = 2;
        chn.chn_id = 2; (td_void)ss_mpi_rgn_attach_to_chn(NPU_HANDLE, &chn, &ca);
        g_npu_rgn_ready = 1;
        printf("[npu] detection box region up (chn2)\n");
    }
```

**锚点**:`cam_osd_thread` 循环里 `cam_osd_update();` 之后加一行(让画框跟着刷):
```c
        cam_npu_draw();
```

---

## Block E — 配置项持久化
**锚点**:`cam_cfg_save` 的 fprintf 格式串,`"osd_en=%d\nosd_focus=%d\nfocus_spot=%d\n"`
末尾追加 `npu_en=%d\nnpu_draw=%d\n`,并在参数列表末尾加 `, g_npu_en, g_npu_draw`。

**锚点**:`cam_cfg_load` 里 `focus_spot=` 那行之后加:
```c
        else if (!strncmp(line, "npu_en=", 7))   { g_npu_en = atoi(line + 7) ? 1 : 0; }
        else if (!strncmp(line, "npu_draw=", 9)) { g_npu_draw = atoi(line + 9) ? 1 : 0; }
```

---

## Block F — web /getnpu /setnpu
**锚点**:`cam_http_setosd` 函数(约 4454 行)之后加两个 handler:
```c
static void cam_http_getnpu(int fd)
{
    char j[640]; int n, i, cnt;
    cam_det_t d[CAM_NPU_MAX_DET];
    pthread_mutex_lock(&g_npu_lock);
    cnt = g_npu_ndet; if (cnt > CAM_NPU_MAX_DET) { cnt = CAM_NPU_MAX_DET; }
    memcpy(d, g_npu_dets, sizeof(cam_det_t) * (cnt > 0 ? cnt : 0));
    pthread_mutex_unlock(&g_npu_lock);
    n = snprintf(j, sizeof(j), "{\"en\":%d,\"ok\":%d,\"draw\":%d,\"n\":%d,\"dets\":[",
                 g_npu_en, g_npu_ok, g_npu_draw, cnt);
    for (i = 0; i < cnt && n < (int)sizeof(j) - 80; i++) {
        n += snprintf(j + n, sizeof(j) - n, "%s{\"c\":\"%s\",\"s\":%.2f,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}",
                      i ? "," : "", cam_npu_class_name(d[i].cls), d[i].score,
                      d[i].x, d[i].y, d[i].w, d[i].h);
    }
    n += snprintf(j + n, sizeof(j) - n, "]}");
    cam_http_body(fd, "application/json", j, n);
}

static void cam_http_setnpu(int fd, const char *req)
{
    char v[16]; int changed = 0;
    if (cam_qparam(req, "en", v, sizeof(v)))   { pthread_mutex_lock(&g_cfg_lock); g_npu_en = atoi(v) ? 1 : 0;   pthread_mutex_unlock(&g_cfg_lock); changed = 1; }
    if (cam_qparam(req, "draw", v, sizeof(v))) { pthread_mutex_lock(&g_cfg_lock); g_npu_draw = atoi(v) ? 1 : 0; pthread_mutex_unlock(&g_cfg_lock); changed = 1; }
    if (changed) { cam_cfg_save(); }
    cam_http_text(fd, "\xe5\xb7\xb2\xe4\xbf\x9d\xe5\xad\x98");  /* 已保存 */
}
```

**锚点**:web 分发的白名单 `strstr(req, "GET /setosd") != TD_NULL ||`(约 4788 行)后加:
```c
                   strstr(req, "GET /getnpu") != TD_NULL || strstr(req, "GET /setnpu") != TD_NULL ||
```

**锚点**:分发 else-if 链里 `cam_http_setosd(fd, req);` 那个分支之后加:
```c
            } else if (strstr(req, "GET /getnpu") != TD_NULL) {
                cam_http_getnpu(fd);
            } else if (strstr(req, "GET /setnpu") != TD_NULL) {
                cam_http_setnpu(fd, req);
```
> 注意顺序:`/setnpu`/`/getnpu` 必须在 `/set` 之前分派(strstr 会误命中 `/set`);
> 上面这两个分支加在 `/setosd` 之后、`/set` 之前即满足。

---

## Block G — main() 启动挂钩
**锚点**:main 里 `cam_osd_init();` +起 osd 线程那段(约 5466 行)之后加:
```c
    /* NPU/YOLO: 加载模型 + 开扩展通道喂帧 + 起推理线程(非致命) */
    if (cam_npu_open(NPU_OM_PATH, 1280, 720) == 0) {
        g_npu_ok = 1;
        if (cam_npu_feed_open(1280, 720) == 0) {
            pthread_t npu_th; pthread_create(&npu_th, TD_NULL, cam_npu_thread, TD_NULL);
            printf("[npu] inference thread started (en=%d)\n", g_npu_en);
        } else {
            g_npu_ok = 0;
        }
    } else {
        printf("[npu] model not loaded (%s missing?) -> detection disabled\n", NPU_OM_PATH);
    }
```

---

## Block H — Makefile / 链接 (见 prep_npu.sh, 这里说明)
1. `venc/Makefile`:`TARGET := $(SMP_SRCS:%.c=%)` 改成固定 `TARGET := sample_venc`
   (否则每个 .c 都会被当成一个可执行目标, cam_npu.c 会被单独链)。
2. `mpp/sample/Makefile.param`:打开 svp 静态库那段(约 78-83 行的条件),使
   `MPI_LIBS += $(REL_LIB)/libsvp_acl.a $(REL_LIB)/libsvp_acl_sec.a $(REL_LIB)/libprotobuf-c.a`,
   并加动态 `-lsvp_aicpu`(把 `libsvp_aicpu.so` 放进 `opt_stage/lib`)。
3. 加头文件搜索路径:`-I$(SDK)/mpp/sample/svp/common`(或拷进 venc/ 后 `-I.`)。
4. 部署:把转换好的 `yolov8.om` 放到板子 `/opt/model/`;`libsvp_aicpu.so` 打进
   `user.squashfs` 的 `/opt/lib`;`ot_svp_npu.ko` 已在(load 脚本已加载)。

---

## 验证顺序(回家后)
1. `./prep_npu.sh <SDK a55_linux/source>` → 拷 helper+cam_npu、剥 HRNET、改 Makefile。
2. 按本文件贴 Block A/B/C/E/F/G(Phase1)。核对所有 `⚠VERIFY`。
3. `./build_firmware.sh` 编译。**先在 /tmp 跑**(不固化):
   `./sample_venc 7 >/tmp/npu.log 2>&1 &`,web `/setnpu?en=1`,`tail -f /tmp/npu.log`
   看 `[npu] frame#..: N obj: person 0.82 @(...)`。
4. OK 后贴 Block D(Phase2 画框),重编,web `/setnpu?en=1&draw=1`,看预览/DSI 屏上框。
5. 都稳 → 固化(squashfs 加 libsvp_aicpu.so + yolov8.om + cam_npu.o)。
