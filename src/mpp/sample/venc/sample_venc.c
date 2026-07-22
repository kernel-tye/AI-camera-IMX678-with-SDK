/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include "sample_comm.h"
#include "sample_ipc.h"
#include "securec.h"

#define BIG_STREAM_SIZE PIC_3840X2160
#define SMALL_STREAM_SIZE PIC_1080P

#define VI_VB_YUV_CNT 6
#define VPSS_VB_YUV_CNT 8

#define ENTER_ASCII 10

#define VB_MAX_NUM 10

#define TYPE_NUM_MAX 3
#define CHN_NUM_MAX 2
#define GRP_NUM_MAX 2
#define MAX_WATERMARK_LEN 128

typedef struct {
    ot_size max_size;
    ot_pixel_format pixel_format;
    ot_size in_size;
    ot_size output_size[OT_VPSS_MAX_PHYS_CHN_NUM];
    ot_compress_mode compress_mode[OT_VPSS_MAX_PHYS_CHN_NUM];
    td_bool enable[OT_VPSS_MAX_PHYS_CHN_NUM];
    ot_fmu_mode fmu_mode[OT_VPSS_MAX_PHYS_CHN_NUM];
} sample_venc_vpss_chn_attr;

typedef struct {
    td_u32 valid_num;
    td_u64 blk_size[OT_VB_MAX_COMMON_POOLS];
    td_u32 blk_cnt[OT_VB_MAX_COMMON_POOLS];
    td_u32 supplement_config;
} sample_venc_vb_attr;

typedef struct {
    ot_vpss_chn vpss_chn[CHN_NUM_MAX];
    ot_venc_chn venc_chn[CHN_NUM_MAX];
} sample_venc_vpss_chn;

static td_bool g_sample_venc_exit = TD_FALSE;

/* *****************************************************************************
 * function : show usage
 * **************************************************************************** */
static td_void sample_venc_usage(td_char *s_prg_nm)
{
    printf("usage : %s [index] [options]\n", s_prg_nm);
    printf("index:\n");
    printf("\t  0) normal               :H.265e + H.264e.\n");
    printf("\t  1) qpmap                :H.265e + H.264e.\n");
    printf("\t  2) intra_refresh        :H.265e + H.264e.\n");
    printf("\t  3) roi_bg_frame_rate    :H.265e + H.264e.\n");
    printf("\t  4) debreath_effect      :H.265e + H.264e.\n");
    printf("\t  5) roi_set              :Mjpege + Mjpege(user set roi info by API).\n");
    printf("\t  6) roimap               :Mjpege + Mjpege(user customize every region).\n");
    printf("\t  7) mosaic map           :H.265e + H.264e(user send frame,mosaic)\n");
    printf("\t  8) mjpeg mosaic map     :Mjpege + Mjpege(user send frame,mosaic).\n");
    printf("\t  9) watermark            :H.265e + H.264e.\n");
    printf("\t  10) quality balance     :H.265e + H.264e.\n");
    printf("\t  11) FMU 1 grp 2 chn     :H.265e(direct) + Mjpge(warp).\n");
    printf("\t  12) FMU 2 grp 2 chn     :H.265e(direct) + H.264e(direct).\n");
    printf("options: not necessary\n");
    printf("\t  0) not save heif file.\n");
    printf("\t  1) save heif file.\n");
}

/* *****************************************************************************
 * function : to process abnormal case
 * **************************************************************************** */
static td_void sample_venc_handle_sig(td_s32 signo)
{
    if (g_sample_venc_exit == TD_TRUE) {
        return;
    }

    if (signo == SIGINT || signo == SIGTERM) {
        g_sample_venc_exit = TD_TRUE;
    }
}

static td_s32 sample_venc_getchar()
{
    td_s32 c;
    if (g_sample_venc_exit == TD_TRUE) {
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
        return 'e';
    }

    c = getchar();

    if (g_sample_venc_exit == TD_TRUE) {
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
        return 'e';
    }

    return c;
}

static td_void print_gop_mode()
{
    printf("please input choose gop mode!\n");
    printf("\t 0) normal p.\n");
    printf("\t 1) dual p.\n");
    printf("\t 2) smart p.\n");
}


static td_s32 sample_clear_invalid_ch()
{
    td_s32 c;

    while ((c = sample_venc_getchar()) != ENTER_ASCII) {
    }
    return TD_SUCCESS;
}

static td_s32 set_gop_mode(td_s32 c, ot_venc_gop_mode *gop_mode)
{
    switch (c) {
        case '0':
            *gop_mode = OT_VENC_GOP_MODE_NORMAL_P;
            break;

        case '1':
            *gop_mode = OT_VENC_GOP_MODE_DUAL_P;
            break;

        case '2':
            *gop_mode = OT_VENC_GOP_MODE_SMART_P;
            break;

        default:
            return TD_FAILURE;
    }
    return TD_SUCCESS;
}

static td_s32 get_gop_mode(ot_venc_gop_mode *gop_mode)
{
    td_s32 c;

    while (TD_TRUE) {
        print_gop_mode();
        c = sample_venc_getchar();
        if (g_sample_venc_exit) {
            return TD_FAILURE;
        }

        if (c == ENTER_ASCII) {
            sample_print("invalid input! please try again.\n");
            continue;
        } else if (set_gop_mode(c, gop_mode) == TD_SUCCESS && sample_venc_getchar() == ENTER_ASCII) {
            return TD_SUCCESS;
        }
        sample_clear_invalid_ch();
        sample_print("invalid input! please try again.\n");
    }

    return TD_SUCCESS;
}

static td_void print_rc_mode(ot_payload_type type)
{
    printf("please input choose rc mode!\n");
    printf("\t c) cbr.\n");
    printf("\t v) vbr.\n");
    if (type != OT_PT_MJPEG) {
        printf("\t b) abr.\n");
        printf("\t a) avbr.\n");
        printf("\t x) cvbr.\n");
        printf("\t q) qvbr.\n");
    }
    printf("\t f) fix_qp\n");
}

static td_s32 set_rc_mode(td_s32 c, sample_rc *rc_mode)
{
    switch (c) {
        case 'b':
            *rc_mode = SAMPLE_RC_ABR;
            break;

        case 'c':
            *rc_mode = SAMPLE_RC_CBR;
            break;

        case 'v':
            *rc_mode = SAMPLE_RC_VBR;
            break;

        case 'a':
            *rc_mode = SAMPLE_RC_AVBR;
            break;

        case 'x':
            *rc_mode = SAMPLE_RC_CVBR;
            break;

        case 'q':
            *rc_mode = SAMPLE_RC_QVBR;
            break;

        case 'f':
            *rc_mode = SAMPLE_RC_FIXQP;
            break;

        default:
            return TD_FAILURE;
    }
    return TD_SUCCESS;
}

td_s32 get_rc_mode(ot_payload_type type, sample_rc *rc_mode)
{
    td_s32 c;

    if (type == OT_PT_JPEG) {
        return TD_SUCCESS;
    }

    while (TD_TRUE) {
        print_rc_mode(type);
        c = sample_venc_getchar();
        if (g_sample_venc_exit) {
            return TD_FAILURE;
        }

        if (c == ENTER_ASCII) {
            sample_print("invalid input! please try again.\n");
            continue;
        } else if (set_rc_mode(c, rc_mode) == TD_SUCCESS && sample_venc_getchar() == ENTER_ASCII) {
            return TD_SUCCESS;
        }
        sample_clear_invalid_ch();
        sample_print("invalid input! please try again.\n");
    }

    return TD_SUCCESS;
}

static td_s32 set_intra_refresh_mode(td_s32 c, ot_venc_intra_refresh_mode *intra_refresh_mode)
{
    switch (c) {
        case 'r':
            *intra_refresh_mode = OT_VENC_INTRA_REFRESH_ROW;
            break;

        case 'c':
            *intra_refresh_mode = OT_VENC_INTRA_REFRESH_COLUMN;
            break;

        default:
            return TD_FAILURE;
    }
    return TD_SUCCESS;
}

static td_void print_refresh_mode()
{
    printf("please input choose intra refresh mode!\n");
    printf("\t r) row.\n");
    printf("\t c) column.\n");
}

td_s32 get_intra_refresh_mode(ot_venc_intra_refresh_mode *intra_refresh_mode)
{
    td_s32 c;

    while (TD_TRUE) {
        print_refresh_mode();
        c = sample_venc_getchar();
        if (g_sample_venc_exit) {
            return TD_FAILURE;
        }

        if (c == ENTER_ASCII) {
            sample_print("invalid input! please try again.\n");
            continue;
        } else if (set_intra_refresh_mode(c, intra_refresh_mode) == TD_SUCCESS &&
            sample_venc_getchar() == ENTER_ASCII) {
            return TD_SUCCESS;
        }
        sample_clear_invalid_ch();
        sample_print("invalid input! please try again.\n");
    }

    return TD_SUCCESS;
}

static td_void update_vb_attr(sample_venc_vb_attr *vb_attr, const ot_size *size, ot_pixel_format format,
    ot_compress_mode compress_mode, td_u32 blk_cnt)
{
    ot_pic_buf_attr pic_buf_attr = { 0 };

    if (vb_attr->valid_num >= OT_VB_MAX_COMMON_POOLS) {
        printf("vb valid_num = %d should smaller than OT_VB_MAX_COMMON_POOLS(%d)\n",
            vb_attr->valid_num, OT_VB_MAX_COMMON_POOLS);
        return;
    }

    pic_buf_attr.width = size->width;
    pic_buf_attr.height = size->height;
    pic_buf_attr.align = OT_DEFAULT_ALIGN;
    pic_buf_attr.bit_width = OT_DATA_BIT_WIDTH_8;
    pic_buf_attr.pixel_format = format;
    pic_buf_attr.compress_mode = compress_mode;
    vb_attr->blk_size[vb_attr->valid_num] = ot_common_get_pic_buf_size(&pic_buf_attr);
    vb_attr->blk_cnt[vb_attr->valid_num] = blk_cnt;

    vb_attr->valid_num++;
}

static td_void get_vb_attr(const ot_size *vi_size, const sample_venc_vpss_chn_attr *vpss_chn_attr,
    sample_venc_vb_attr *vb_attr)
{
    td_s32 i;

    /* vb for vi-vpss */
    update_vb_attr(vb_attr, vi_size, OT_PIXEL_FORMAT_YUV_SEMIPLANAR_422, OT_COMPRESS_MODE_NONE, VI_VB_YUV_CNT);

    // vb for vpss-venc(big stream)
    if (vb_attr->valid_num >= OT_VB_MAX_COMMON_POOLS) {
        return;
    }

    for (i = 0; i < OT_VPSS_MAX_PHYS_CHN_NUM && vb_attr->valid_num < OT_VB_MAX_COMMON_POOLS; i++) {
        if (vpss_chn_attr->enable[i] == TD_TRUE && vpss_chn_attr->fmu_mode[i] == OT_FMU_MODE_OFF) {
            update_vb_attr(vb_attr, &vpss_chn_attr->output_size[i], vpss_chn_attr->pixel_format,
                vpss_chn_attr->compress_mode[i], VPSS_VB_YUV_CNT);
        }
    }

    vb_attr->supplement_config = OT_VB_SUPPLEMENT_JPEG_MASK | OT_VB_SUPPLEMENT_BNR_MOT_MASK;
}

static td_void get_default_vpss_chn_attr(ot_size *vi_size, ot_size enc_size[], td_s32 len,
    sample_venc_vpss_chn_attr *vpss_chan_attr)
{
    td_s32 i;
    td_u32 max_width;
    td_u32 max_height;

    if (memset_s(vpss_chan_attr, sizeof(sample_venc_vpss_chn_attr), 0, sizeof(sample_venc_vpss_chn_attr)) != EOK) {
        printf("vpss chn attr call memset_s error\n");
        return;
    }

    max_width = vi_size->width;
    max_height = vi_size->height;

    for (i = 0; (i < len) && (i < OT_VPSS_MAX_PHYS_CHN_NUM); i++) {
        vpss_chan_attr->output_size[i].width = enc_size[i].width;
        vpss_chan_attr->output_size[i].height = enc_size[i].height;
        vpss_chan_attr->compress_mode[i] = (i == 0) ? OT_COMPRESS_MODE_SEG_COMPACT : OT_COMPRESS_MODE_NONE;
        vpss_chan_attr->enable[i] = TD_TRUE;
        vpss_chan_attr->fmu_mode[i] = OT_FMU_MODE_OFF;

        max_width = MAX2(max_width, enc_size[i].width);
        max_height = MAX2(max_height, enc_size[i].height);
    }

    vpss_chan_attr->max_size.width = max_width;
    vpss_chan_attr->max_size.height = max_height;
    vpss_chan_attr->pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;

    return;
}

static td_s32 sample_venc_sys_init(sample_venc_vb_attr *vb_attr)
{
    td_u32 i;
    td_s32 ret;
    ot_vb_cfg vb_cfg = { 0 };

    if (vb_attr->valid_num > OT_VB_MAX_COMMON_POOLS) {
        sample_print("sample_venc_sys_init vb valid num(%d) too large than OT_VB_MAX_COMMON_POOLS(%d)!\n",
            vb_attr->valid_num, OT_VB_MAX_COMMON_POOLS);
        return TD_FAILURE;
    }

    for (i = 0; i < vb_attr->valid_num; i++) {
        vb_cfg.common_pool[i].blk_size = vb_attr->blk_size[i];
        vb_cfg.common_pool[i].blk_cnt = vb_attr->blk_cnt[i];
    }

    vb_cfg.max_pool_cnt = vb_attr->valid_num;

    if (vb_attr->supplement_config == 0) {
        ret = sample_comm_sys_vb_init(&vb_cfg);
    } else {
        ret = sample_comm_sys_init_with_vb_supplement(&vb_cfg, vb_attr->supplement_config);
    }

    if (ret != TD_SUCCESS) {
        sample_print("sample_venc_sys_init failed!\n");
    }

    return ret;
}

static td_s32 sample_venc_vi_init(sample_vi_cfg *vi_cfg)
{
    td_s32 ret;

    ret = sample_comm_vi_start_vi(vi_cfg);
    if (ret != TD_SUCCESS) {
        sample_print("sample_comm_vi_start_vi failed: 0x%x\n", ret);
        return ret;
    }

    return TD_SUCCESS;
}

static td_void sample_venc_vi_deinit(sample_vi_cfg *vi_cfg)
{
    sample_comm_vi_stop_vi(vi_cfg);
}

static td_s32 sample_venc_vpss_init(ot_vpss_grp vpss_grp, sample_venc_vpss_chn_attr *vpss_chan_cfg)
{
    td_s32 ret;
    ot_vpss_chn vpss_chn;
    ot_vpss_grp_attr grp_attr = { 0 };
    sample_vpss_chn_attr vpss_chn_attr = {0};

    grp_attr.max_width = vpss_chan_cfg->max_size.width;
    grp_attr.max_height = vpss_chan_cfg->max_size.height;
    grp_attr.dei_mode = OT_VPSS_DEI_MODE_OFF;
    grp_attr.pixel_format = vpss_chan_cfg->pixel_format;
    grp_attr.frame_rate.src_frame_rate = -1;
    grp_attr.frame_rate.dst_frame_rate = -1;

    for (vpss_chn = 0; vpss_chn < OT_VPSS_MAX_PHYS_CHN_NUM; vpss_chn++) {
        if (vpss_chan_cfg->enable[vpss_chn] == TD_TRUE) {
            vpss_chn_attr.chn_attr[vpss_chn].width = vpss_chan_cfg->output_size[vpss_chn].width;
            vpss_chn_attr.chn_attr[vpss_chn].height = vpss_chan_cfg->output_size[vpss_chn].height;
            vpss_chn_attr.chn_attr[vpss_chn].chn_mode = OT_VPSS_CHN_MODE_USER;
            vpss_chn_attr.chn_attr[vpss_chn].compress_mode = vpss_chan_cfg->compress_mode[vpss_chn];
            vpss_chn_attr.chn_attr[vpss_chn].pixel_format = vpss_chan_cfg->pixel_format;
            vpss_chn_attr.chn_attr[vpss_chn].frame_rate.src_frame_rate = -1;
            vpss_chn_attr.chn_attr[vpss_chn].frame_rate.dst_frame_rate = -1;
            vpss_chn_attr.chn_attr[vpss_chn].depth = 0;
            vpss_chn_attr.chn_attr[vpss_chn].mirror_en = 0;
            vpss_chn_attr.chn_attr[vpss_chn].flip_en = 0;
            vpss_chn_attr.chn_attr[vpss_chn].aspect_ratio.mode = OT_ASPECT_RATIO_NONE;
        }
    }

    memcpy_s(vpss_chn_attr.chn_enable, sizeof(vpss_chn_attr.chn_enable),
            vpss_chan_cfg->enable, sizeof(vpss_chn_attr.chn_enable));
    vpss_chn_attr.chn_array_size = OT_VPSS_MAX_PHYS_CHN_NUM;

    ret = sample_common_vpss_start(vpss_grp, &grp_attr, &vpss_chn_attr);
    if (ret != TD_SUCCESS) {
        sample_print("failed with %#x!\n", ret);
    }

    return ret;
}

static td_void sample_venc_vpss_deinit(ot_vpss_grp vpss_grp, sample_venc_vpss_chn_attr *vpss_chan_cfg)
{
    td_s32 ret;

    ret = sample_common_vpss_stop(vpss_grp, vpss_chan_cfg->enable, OT_VPSS_MAX_PHYS_CHN_NUM);
    if (ret != TD_SUCCESS) {
        sample_print("failed with %#x!\n", ret);
    }
}

static td_s32 sample_venc_init_param(ot_size *enc_size, td_s32 chn_num_max, ot_size *vi_size,
    sample_venc_vpss_chn_attr *vpss_param)
{
    td_s32 i;
    td_s32 ret;
    ot_pic_size pic_size[CHN_NUM_MAX] = {BIG_STREAM_SIZE, SMALL_STREAM_SIZE};

    enc_size[0].width = vi_size->width;
    enc_size[0].height = vi_size->height;
    for (i = 1; i < chn_num_max && i < CHN_NUM_MAX; i++) {
        ret = sample_comm_sys_get_pic_size(pic_size[i], &enc_size[i]);
        if (ret != TD_SUCCESS) {
            sample_print("sample_comm_sys_get_pic_size failed!\n");
            return ret;
        }
    }

    // get vpss param
    get_default_vpss_chn_attr(vi_size, enc_size, chn_num_max, vpss_param);

    return 0;
}

static td_s32 sample_venc_set_video_param(ot_size *enc_size, sample_comm_venc_chn_param *chn_param, td_s32 chn_num_max,
    ot_venc_gop_attr gop_attr, td_bool qp_map)
{
    td_bool share_buf_en = TD_TRUE;
    sample_rc rc_mode = 0;
    td_u32 profile[CHN_NUM_MAX] = {0, 0};
    ot_payload_type payload[4] = {OT_PT_H265, OT_PT_H264}; // 2 : payload num

    if (qp_map) {
        rc_mode = SAMPLE_RC_QPMAP;
    } else if ((rc_mode = SAMPLE_RC_CBR, TD_SUCCESS) != TD_SUCCESS) {
        return TD_FAILURE;
    }

    if (chn_num_max < 2) { /* 2: chn_param array len */
        sample_print("chn_num_max  %d not enough! should > 1\n", chn_num_max);
        return TD_FAILURE;
    }

    /* encode h.265 */
    chn_param[0].frame_rate = 30; /* 30: is a number */
    chn_param[0].gop = 60; /* 60: is a number */
    chn_param[0].stats_time = 2; /* 2: is a number */
    chn_param[0].gop_attr = gop_attr;
    chn_param[0].type = payload[0];
    chn_param[0].size = sample_comm_sys_get_pic_enum(&enc_size[0]);
    chn_param[0].rc_mode = rc_mode;
    chn_param[0].profile = profile[0];
    chn_param[0].is_rcn_ref_share_buf = share_buf_en;

    /* encode h.264 */
    chn_param[1].frame_rate = 30; /* 30: is a number */
    chn_param[1].gop = 60; /* 60: is a number */
    chn_param[1].stats_time = 2; /* 2: is a number */
    chn_param[1].gop_attr = gop_attr;
    chn_param[1].type = payload[1];
    chn_param[1].size = sample_comm_sys_get_pic_enum(&enc_size[1]);
    chn_param[1].rc_mode = rc_mode;
    chn_param[1].profile = profile[1];
    chn_param[1].is_rcn_ref_share_buf = share_buf_en;
    return TD_SUCCESS;
}

static td_void sample_set_venc_vpss_chn(sample_venc_vpss_chn *venc_vpss_chn, td_s32 chn_num_max)
{
    td_s32 i;
    ot_unused(chn_num_max);

    for (i = 0; i < CHN_NUM_MAX; i++) {
        venc_vpss_chn->vpss_chn[i] = i;
        venc_vpss_chn->venc_chn[i] = i;
    }
}

static td_void sample_venc_unbind_vpss_stop(ot_vpss_grp vpss_grp, const sample_venc_vpss_chn *venc_vpss_chn)
{
    td_s32 i;

    for (i = 0; i < CHN_NUM_MAX; i++) {
        sample_comm_vpss_un_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[i], venc_vpss_chn->venc_chn[i]);
        sample_comm_venc_stop(venc_vpss_chn->venc_chn[i]);
    }
}

static td_void sample_venc_stop(const sample_venc_vpss_chn *venc_vpss_chn, td_s32 chn_num_max)
{
    td_s32 i;
    ot_unused(chn_num_max);

    for (i = 0; i < CHN_NUM_MAX; i++) {
        sample_comm_venc_stop(venc_vpss_chn->venc_chn[i]);
    }
}

static td_s32 sample_venc_normal_start_encode(ot_size *enc_size, td_s32 chn_num_max,
    ot_vpss_grp vpss_grp, sample_venc_vpss_chn *venc_vpss_chn)
{
    td_s32 ret;
    ot_venc_gop_mode gop_mode;
    ot_venc_gop_attr gop_attr;
    sample_comm_venc_chn_param chn_param[CHN_NUM_MAX] = {0};

    gop_mode = OT_VENC_GOP_MODE_NORMAL_P; if (TD_FALSE) {
        return TD_FAILURE;
    }
    if ((ret = sample_comm_venc_get_gop_attr(gop_mode, &gop_attr)) != TD_SUCCESS) {
        sample_print("Venc Get GopAttr for %#x!\n", ret);
        return ret;
    }

    if ((ret = sample_venc_set_video_param(enc_size, chn_param, CHN_NUM_MAX, gop_attr, TD_FALSE)) != TD_SUCCESS) {
        return ret;
    }

    /* encode h.265 */
    if ((ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[0], &(chn_param[0]))) != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        return ret;
    }

    ret = sample_comm_vpss_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[0], venc_vpss_chn->venc_chn[0]);
    if (ret != TD_SUCCESS) {
        sample_print("sample_comm_vpss_bind_venc failed for %#x!\n", ret);
        goto EXIT_VENC_H265_STOP;
    }

    /* encode h.264 */
    if ((ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[1], &(chn_param[1]))) != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        goto EXIT_VENC_H265_UnBind;
    }

    ret = sample_comm_vpss_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[1], venc_vpss_chn->venc_chn[1]);
    if (ret != TD_SUCCESS) {
        sample_print("sample_comm_vpss_bind_venc failed for %#x!\n", ret);
        goto EXIT_VENC_H264_STOP;
    }

    /* *****************************************
     stream save process
    ***************************************** */
    if ((ret = sample_comm_venc_start_get_stream(venc_vpss_chn->venc_chn, CHN_NUM_MAX)) != TD_SUCCESS) {
        sample_print("Start Venc failed!\n");
        goto EXIT_VENC_H264_UnBind;
    }

    return TD_SUCCESS;

EXIT_VENC_H264_UnBind:
    sample_comm_vpss_un_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[1], venc_vpss_chn->venc_chn[1]);
EXIT_VENC_H264_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[1]);
EXIT_VENC_H265_UnBind:
    sample_comm_vpss_un_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[0], venc_vpss_chn->venc_chn[0]);
EXIT_VENC_H265_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[0]);

    return ret;
}

static td_void sample_venc_exit_process()
{
    printf("please press twice ENTER to exit this sample\n");
    (td_void)getchar();

    if (g_sample_venc_exit != TD_TRUE) {
        (td_void)getchar();
    }
    sample_comm_venc_stop_get_stream(CHN_NUM_MAX);
}

static td_void sample_venc_mosaic_map_exit_process(td_s32 chn_num_max)
{
    printf("please press twice ENTER to exit this sample\n");
    (td_void)getchar();

    if (g_sample_venc_exit != TD_TRUE) {
        (td_void)getchar();
    }

    sample_comm_venc_stop_send_frame();
    sample_comm_venc_stop_get_stream(chn_num_max);
}
/* *****************************************************************************
 * function :  H.265e@1080P@30fps + h264e@D1@30fps
 * **************************************************************************** */
static td_s32 sample_venc_normal(td_void)
{
    td_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg;
    ot_size enc_size[CHN_NUM_MAX], vi_size;
    const ot_vi_pipe vi_pipe = 0;
    const ot_vi_chn vi_chn = 0;
    const ot_vpss_grp vpss_grp = 0;
    sample_venc_vpss_chn_attr vpss_param;
    sample_venc_vb_attr vb_attr = { 0 };
    sample_venc_vpss_chn venc_vpss_chn = { 0 };

    sample_set_venc_vpss_chn(&venc_vpss_chn, CHN_NUM_MAX);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    sample_comm_vi_get_size_by_sns_type(sns_type, &vi_size);
    /* *****************************************
      step 0: related parameter ready
    ***************************************** */
    ret = sample_venc_init_param(enc_size, CHN_NUM_MAX, &vi_size, &vpss_param);
    if (ret != TD_SUCCESS) {
        return ret;
    }

    /* *****************************************
      step 1: init sys alloc common vb
    ***************************************** */
    get_vb_attr(&vi_size, &vpss_param, &vb_attr);

    if ((ret = sample_venc_sys_init(&vb_attr)) != TD_SUCCESS) {
        sample_print("Init SYS err for %#x!\n", ret);
        return ret;
    }

    if ((ret = sample_venc_vi_init(&vi_cfg)) != TD_SUCCESS) {
        sample_print("Init VI err for %#x!\n", ret);
        goto EXIT_SYS_STOP;
    }

    if ((ret = sample_venc_vpss_init(vpss_grp, &vpss_param)) != TD_SUCCESS) {
        sample_print("Init VPSS err for %#x!\n", ret);
        goto EXIT_VI_STOP;
    }

    if ((ret = sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0)) != TD_SUCCESS) {
        sample_print("VI Bind VPSS err for %#x!\n", ret);
        goto EXIT_VPSS_STOP;
    }

    if ((ret = sample_venc_normal_start_encode(enc_size, CHN_NUM_MAX, vpss_grp, &venc_vpss_chn)) != TD_SUCCESS) {
        goto EXIT_VI_VPSS_UNBIND;
    }

    /* *****************************************
     exit process
    ***************************************** */
    sample_venc_exit_process();
    sample_venc_unbind_vpss_stop(vpss_grp, &venc_vpss_chn);

EXIT_VI_VPSS_UNBIND:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0);
EXIT_VPSS_STOP:
    sample_venc_vpss_deinit(vpss_grp, &vpss_param);
EXIT_VI_STOP:
    sample_venc_vi_deinit(&vi_cfg);
EXIT_SYS_STOP:
    sample_comm_sys_exit();

    return ret;
}

static td_s32 sample_venc_qpmap_start_encode(ot_size *enc_size, td_s32 chn_num_max, ot_vpss_grp vpss_grp,
    sample_venc_vpss_chn *venc_vpss_chn)
{
    td_s32 ret;
    ot_venc_gop_mode gop_mode;
    ot_venc_gop_attr gop_attr;
    sample_comm_venc_chn_param chn_param[CHN_NUM_MAX] = {0};
    sample_comm_venc_chn_param *h265_chn_param = TD_NULL;
    sample_comm_venc_chn_param *h264_chn_param = TD_NULL;

    gop_mode = OT_VENC_GOP_MODE_NORMAL_P; if (TD_FALSE) {
        return TD_FAILURE;
    }
    if ((ret = sample_comm_venc_get_gop_attr(gop_mode, &gop_attr)) != TD_SUCCESS) {
        sample_print("Venc Get GopAttr for %#x!\n", ret);
        return ret;
    }

    if ((ret = sample_venc_set_video_param(enc_size, chn_param, CHN_NUM_MAX, gop_attr, TD_TRUE)) != TD_SUCCESS) {
        return ret;
    }

    /* encode h.265 */
    h265_chn_param = &(chn_param[0]);
    ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[0], h265_chn_param);
    if (ret != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        return ret;
    }

    /* encode h.264 */
    h264_chn_param = &(chn_param[1]);
    ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[1], h264_chn_param);
    if (ret != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        goto EXIT_VENC_H265_STOP;
    }

    ret = sample_comm_venc_qpmap_send_frame(vpss_grp, venc_vpss_chn->vpss_chn, venc_vpss_chn->venc_chn, CHN_NUM_MAX,
        enc_size);
    if (ret != TD_SUCCESS) {
        sample_print("sample_comm_venc_qpmap_send_frame failed for %#x!\n", ret);
        goto EXIT_VENC_H264_STOP;
    }

    /* *****************************************
     stream save process
    ***************************************** */
    if ((ret = sample_comm_venc_start_get_stream(venc_vpss_chn->venc_chn, CHN_NUM_MAX)) != TD_SUCCESS) {
        sample_print("Start Venc failed!\n");
        goto EXIT_VENC_H264_STOP;
    }

    return TD_SUCCESS;

EXIT_VENC_H264_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[1]);
EXIT_VENC_H265_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[0]);

    return ret;
}

static td_void sample_venc_qpmap_exit_process()
{
    printf("please press twice ENTER to exit this sample\n");
    (td_void)getchar();

    if (g_sample_venc_exit != TD_TRUE) {
        (td_void)getchar();
    }

    sample_comm_venc_stop_send_qpmap_frame();
    sample_comm_venc_stop_get_stream(CHN_NUM_MAX);
}

static td_s32 sample_venc_qpmap(td_void)
{
    td_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg;
    ot_size enc_size[CHN_NUM_MAX], vi_size;
    const ot_vi_pipe vi_pipe = 0;
    const ot_vi_chn vi_chn = 0;
    const ot_vpss_grp vpss_grp = 0;
    sample_venc_vpss_chn_attr vpss_param;
    sample_venc_vb_attr vb_attr = { 0 };
    sample_venc_vpss_chn venc_vpss_chn = { 0 };

    sample_set_venc_vpss_chn(&venc_vpss_chn, CHN_NUM_MAX);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    sample_comm_vi_get_size_by_sns_type(sns_type, &vi_size);
    /* *****************************************
      step 0: related parameter ready
    ***************************************** */
    if ((ret = sample_venc_init_param(enc_size, CHN_NUM_MAX, &vi_size, &vpss_param)) != TD_SUCCESS) {
        return ret;
    }

    /* *****************************************
      step 1: init sys alloc common vb
    ***************************************** */
    get_vb_attr(&vi_size, &vpss_param, &vb_attr);

    if ((ret = sample_venc_sys_init(&vb_attr)) != TD_SUCCESS) {
        sample_print("Init SYS err for %#x!\n", ret);
        return ret;
    }

    if ((ret = sample_venc_vi_init(&vi_cfg)) != TD_SUCCESS) {
        sample_print("Init VI err for %#x!\n", ret);
        goto EXIT_SYS_STOP;
    }

    if ((ret = sample_venc_vpss_init(vpss_grp, &vpss_param)) != TD_SUCCESS) {
        sample_print("Init VPSS err for %#x!\n", ret);
        goto EXIT_VI_STOP;
    }

    if ((ret = sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0)) != TD_SUCCESS) {
        sample_print("VI Bind VPSS err for %#x!\n", ret);
        goto EXIT_VPSS_STOP;
    }

    /* *****************************************
    start stream venc
    ***************************************** */
    if ((ret = sample_venc_qpmap_start_encode(enc_size, CHN_NUM_MAX, vpss_grp, &venc_vpss_chn)) != TD_SUCCESS) {
        goto EXIT_VI_VPSS_UNBIND;
    }

    /* *****************************************
     exit process
    ***************************************** */
    sample_venc_qpmap_exit_process();
    sample_venc_stop(&venc_vpss_chn, CHN_NUM_MAX);

EXIT_VI_VPSS_UNBIND:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0);
EXIT_VPSS_STOP:
    sample_venc_vpss_deinit(vpss_grp, &vpss_param);
EXIT_VI_STOP:
    sample_venc_vi_deinit(&vi_cfg);
EXIT_SYS_STOP:
    sample_comm_sys_exit();

    return ret;
}

static td_s32 sample_venc_vpss_bind_venc(ot_vpss_grp vpss_grp, ot_vpss_chn vpss_chn, ot_venc_chn venc_chn)
{
    td_s32 ret;

    ret = sample_comm_vpss_bind_venc(vpss_grp, vpss_chn, venc_chn);
    if (ret != TD_SUCCESS) {
        sample_print("call sample_comm_vpss_bind_venc vpss grp %d, vpss chn %d, venc chn %d, ret =  %#x!\n", vpss_grp,
            vpss_chn, venc_chn, ret);
        return ret;
    }

    return TD_SUCCESS;
}

static td_void sample_venc_intra_refresh_param_init(ot_venc_intra_refresh_mode intra_refresh_mode,
    ot_venc_intra_refresh *intra_refresh)
{
    intra_refresh->enable = TD_TRUE;
    intra_refresh->mode = intra_refresh_mode;
    if (intra_refresh_mode == OT_VENC_INTRA_REFRESH_ROW) {
        intra_refresh->refresh_num = 5; /* 5: refresh num */
    } else {
        intra_refresh->refresh_num = 6; /* 6: refresh num */
    }
    intra_refresh->request_i_qp = 30; /* 30: request num */
}

static td_s32 sample_venc_set_intra_refresh(ot_venc_chn venc_chn, ot_venc_intra_refresh_mode intra_refresh_mode)
{
    td_s32 ret;
    ot_venc_intra_refresh intra_refresh = { 0 };

    if ((ret = ss_mpi_venc_get_intra_refresh(venc_chn, &intra_refresh)) != TD_SUCCESS) {
        sample_print("Get Intra Refresh failed for %#x!\n", ret);
        return ret;
    }

    sample_venc_intra_refresh_param_init(intra_refresh_mode, &intra_refresh);

    if ((ret = ss_mpi_venc_set_intra_refresh(venc_chn, &intra_refresh)) != TD_SUCCESS) {
        sample_print("Set Intra Refresh failed for %#x!\n", ret);
        return ret;
    }

    return TD_SUCCESS;
}

static td_s32 sample_comm_venc_get_gop_default_attr(ot_venc_gop_attr *gop_attr)
{
    td_s32 ret;
    ot_venc_gop_mode gop_mode;

    gop_mode = OT_VENC_GOP_MODE_NORMAL_P; if (TD_FALSE) {
        return TD_FAILURE;
    }
    ret = sample_comm_venc_get_gop_attr(gop_mode, gop_attr);
    if (ret != TD_SUCCESS) {
        sample_print("Venc get gop default attr for mode %d failed return %#x!\n", gop_mode, ret);
        return ret;
    }

    return TD_SUCCESS;
}

static td_s32 sample_venc_intra_refresh_set_video_param(ot_size *enc_size,
    ot_venc_intra_refresh_mode *intra_refresh_mode, sample_comm_venc_chn_param *chn_param, td_s32 len)
{
    td_s32 ret;
    ot_venc_gop_attr gop_attr;

    if ((ret = sample_comm_venc_get_gop_default_attr(&gop_attr)) != TD_SUCCESS) {
        return ret;
    }

    if (get_intra_refresh_mode(intra_refresh_mode) != TD_SUCCESS) {
        return TD_FAILURE;
    }

    if (len > CHN_NUM_MAX) {
        sample_print("the num of venc_create_param is beyond CHN_NUM_MAX !\n");
        return TD_FAILURE;
    }

    if ((ret = sample_venc_set_video_param(enc_size, chn_param, CHN_NUM_MAX, gop_attr, TD_FALSE)) != TD_SUCCESS) {
        return ret;
    }

    return TD_SUCCESS;
}

static td_s32 sample_venc_intra_refresh_start_encode(ot_size *enc_size, td_s32 chn_num_max, ot_vpss_grp vpss_grp,
    sample_venc_vpss_chn *venc_vpss_chn)
{
    td_s32 ret;
    ot_venc_intra_refresh_mode intra_refresh_mode;
    sample_comm_venc_chn_param chn_param[CHN_NUM_MAX] = {0};
    sample_comm_venc_chn_param *h265_chn_param = TD_NULL;
    sample_comm_venc_chn_param *h264_chn_param = TD_NULL;

    ret = sample_venc_intra_refresh_set_video_param(enc_size, &intra_refresh_mode, chn_param, CHN_NUM_MAX);
    if (ret != TD_SUCCESS) {
        return ret;
    }

    /* encode h.265 */
    h265_chn_param = &(chn_param[0]);
    if ((ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[0], h265_chn_param)) != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        return ret;
    }

    ret = sample_venc_vpss_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[0], venc_vpss_chn->venc_chn[0]);
    if (ret != TD_SUCCESS) {
        goto EXIT_VENC_H265_STOP;
    }

    /* set intra refresh mode for chn 0 */
    if ((ret = sample_venc_set_intra_refresh(venc_vpss_chn->venc_chn[0], intra_refresh_mode)) != TD_SUCCESS) {
        goto EXIT_VENC_H265_UnBind;
    }

    /* encode h.264 */
    h264_chn_param = &(chn_param[1]);
    if ((ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[1], h264_chn_param)) != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        goto EXIT_VENC_H265_UnBind;
    }

    ret = sample_venc_vpss_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[1], venc_vpss_chn->venc_chn[1]);
    if (ret != TD_SUCCESS) {
        goto EXIT_VENC_H264_STOP;
    }

    /* set intra refresh mode for chn 1 */
    if ((ret = sample_venc_set_intra_refresh(venc_vpss_chn->venc_chn[1], intra_refresh_mode)) != TD_SUCCESS) {
        goto EXIT_VENC_H264_UnBind;
    }

    /* *****************************************
     stream save process
    ***************************************** */
    if ((ret = sample_comm_venc_start_get_stream(venc_vpss_chn->venc_chn, CHN_NUM_MAX)) != TD_SUCCESS) {
        sample_print("Start Venc failed!\n");
        goto EXIT_VENC_H264_UnBind;
    }

    return ret;

EXIT_VENC_H264_UnBind:
    sample_comm_vpss_un_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[1], venc_vpss_chn->venc_chn[1]);
EXIT_VENC_H264_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[1]);
EXIT_VENC_H265_UnBind:
    sample_comm_vpss_un_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[0], venc_vpss_chn->venc_chn[0]);
EXIT_VENC_H265_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[0]);

    return ret;
}

/* *****************************************************************************
 * function : intra_refresh:H.265e@1080P@30fps(row) + h264e@1080P@30fps(column).
 * **************************************************************************** */
static td_s32 sample_venc_intra_refresh(td_void)
{
    td_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg;
    ot_size enc_size[CHN_NUM_MAX], vi_size;
    const ot_vi_pipe vi_pipe = 0;
    const ot_vi_chn vi_chn = 0;
    const ot_vpss_grp vpss_grp = 0;
    sample_venc_vpss_chn_attr vpss_param;
    sample_venc_vb_attr vb_attr = { 0 };
    sample_venc_vpss_chn venc_vpss_chn = { 0 };

    sample_set_venc_vpss_chn(&venc_vpss_chn, CHN_NUM_MAX);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    sample_comm_vi_get_size_by_sns_type(sns_type, &vi_size);
    /* *****************************************
      step 0: related parameter ready
    ***************************************** */
    if ((ret = sample_venc_init_param(enc_size, CHN_NUM_MAX, &vi_size, &vpss_param)) != TD_SUCCESS) {
        return ret;
    }

    /* *****************************************
      step 1: init sys alloc common vb
    ***************************************** */
    get_vb_attr(&vi_size, &vpss_param, &vb_attr);

    if ((ret = sample_venc_sys_init(&vb_attr)) != TD_SUCCESS) {
        sample_print("Init SYS err for %#x!\n", ret);
        return ret;
    }

    if ((ret = sample_venc_vi_init(&vi_cfg)) != TD_SUCCESS) {
        sample_print("Init VI err for %#x!\n", ret);
        goto EXIT_SYS_STOP;
    }

    if ((ret = sample_venc_vpss_init(vpss_grp, &vpss_param)) != TD_SUCCESS) {
        sample_print("Init VPSS err for %#x!\n", ret);
        goto EXIT_VI_STOP;
    }

    if ((ret = sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0)) != TD_SUCCESS) {
        sample_print("VI Bind VPSS err for %#x!\n", ret);
        goto EXIT_VPSS_STOP;
    }

    /* *****************************************
    start stream venc
    ***************************************** */
    ret = sample_venc_intra_refresh_start_encode(enc_size, CHN_NUM_MAX, vpss_grp, &venc_vpss_chn);
    if (ret != TD_SUCCESS) {
        goto EXIT_VI_VPSS_UNBIND;
    }

    /* *****************************************
     exit process
    ***************************************** */
    sample_venc_exit_process();
    sample_venc_unbind_vpss_stop(vpss_grp, &venc_vpss_chn);

EXIT_VI_VPSS_UNBIND:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0);
EXIT_VPSS_STOP:
    sample_venc_vpss_deinit(vpss_grp, &vpss_param);
EXIT_VI_STOP:
    sample_venc_vi_deinit(&vi_cfg);
EXIT_SYS_STOP:
    sample_comm_sys_exit();

    return ret;
}

static td_void sample_venc_roi_attr_init(ot_venc_roi_attr *roi_attr)
{
    roi_attr->is_abs_qp = TD_TRUE;
    roi_attr->enable = TD_TRUE;
    roi_attr->qp = 30; /* 30: qp value */
    roi_attr->idx = 0;
    roi_attr->rect.x = 64;       /* 64: rect.x value */
    roi_attr->rect.y = 64;       /* 64: rect.y value */
    roi_attr->rect.height = 256; /* 256: rect.height value */
    roi_attr->rect.width = 256;  /* 256: rect.width value */
}

static td_void sample_venc_roi_bg_frame_rate_init(ot_venc_roi_bg_frame_rate *roi_bg_frame_rate)
{
    roi_bg_frame_rate->src_frame_rate = 30; /* 30: src_frame_rate value */
    roi_bg_frame_rate->dst_frame_rate = 15; /* 15: dst_frame_rate value */
}

static td_s32 sample_venc_set_roi_attr(ot_venc_chn venc_chn)
{
    td_s32 ret;
    ot_venc_roi_attr roi_attr;
    ot_venc_roi_bg_frame_rate roi_bg_frame_rate;

    if ((ret = ss_mpi_venc_get_roi_attr(venc_chn, 0, &roi_attr)) != TD_SUCCESS) { /* 0: roi index */
        sample_print("chn %d Get Roi Attr failed for %#x!\n", venc_chn, ret);
        return ret;
    }

    sample_venc_roi_attr_init(&roi_attr);

    if ((ret = ss_mpi_venc_set_roi_attr(venc_chn, &roi_attr)) != TD_SUCCESS) {
        sample_print("chn %d Set Roi Attr failed for %#x!\n", venc_chn, ret);
        return ret;
    }

    if ((ret = ss_mpi_venc_get_roi_bg_frame_rate(venc_chn, &roi_bg_frame_rate)) != TD_SUCCESS) {
        sample_print("chn %d Get Roi BgFrameRate failed for %#x!\n", venc_chn, ret);
        return ret;
    }

    sample_venc_roi_bg_frame_rate_init(&roi_bg_frame_rate);

    if ((ret = ss_mpi_venc_set_roi_bg_frame_rate(venc_chn, &roi_bg_frame_rate)) != TD_SUCCESS) {
        sample_print("chn %d Set Roi BgFrameRate failed for %#x!\n", venc_chn, ret);
        return ret;
    }

    return TD_SUCCESS;
}

static td_s32 sample_venc_roi_bg_start_encode(ot_size *enc_size, td_s32 chn_num_max, ot_vpss_grp vpss_grp,
    sample_venc_vpss_chn *venc_vpss_chn)
{
    td_s32 ret;
    ot_venc_gop_attr gop_attr;
    sample_comm_venc_chn_param chn_param[CHN_NUM_MAX] = {0};

    if ((ret = sample_comm_venc_get_gop_default_attr(&gop_attr)) != TD_SUCCESS) {
        return ret;
    }

    if ((ret = sample_venc_set_video_param(enc_size, chn_param, CHN_NUM_MAX, gop_attr, TD_FALSE)) != TD_SUCCESS) {
        return ret;
    }

    /* encode h.265 */
    if ((ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[0], &(chn_param[0]))) != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        return ret;
    }

    ret = sample_venc_vpss_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[0], venc_vpss_chn->venc_chn[0]);
    if (ret != TD_SUCCESS) {
        goto EXIT_VENC_H265_STOP;
    }

    /* set roi bg frame rate for chn 0 */
    if ((ret = sample_venc_set_roi_attr(venc_vpss_chn->venc_chn[0])) != TD_SUCCESS) {
        goto EXIT_VENC_H265_UnBind;
    }

    /* encode h.264 */
    if ((ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[1], &(chn_param[1]))) != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        goto EXIT_VENC_H265_UnBind;
    }

    ret = sample_venc_vpss_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[1], venc_vpss_chn->venc_chn[1]);
    if (ret != TD_SUCCESS) {
        goto EXIT_VENC_H264_STOP;
    }

    /* set roi bg frame rate for chn 1 */
    if ((ret = sample_venc_set_roi_attr(venc_vpss_chn->venc_chn[1])) != TD_SUCCESS) {
        goto EXIT_VENC_H264_UnBind;
    }

    /* *****************************************
     stream save process
    ***************************************** */
    if ((ret = sample_comm_venc_start_get_stream(venc_vpss_chn->venc_chn, CHN_NUM_MAX)) != TD_SUCCESS) {
        sample_print("Start Venc failed!\n");
        goto EXIT_VENC_H264_UnBind;
    }

    return ret;

EXIT_VENC_H264_UnBind:
    sample_comm_vpss_un_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[1], venc_vpss_chn->venc_chn[1]);
EXIT_VENC_H264_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[1]);
EXIT_VENC_H265_UnBind:
    sample_comm_vpss_un_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[0], venc_vpss_chn->venc_chn[0]);
EXIT_VENC_H265_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[0]);

    return ret;
}

/* *****************************************************************************
 * function : roi_bg_frame_rate:H.265e@1080P@30fps + H.264@1080P@30fps.
 * **************************************************************************** */
static td_s32 sample_venc_roi_bg(td_void)
{
    td_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg;
    const ot_vi_pipe vi_pipe = 0;
    const ot_vi_chn vi_chn = 0;
    const ot_vpss_grp vpss_grp = 0;
    sample_venc_vpss_chn_attr vpss_param;
    sample_venc_vb_attr vb_attr = { 0 };
    ot_size enc_size[CHN_NUM_MAX], vi_size;
    sample_venc_vpss_chn venc_vpss_chn = { 0 };

    sample_set_venc_vpss_chn(&venc_vpss_chn, CHN_NUM_MAX);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    sample_comm_vi_get_size_by_sns_type(sns_type, &vi_size);
    /* *****************************************
      step 0: related parameter ready
    ***************************************** */
    if ((ret = sample_venc_init_param(enc_size, CHN_NUM_MAX, &vi_size, &vpss_param)) != TD_SUCCESS) {
        return ret;
    }

    /* *****************************************
      step 1: init sys alloc common vb
    ***************************************** */
    get_vb_attr(&vi_size, &vpss_param, &vb_attr);

    if ((ret = sample_venc_sys_init(&vb_attr)) != TD_SUCCESS) {
        sample_print("Init SYS err for %#x!\n", ret);
        return ret;
    }

    if ((ret = sample_venc_vi_init(&vi_cfg)) != TD_SUCCESS) {
        sample_print("Init VI err for %#x!\n", ret);
        goto EXIT_SYS_STOP;
    }

    if ((ret = sample_venc_vpss_init(vpss_grp, &vpss_param)) != TD_SUCCESS) {
        sample_print("Init VPSS err for %#x!\n", ret);
        goto EXIT_VI_STOP;
    }

    if ((ret = sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0)) != TD_SUCCESS) {
        sample_print("VI Bind VPSS err for %#x!\n", ret);
        goto EXIT_VPSS_STOP;
    }

    /* *****************************************
    start stream venc
    ***************************************** */
    if ((ret = sample_venc_roi_bg_start_encode(enc_size, CHN_NUM_MAX, vpss_grp, &venc_vpss_chn)) != TD_SUCCESS) {
        goto EXIT_VI_VPSS_UNBIND;
    }

    /* *****************************************
     exit process
    ***************************************** */
    sample_venc_exit_process();
    sample_venc_unbind_vpss_stop(vpss_grp, &venc_vpss_chn);

EXIT_VI_VPSS_UNBIND:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0);
EXIT_VPSS_STOP:
    sample_venc_vpss_deinit(vpss_grp, &vpss_param);
EXIT_VI_STOP:
    sample_venc_vi_deinit(&vi_cfg);
EXIT_SYS_STOP:
    sample_comm_sys_exit();

    return ret;
}

static td_s32 sample_venc_set_debreath_effect(ot_venc_chn venc_chn, td_bool enable)
{
    td_s32 ret;
    ot_venc_debreath_effect debreath_effect;

    if ((ret = ss_mpi_venc_get_debreath_effect(venc_chn, &debreath_effect)) != TD_SUCCESS) {
        sample_print("Get debreath_effect failed for %#x!\n", ret);
        return ret;
    }

    if (enable) {
        debreath_effect.enable = TD_TRUE;
        debreath_effect.strength0 = 3;  /* 3 : param */
        debreath_effect.strength1 = 20; /* 20 : param */
    } else {
        debreath_effect.enable = TD_FALSE;
    }

    if ((ret = ss_mpi_venc_set_debreath_effect(venc_chn, &debreath_effect)) != TD_SUCCESS) {
        sample_print("Set debreath_effect failed for %#x!\n", ret);
        return ret;
    }

    return TD_SUCCESS;
}

static td_s32 sample_venc_debreath_effect_start_encode(ot_size *enc_size, td_s32 chn_num_max,
    ot_vpss_grp vpss_grp, sample_venc_vpss_chn *venc_vpss_chn)
{
    td_s32 ret;
    ot_venc_gop_attr gop_attr;
    sample_comm_venc_chn_param chn_param[CHN_NUM_MAX] = {0};

    if ((ret = sample_comm_venc_get_gop_default_attr(&gop_attr)) != TD_SUCCESS) {
        return ret;
    }

    if ((ret = sample_venc_set_video_param(enc_size, chn_param, CHN_NUM_MAX, gop_attr, TD_FALSE)) != TD_SUCCESS) {
        return ret;
    }

    /* encode h.265 */
    if ((ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[0], &(chn_param[0]))) != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        return ret;
    }

    ret = sample_venc_vpss_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[0], venc_vpss_chn->venc_chn[0]);
    if (ret != TD_SUCCESS) {
        goto EXIT_VENC_H265_STOP;
    }

    /* set intra refresh mode for chn 0 */
    if ((ret = sample_venc_set_debreath_effect(venc_vpss_chn->venc_chn[0], TD_TRUE)) != TD_SUCCESS) {
        goto EXIT_VENC_H265_UnBind;
    }

    /* encode h.264 */
    if ((ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[1], &(chn_param[1]))) != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        goto EXIT_VENC_H265_UnBind;
    }

    ret = sample_venc_vpss_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[1], venc_vpss_chn->venc_chn[1]);
    if (ret != TD_SUCCESS) {
        goto EXIT_VENC_H264_STOP;
    }

    /* set intra refresh mode for chn 1 */
    if ((ret = sample_venc_set_debreath_effect(venc_vpss_chn->venc_chn[1], TD_FALSE)) != TD_SUCCESS) {
        goto EXIT_VENC_H264_UnBind;
    }

    /* *****************************************
     stream save process
    ***************************************** */
    if ((ret = sample_comm_venc_start_get_stream(venc_vpss_chn->venc_chn, CHN_NUM_MAX)) != TD_SUCCESS) {
        sample_print("Start Venc failed!\n");
        goto EXIT_VENC_H264_UnBind;
    }

    return TD_SUCCESS;

EXIT_VENC_H264_UnBind:
    sample_comm_vpss_un_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[1], venc_vpss_chn->venc_chn[1]);
EXIT_VENC_H264_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[1]);
EXIT_VENC_H265_UnBind:
    sample_comm_vpss_un_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[0], venc_vpss_chn->venc_chn[0]);
EXIT_VENC_H265_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[0]);

    return ret;
}

static td_s32 sample_venc_debreath_effect(td_void)
{
    td_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg;
    const ot_vi_pipe vi_pipe = 0;
    const ot_vi_chn vi_chn = 0;
    const ot_vpss_grp vpss_grp = 0;
    sample_venc_vpss_chn_attr vpss_param;
    sample_venc_vb_attr vb_attr = { 0 };
    ot_size enc_size[CHN_NUM_MAX], vi_size;
    sample_venc_vpss_chn venc_vpss_chn = { 0 };

    sample_set_venc_vpss_chn(&venc_vpss_chn, CHN_NUM_MAX);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    sample_comm_vi_get_size_by_sns_type(sns_type, &vi_size);
    /* *****************************************
      step 0: related parameter ready
    ***************************************** */
    if ((ret = sample_venc_init_param(enc_size, CHN_NUM_MAX, &vi_size, &vpss_param)) != TD_SUCCESS) {
        return ret;
    }

    /* *****************************************
      step 1: init sys alloc common vb
    ***************************************** */
    get_vb_attr(&vi_size, &vpss_param, &vb_attr);
    if ((ret = sample_venc_sys_init(&vb_attr)) != TD_SUCCESS) {
        sample_print("Init SYS err for %#x!\n", ret);
        return ret;
    }

    if ((ret = sample_venc_vi_init(&vi_cfg)) != TD_SUCCESS) {
        sample_print("Init VI err for %#x!\n", ret);
        goto EXIT_SYS_STOP;
    }

    if ((ret = sample_venc_vpss_init(vpss_grp, &vpss_param)) != TD_SUCCESS) {
        sample_print("Init VPSS err for %#x!\n", ret);
        goto EXIT_VI_STOP;
    }

    if ((ret = sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0)) != TD_SUCCESS) {
        sample_print("VI Bind VPSS err for %#x!\n", ret);
        goto EXIT_VPSS_STOP;
    }

    /* *****************************************
    start stream venc
    ***************************************** */
    ret = sample_venc_debreath_effect_start_encode(enc_size, CHN_NUM_MAX, vpss_grp, &venc_vpss_chn);
    if (ret != TD_SUCCESS) {
        goto EXIT_VI_VPSS_UNBIND;
    }

    /* *****************************************
     exit process
    ***************************************** */
    sample_venc_exit_process();
    sample_venc_unbind_vpss_stop(vpss_grp, &venc_vpss_chn);

EXIT_VI_VPSS_UNBIND:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0);
EXIT_VPSS_STOP:
    sample_venc_vpss_deinit(vpss_grp, &vpss_param);
EXIT_VI_STOP:
    sample_venc_vi_deinit(&vi_cfg);
EXIT_SYS_STOP:
    sample_comm_sys_exit();

    return ret;
}

static td_void sample_venc_set_jpeg_param(ot_size *enc_size, td_s32 enc_len,
    sample_comm_venc_chn_param *venc_create_param, td_s32 creat_size, ot_venc_gop_attr gop_attr)
{
    td_u32 profile[CHN_NUM_MAX] = {0, 0};
    td_bool share_buf_en = TD_FALSE;
    ot_payload_type payload[CHN_NUM_MAX] = {OT_PT_MJPEG, OT_PT_MJPEG};
    sample_rc rc_mode = 0;

    if ((rc_mode = SAMPLE_RC_CBR, TD_SUCCESS) != TD_SUCCESS) {
        return;
    }

    venc_create_param[0].frame_rate = 30; /* 30: is a number */
    venc_create_param[0].gop = 60; /* 60: is a number */
    venc_create_param[0].stats_time = 2; /* 2: is a number */
    venc_create_param[0].gop_attr = gop_attr;
    venc_create_param[0].type = payload[0];
    venc_create_param[0].size = sample_comm_sys_get_pic_enum(&enc_size[0]);
    venc_create_param[0].rc_mode = rc_mode;
    venc_create_param[0].profile = profile[0];
    venc_create_param[0].is_rcn_ref_share_buf = share_buf_en;

    if (enc_len == 1) {
        return;
    }

    venc_create_param[1].frame_rate = 30; /* 30: is a number */
    venc_create_param[1].gop = 60; /* 60: is a number */
    venc_create_param[1].stats_time = 2; /* 2: is a number */
    venc_create_param[1].gop_attr = gop_attr;
    venc_create_param[1].type = payload[1];
    venc_create_param[1].size = sample_comm_sys_get_pic_enum(&enc_size[1]);
    venc_create_param[1].rc_mode = rc_mode;
    venc_create_param[1].profile = profile[1];
    venc_create_param[1].is_rcn_ref_share_buf = share_buf_en;
}

static td_void sample_venc_mjpeg_roi_param_init(ot_venc_jpeg_roi_attr *roi_param)
{
    roi_param->idx = 0;
    roi_param->enable = TD_TRUE;
    roi_param->level = 0;
    roi_param->rect.x = 0;
    roi_param->rect.y = 0;
    roi_param->rect.width = 1280; /* 1280: rect.width value */
    roi_param->rect.height = 720; /* 720: rect.height value */
}

static td_s32 sample_venc_set_mjpeg_roi(ot_venc_chn venc_chn)
{
    td_s32 ret;
    const td_u32 idx = 0;
    ot_venc_jpeg_roi_attr roi_param;

    if ((ret = ss_mpi_venc_get_jpeg_roi_attr(venc_chn, idx, &roi_param)) != TD_SUCCESS) {
        sample_print("Get roi_param failed for %#x!\n", ret);
        return ret;
    }

    sample_venc_mjpeg_roi_param_init(&roi_param);

    if ((ret = ss_mpi_venc_set_jpeg_roi_attr(venc_chn, &roi_param)) != TD_SUCCESS) {
        sample_print("Set roi_param failed for %#x!\n", ret);
    }

    return ret;
}

static td_s32 sample_venc_mjpeg_roi_set_start_encode(ot_size *enc_size, td_s32 chn_num, ot_vpss_grp vpss_grp,
    sample_venc_vpss_chn *venc_vpss_chn)
{
    td_s32 ret;
    ot_venc_gop_attr gop_attr;
    sample_comm_venc_chn_param venc_create_param[CHN_NUM_MAX] = {0};
    sample_comm_venc_chn_param venc_chn0_param, venc_chn1_param;

    if ((ret = sample_comm_venc_get_gop_attr(OT_VENC_GOP_MODE_NORMAL_P, &gop_attr)) != TD_SUCCESS) {
        return ret;
    }

    sample_venc_set_jpeg_param(enc_size, chn_num, venc_create_param, CHN_NUM_MAX, gop_attr);

    venc_chn0_param = venc_create_param[0];
    ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[0], &venc_chn0_param);
    if (ret != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        return ret;
    }

    ret = sample_venc_vpss_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[0], venc_vpss_chn->venc_chn[0]);
    if (ret != TD_SUCCESS) {
        goto EXIT_VENC_CHN0_STOP;
    }

    /* set intra refresh mode for chn 0 */
    if ((ret = sample_venc_set_mjpeg_roi(venc_vpss_chn->venc_chn[0])) != TD_SUCCESS) {
        goto EXIT_VENC_CHN_0_UnBind;
    }

    venc_chn1_param = venc_create_param[1];
    ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[1], &venc_chn1_param);
    if (ret != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        goto EXIT_VENC_CHN_0_UnBind;
    }

    ret = sample_venc_vpss_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[1], venc_vpss_chn->venc_chn[1]);
    if (ret != TD_SUCCESS) {
        goto EXIT_VENC_CHN_1_STOP;
    }

    /* *****************************************
     stream save process
    ***************************************** */
    if ((ret = sample_comm_venc_start_get_stream(venc_vpss_chn->venc_chn, CHN_NUM_MAX)) != TD_SUCCESS) {
        goto EXIT_VENC_CHN_1_UnBind;
    }

    return TD_SUCCESS;

EXIT_VENC_CHN_1_UnBind:
    sample_comm_vpss_un_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[1], venc_vpss_chn->venc_chn[1]);
EXIT_VENC_CHN_1_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[1]);
EXIT_VENC_CHN_0_UnBind:
    sample_comm_vpss_un_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[0], venc_vpss_chn->venc_chn[0]);
EXIT_VENC_CHN0_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[0]);

    return ret;
}

static td_s32 sample_venc_mjpeg_roi_set(td_void)
{
    td_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg;
    ot_size vi_size;
    const ot_vi_pipe vi_pipe = 0;
    const ot_vi_chn vi_chn = 0;
    const ot_vpss_grp vpss_grp = 0;
    sample_venc_vpss_chn_attr vpss_param;
    sample_venc_vb_attr vb_attr = { 0 };
    ot_size enc_size[CHN_NUM_MAX];
    sample_venc_vpss_chn venc_vpss_chn = { 0 };

    sample_set_venc_vpss_chn(&venc_vpss_chn, CHN_NUM_MAX);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    sample_comm_vi_get_size_by_sns_type(sns_type, &vi_size);

    /* *****************************************
      step 0: related parameter ready
    ***************************************** */
    ret = sample_venc_init_param(enc_size, CHN_NUM_MAX, &vi_size, &vpss_param);
    if (ret != TD_SUCCESS) {
        return ret;
    }

    /* *****************************************
      step 1: init sys alloc common vb
    ***************************************** */
    get_vb_attr(&vi_size, &vpss_param, &vb_attr);

    if ((ret = sample_venc_sys_init(&vb_attr)) != TD_SUCCESS) {
        return ret;
    }

    if ((ret = sample_venc_vi_init(&vi_cfg)) != TD_SUCCESS) {
        goto EXIT_SYS_STOP;
    }

    if ((ret = sample_venc_vpss_init(vpss_grp, &vpss_param)) != TD_SUCCESS) {
        goto EXIT_VI_STOP;
    }

    if ((ret = sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0)) != TD_SUCCESS) {
        goto EXIT_VPSS_STOP;
    }

    /* *****************************************
    start stream venc
    ***************************************** */
    ret = sample_venc_mjpeg_roi_set_start_encode(enc_size, CHN_NUM_MAX, vpss_grp, &venc_vpss_chn);
    if (ret != TD_SUCCESS) {
        goto EXIT_VI_VPSS_UNBIND;
    }

    /* *****************************************
     exit process
    ***************************************** */
    sample_venc_exit_process();
    sample_venc_unbind_vpss_stop(vpss_grp, &venc_vpss_chn);

EXIT_VI_VPSS_UNBIND:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0);
EXIT_VPSS_STOP:
    sample_venc_vpss_deinit(vpss_grp, &vpss_param);
EXIT_VI_STOP:
    sample_venc_vi_deinit(&vi_cfg);
EXIT_SYS_STOP:
    sample_comm_sys_exit();

    return ret;
}

static td_void sample_venc_mjpeg_roi_attr_init(ot_venc_jpeg_roi_attr *roi_attr, td_s32 chn_num_max)
{
    if (chn_num_max < 2) { /* 2: roi_attr array len */
        sample_print("roi_attr array len not enough, need 2, current %d!\n", chn_num_max);
        return;
    }
    roi_attr[0].enable = 1;
    roi_attr[0].idx = 0;
    roi_attr[0].rect.x = 0;
    roi_attr[0].rect.y = 0;
    roi_attr[0].rect.width = 160;  /* 160: rect.width value */
    roi_attr[0].rect.height = 160; /* 160: rect.height value */
    roi_attr[0].level = 0;

    roi_attr[1].enable = 1;
    roi_attr[1].idx = 0;
    roi_attr[1].rect.x = 0;
    roi_attr[1].rect.y = 0;
    roi_attr[1].rect.width = 160;  /* 160: rect.width value */
    roi_attr[1].rect.height = 160; /* 160: rect.height value */
    roi_attr[1].level = 0;
}

static td_s32 sample_venc_mjpeg_roimap_start_encode(ot_vpss_grp vpss_grp, ot_venc_jpeg_roi_attr *roi_attr,
    ot_size *enc_size, td_s32 chn_num_max, sample_venc_vpss_chn *venc_vpss_chn)
{
    td_s32 ret;
    ot_venc_gop_attr gop_attr;
    sample_comm_venc_chn_param venc_create_param[CHN_NUM_MAX] = {0};
    sample_comm_venc_chn_param venc_chn0_param, venc_chn1_param;
    sample_venc_roimap_chn_info roimap_chn_info = { 0 };

    if ((ret = sample_comm_venc_get_gop_attr(OT_VENC_GOP_MODE_NORMAL_P, &gop_attr)) != TD_SUCCESS) {
        sample_print("Venc Get GopAttr for %#x!\n", ret);
        return ret;
    }

    sample_venc_set_jpeg_param(enc_size, chn_num_max, venc_create_param, CHN_NUM_MAX, gop_attr);

    venc_chn0_param = venc_create_param[0];
    ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[0], &venc_chn0_param);
    if (ret != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        return ret;
    }

    venc_chn1_param = venc_create_param[1];
    ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[1], &venc_chn1_param);
    if (ret != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        goto EXIT_VENC_CHN0_STOP;
    }

    roimap_chn_info.vpss_chn = venc_vpss_chn->vpss_chn;
    roimap_chn_info.venc_chn = venc_vpss_chn->venc_chn;
    roimap_chn_info.cnt = chn_num_max;
    ret = sample_comm_venc_send_roimap_frame(vpss_grp, roimap_chn_info, enc_size, roi_attr);
    if (ret != TD_SUCCESS) {
        sample_print("sample_comm_venc_qpmap_send_frame failed for %#x!\n", ret);
        goto EXIT_VENC_CHN1_STOP;
    }

    /* *****************************************
     stream save process
    ***************************************** */
    if ((ret = sample_comm_venc_start_get_stream(venc_vpss_chn->venc_chn, CHN_NUM_MAX)) != TD_SUCCESS) {
        sample_print("Start Venc failed!\n");
        goto EXIT_VENC_CHN1_STOP;
    }

    return TD_SUCCESS;

EXIT_VENC_CHN1_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[1]);
EXIT_VENC_CHN0_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[0]);

    return ret;
}

static td_void sample_venc_mjpeg_roimap_exit_process()
{
    printf("please press twice ENTER to exit this sample\n");
    (td_void)getchar();

    if (g_sample_venc_exit != TD_TRUE) {
        (td_void)getchar();
    }

    sample_comm_venc_stop_send_roimap_frame();
    sample_comm_venc_stop_get_stream(CHN_NUM_MAX);
}

static td_s32 sample_venc_mjpeg_roimap(td_void)
{
    td_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg;
    ot_size vi_size;
    const ot_vi_pipe vi_pipe = 0;
    const ot_vi_chn vi_chn = 0;
    const ot_vpss_grp vpss_grp = 0;
    sample_venc_vpss_chn_attr vpss_param;
    sample_venc_vb_attr vb_attr = { 0 };
    ot_venc_jpeg_roi_attr roi_attr[CHN_NUM_MAX];
    ot_size enc_size[CHN_NUM_MAX];
    sample_venc_vpss_chn venc_vpss_chn = { 0 };

    sample_set_venc_vpss_chn(&venc_vpss_chn, CHN_NUM_MAX);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    sample_comm_vi_get_size_by_sns_type(sns_type, &vi_size);

    sample_venc_mjpeg_roi_attr_init(roi_attr, CHN_NUM_MAX);

    ret = sample_venc_init_param(enc_size, CHN_NUM_MAX, &vi_size, &vpss_param);
    if (ret != TD_SUCCESS) {
        return ret;
    }

    /* *****************************************
      step 1: init sys alloc common vb
    ***************************************** */
    get_vb_attr(&vi_size, &vpss_param, &vb_attr);

    if ((ret = sample_venc_sys_init(&vb_attr)) != TD_SUCCESS) {
        return ret;
    }

    if ((ret = sample_venc_vi_init(&vi_cfg)) != TD_SUCCESS) {
        goto EXIT_SYS_STOP;
    }

    if ((ret = sample_venc_vpss_init(vpss_grp, &vpss_param)) != TD_SUCCESS) {
        goto EXIT_VI_STOP;
    }

    if ((ret = sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0)) != TD_SUCCESS) {
        goto EXIT_VPSS_STOP;
    }

    ret = sample_venc_mjpeg_roimap_start_encode(vpss_grp, roi_attr, enc_size, CHN_NUM_MAX, &venc_vpss_chn);
    if (ret != TD_SUCCESS) {
        goto EXIT_VI_VPSS_UNBIND;
    }

    /* *****************************************
     exit process
    ***************************************** */
    sample_venc_mjpeg_roimap_exit_process();
    sample_venc_stop(&venc_vpss_chn, CHN_NUM_MAX);

EXIT_VI_VPSS_UNBIND:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0);
EXIT_VPSS_STOP:
    sample_venc_vpss_deinit(vpss_grp, &vpss_param);
EXIT_VI_STOP:
    sample_venc_vi_deinit(&vi_cfg);
EXIT_SYS_STOP:
    sample_comm_sys_exit();

    return ret;
}

static td_s32 sample_venc_mjpeg_mosaic_map_start_encode(ot_size *enc_size, td_s32 enc_len, ot_vpss_grp vpss_grp,
    sample_venc_vpss_chn *venc_vpss_chn)
{
    td_s32 ret;
    ot_venc_gop_attr gop_attr;
    sample_comm_venc_chn_param chn_param[CHN_NUM_MAX] = {0};
    sample_comm_venc_chn_param venc_chn0_param, venc_chn1_param;

    if ((ret = sample_comm_venc_get_gop_attr(OT_VENC_GOP_MODE_NORMAL_P, &gop_attr)) != TD_SUCCESS) {
        sample_print("Venc Get GopAttr for %#x!\n", ret);
        return ret;
    }
    // init default venc chn attr
    sample_venc_set_jpeg_param(enc_size, enc_len, chn_param, CHN_NUM_MAX, gop_attr);

    /* encode mjpeg */
    venc_chn0_param = chn_param[0];
    ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[0], &venc_chn0_param);
    if (ret != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        return ret;
    }

    venc_chn1_param = chn_param[1];
    ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[1], &venc_chn1_param);
    if (ret != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        goto EXIT_VENC_CHN0_STOP;
    }

    ret = sample_comm_venc_mosaic_map_send_frame(vpss_grp, venc_vpss_chn->vpss_chn,
        venc_vpss_chn->venc_chn, CHN_NUM_MAX, enc_size);
    if (ret != TD_SUCCESS) {
        sample_print("sample_comm_venc_qpmap_send_frame failed for %#x!\n", ret);
        goto EXIT_VENC_CHN1_STOP;
    }

    /* *****************************************
     stream save process
    ***************************************** */
    if ((ret = sample_comm_venc_start_get_stream(venc_vpss_chn->venc_chn, CHN_NUM_MAX)) != TD_SUCCESS) {
        sample_print("Start Venc failed!\n");
        goto EXIT_VENC_CHN1_STOP;
    }

    return TD_SUCCESS;

EXIT_VENC_CHN1_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[1]);
EXIT_VENC_CHN0_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[0]);

    return ret;
}

static td_s32 sample_venc_mjpeg_mosaic_map(td_void)
{
    td_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    const ot_vi_pipe vi_pipe = 0;
    const ot_vi_chn vi_chn = 0;
    const ot_vpss_grp vpss_grp = 0;
    sample_venc_vpss_chn_attr vpss_param;
    sample_venc_vb_attr vb_attr = { 0 };
    sample_venc_vpss_chn venc_vpss_chn = { 0 };
    ot_size enc_size[CHN_NUM_MAX], vi_size;
    sample_vi_cfg vi_cfg;

    sample_set_venc_vpss_chn(&venc_vpss_chn, CHN_NUM_MAX);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    sample_comm_vi_get_size_by_sns_type(sns_type, &vi_size);
    /* *****************************************
      step 0: related parameter ready
    ***************************************** */
    if ((ret = sample_venc_init_param(enc_size, CHN_NUM_MAX, &vi_size, &vpss_param)) != TD_SUCCESS) {
        return ret;
    }
    /* *****************************************
      step 1: init sys alloc common vb
    ***************************************** */
    get_vb_attr(&vi_size, &vpss_param, &vb_attr);
    if ((ret = sample_venc_sys_init(&vb_attr)) != TD_SUCCESS) {
        sample_print("Init SYS err for %#x!\n", ret);
        return ret;
    }
    if ((ret = sample_venc_vi_init(&vi_cfg)) != TD_SUCCESS) {
        sample_print("Init VI err for %#x!\n", ret);
        goto EXIT_SYS_STOP;
    }

    if ((ret = sample_venc_vpss_init(vpss_grp, &vpss_param)) != TD_SUCCESS) {
        sample_print("Init VPSS err for %#x!\n", ret);
        goto EXIT_VI_STOP;
    }
    if ((ret = sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp, venc_vpss_chn.vpss_chn[0])) != TD_SUCCESS) {
        sample_print("VI Bind VPSS err for %#x!\n", ret);
        goto EXIT_VPSS_STOP;
    }
    /* *****************************************
    start stream venc
    ***************************************** */
    if ((ret = sample_venc_mjpeg_mosaic_map_start_encode(enc_size, CHN_NUM_MAX, vpss_grp, &venc_vpss_chn)) !=
        TD_SUCCESS) {
        goto EXIT_VI_VPSS_UNBIND;
    }
    /* *****************************************
     exit process
    ***************************************** */
    sample_venc_mosaic_map_exit_process(CHN_NUM_MAX);
    sample_venc_stop(&venc_vpss_chn, CHN_NUM_MAX);

EXIT_VI_VPSS_UNBIND:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp, venc_vpss_chn.vpss_chn[0]);
EXIT_VPSS_STOP:
    sample_venc_vpss_deinit(vpss_grp, &vpss_param);
EXIT_VI_STOP:
    sample_venc_vi_deinit(&vi_cfg);
EXIT_SYS_STOP:
    sample_comm_sys_exit();

    return ret;
}

static td_s32 sample_venc_mosaic_map_start_encode(ot_size *enc_size, td_s32 chn_num_max, ot_vpss_grp vpss_grp,
    sample_venc_vpss_chn *venc_vpss_chn)
{
    td_s32 ret;
    ot_venc_gop_mode gop_mode;
    ot_venc_gop_attr gop_attr;
    sample_comm_venc_chn_param chn_param[CHN_NUM_MAX] = {0};

    gop_mode = OT_VENC_GOP_MODE_NORMAL_P; if (TD_FALSE) {
        return TD_FAILURE;
    }
    if ((ret = sample_comm_venc_get_gop_attr(gop_mode, &gop_attr)) != TD_SUCCESS) {
        sample_print("Venc Get GopAttr for %#x!\n", ret);
        return ret;
    }

    if ((ret = sample_venc_set_video_param(enc_size, chn_param, CHN_NUM_MAX, gop_attr, TD_FALSE)) != TD_SUCCESS) {
        return ret;
    }

    /* encode h.265 */
    ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[0], &(chn_param[0]));
    if (ret != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        return ret;
    }

    /* encode h.264 */
    ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[1], &(chn_param[1]));
    if (ret != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        goto EXIT_VENC_H265_STOP;
    }

    ret = sample_comm_venc_mosaic_map_send_frame(vpss_grp, venc_vpss_chn->vpss_chn, venc_vpss_chn->venc_chn,
        CHN_NUM_MAX, enc_size);
    if (ret != TD_SUCCESS) {
        sample_print("sample_comm_venc_qpmap_send_frame failed for %#x!\n", ret);
        goto EXIT_VENC_H264_STOP;
    }

    /* *****************************************
     stream save process
    ***************************************** */
    if ((ret = sample_comm_venc_start_get_stream(venc_vpss_chn->venc_chn, CHN_NUM_MAX)) != TD_SUCCESS) {
        sample_print("Start Venc failed!\n");
        goto EXIT_VENC_H264_STOP;
    }

    return TD_SUCCESS;

EXIT_VENC_H264_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[1]);
EXIT_VENC_H265_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[0]);

    return ret;
}

static td_s32 sample_venc_mosaic_map(td_void)
{
    td_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg = { 0 };
    ot_size enc_size[CHN_NUM_MAX], vi_size;
    const ot_vi_pipe vi_pipe = 0;
    const ot_vi_chn vi_chn = 0;
    const ot_vpss_grp vpss_grp = 0;
    sample_venc_vpss_chn_attr vpss_param = { 0 };
    sample_venc_vb_attr vb_attr = { 0 };
    sample_venc_vpss_chn venc_vpss_chn = { 0 };

    sample_set_venc_vpss_chn(&venc_vpss_chn, CHN_NUM_MAX);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    sample_comm_vi_get_size_by_sns_type(sns_type, &vi_size);
    /* *****************************************
      step 0: related parameter ready
    ***************************************** */
    ret = sample_venc_init_param(enc_size, CHN_NUM_MAX, &vi_size, &vpss_param);
    if (ret != TD_SUCCESS) {
        return ret;
    }
    /* *****************************************
      step 1: init sys alloc common vb
    ***************************************** */
    get_vb_attr(&vi_size, &vpss_param, &vb_attr);
    if ((ret = sample_venc_sys_init(&vb_attr)) != TD_SUCCESS) {
        sample_print("Init SYS err for %#x!\n", ret);
        return ret;
    }

    if ((ret = sample_venc_vi_init(&vi_cfg)) != TD_SUCCESS) {
        sample_print("Init VI err for %#x!\n", ret);
        goto EXIT_SYS_STOP;
    }

    if ((ret = sample_venc_vpss_init(vpss_grp, &vpss_param)) != TD_SUCCESS) {
        sample_print("Init VPSS err for %#x!\n", ret);
        goto EXIT_VI_STOP;
    }

    if ((ret = sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0)) != TD_SUCCESS) {
        sample_print("VI Bind VPSS err for %#x!\n", ret);
        goto EXIT_VPSS_STOP;
    }

    /* *****************************************
    start stream venc
    ***************************************** */
    if ((ret = sample_venc_mosaic_map_start_encode(enc_size, CHN_NUM_MAX, vpss_grp, &venc_vpss_chn)) != TD_SUCCESS) {
        goto EXIT_VI_VPSS_UNBIND;
    }

    /* *****************************************
     exit process
    ***************************************** */
    sample_venc_mosaic_map_exit_process(CHN_NUM_MAX);
    // last stop venc chn
    sample_venc_stop(&venc_vpss_chn, CHN_NUM_MAX);

EXIT_VI_VPSS_UNBIND:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0);
EXIT_VPSS_STOP:
    sample_venc_vpss_deinit(vpss_grp, &vpss_param);
EXIT_VI_STOP:
    sample_venc_vi_deinit(&vi_cfg);
EXIT_SYS_STOP:
    sample_comm_sys_exit();

    return ret;
}

static td_s32 sample_venc_set_watermark(ot_venc_chn venc_chn, td_bool enable)
{
    td_s32 ret;
    ot_venc_watermark watermark = { 0 };

    if ((ret = ss_mpi_venc_get_watermark(venc_chn, &watermark)) != TD_SUCCESS) {
        sample_print("Get watermark failed for %#x!\n", ret);
        return ret;
    }

    if (enable) {
        td_char watermark_info[OT_VENC_MAX_WATERMARK_INFO_NUM];
        if (sprintf_s(watermark_info, OT_VENC_MAX_WATERMARK_INFO_NUM, "hello world:%d", venc_chn) < 0) {
            sample_print("Set watermark failed for %#x!\n", ret);
            return ret;
        }
        watermark.enable = TD_TRUE;
        watermark.strength = 2;         // 2: watermark strength
        watermark.component = 1;        // 1: watermark component
        watermark.template_period = 10; // 10 : watermark template_period

        if (sprintf_s((char *)watermark.info, OT_VENC_MAX_WATERMARK_INFO_NUM, watermark_info) < 0) {
            sample_print("Set watermark failed for %#x!\n", ret);
            return ret;
        }
        if ((ret = ss_mpi_venc_set_watermark(venc_chn, &watermark)) != TD_SUCCESS) {
            sample_print("Set watermark failed for %#x!\n", ret);
            return ret;
        }
    }
    return TD_SUCCESS;
}

static td_s32 sample_venc_watermark_start_encode(ot_size *enc_size, td_s32 chn_num_max, ot_vpss_grp vpss_grp,
    sample_venc_vpss_chn *venc_vpss_chn)
{
    td_s32 ret;
    ot_venc_gop_attr gop_attr;
    sample_comm_venc_chn_param chn_param[CHN_NUM_MAX] = {0};

    if ((ret = sample_comm_venc_get_gop_default_attr(&gop_attr)) != TD_SUCCESS) {
        return ret;
    }

    if ((ret = sample_venc_set_video_param(enc_size, chn_param, CHN_NUM_MAX, gop_attr, TD_FALSE)) != TD_SUCCESS) {
        return ret;
    }

    /* encode h.265 */
    if ((ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[0], &(chn_param[0]))) != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        return ret;
    }

    ret = sample_venc_vpss_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[0], venc_vpss_chn->venc_chn[0]);
    if (ret != TD_SUCCESS) {
        goto EXIT_VENC_H265_STOP;
    }

    /* set watermark mode for chn 0 */
    if ((ret = sample_venc_set_watermark(venc_vpss_chn->venc_chn[0], TD_TRUE)) != TD_SUCCESS) {
        goto EXIT_VENC_H265_UnBind;
    }

    /* encode h.264 */
    if ((ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[1], &(chn_param[1]))) != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        goto EXIT_VENC_H265_UnBind;
    }

    ret = sample_venc_vpss_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[1], venc_vpss_chn->venc_chn[1]);
    if (ret != TD_SUCCESS) {
        goto EXIT_VENC_H264_STOP;
    }

    /* set watermark mode for chn 1 */
    if ((ret = sample_venc_set_watermark(venc_vpss_chn->venc_chn[1], TD_TRUE)) != TD_SUCCESS) {
        goto EXIT_VENC_H264_UnBind;
    }

    /* *****************************************
     stream save process
    ***************************************** */
    if ((ret = sample_comm_venc_start_get_stream(venc_vpss_chn->venc_chn, CHN_NUM_MAX)) != TD_SUCCESS) {
        sample_print("Start Venc failed!\n");
        goto EXIT_VENC_H264_UnBind;
    }

    return TD_SUCCESS;

EXIT_VENC_H264_UnBind:
    sample_comm_vpss_un_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[1], venc_vpss_chn->venc_chn[1]);
EXIT_VENC_H264_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[1]);
EXIT_VENC_H265_UnBind:
    sample_comm_vpss_un_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[0], venc_vpss_chn->venc_chn[0]);
EXIT_VENC_H265_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[0]);

    return ret;
}

static td_s32 sample_venc_watermark(td_void)
{
    td_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg = { 0 };
    const ot_vi_pipe vi_pipe = 0;
    const ot_vi_chn vi_chn = 0;
    const ot_vpss_grp vpss_grp = 0;
    sample_venc_vpss_chn_attr vpss_param = { 0 };
    sample_venc_vb_attr vb_attr = { 0 };
    ot_size enc_size[CHN_NUM_MAX], vi_size;
    sample_venc_vpss_chn venc_vpss_chn = { 0 };

    sample_set_venc_vpss_chn(&venc_vpss_chn, CHN_NUM_MAX);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    sample_comm_vi_get_size_by_sns_type(sns_type, &vi_size);
    /* *****************************************
      step 0: related parameter ready
    ***************************************** */
    if ((ret = sample_venc_init_param(enc_size, CHN_NUM_MAX, &vi_size, &vpss_param)) != TD_SUCCESS) {
        return ret;
    }

    /* *****************************************
      step 1: init sys alloc common vb
    ***************************************** */
    get_vb_attr(&vi_size, &vpss_param, &vb_attr);

    if ((ret = sample_venc_sys_init(&vb_attr)) != TD_SUCCESS) {
        sample_print("Init SYS err for %#x!\n", ret);
        return ret;
    }

    if ((ret = sample_venc_vi_init(&vi_cfg)) != TD_SUCCESS) {
        sample_print("Init VI err for %#x!\n", ret);
        goto EXIT_SYS_STOP;
    }

    if ((ret = sample_venc_vpss_init(vpss_grp, &vpss_param)) != TD_SUCCESS) {
        sample_print("Init VPSS err for %#x!\n", ret);
        goto EXIT_VI_STOP;
    }

    if ((ret = sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0)) != TD_SUCCESS) {
        sample_print("VI Bind VPSS err for %#x!\n", ret);
        goto EXIT_VPSS_STOP;
    }

    /* *****************************************
    start stream venc
    ***************************************** */
    ret = sample_venc_watermark_start_encode(enc_size, CHN_NUM_MAX, vpss_grp, &venc_vpss_chn);
    if (ret != TD_SUCCESS) {
        goto EXIT_VI_VPSS_UNBIND;
    }

    /* *****************************************
     exit process
    ***************************************** */
    sample_venc_exit_process();
    sample_venc_unbind_vpss_stop(vpss_grp, &venc_vpss_chn);

EXIT_VI_VPSS_UNBIND:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0);
EXIT_VPSS_STOP:
    sample_venc_vpss_deinit(vpss_grp, &vpss_param);
EXIT_VI_STOP:
    sample_venc_vi_deinit(&vi_cfg);
EXIT_SYS_STOP:
    sample_comm_sys_exit();

    return ret;
}

static td_s32 sample_venc_set_pvc_attr(ot_venc_chn venc_chn, td_bool enable)
{
    td_s32 ret;
    ot_venc_quality_balance quality_balance;

    if ((ret = ss_mpi_venc_get_quality_balance(venc_chn, &quality_balance)) != TD_SUCCESS) {
        sample_print("Get quality balance failed for %#x!\n", ret);
        return ret;
    }

    if (enable) {
        quality_balance.enable = TD_TRUE;
    } else {
        quality_balance.enable = TD_FALSE;
    }

    if ((ret = ss_mpi_venc_set_quality_balance(venc_chn, &quality_balance)) != TD_SUCCESS) {
        sample_print("Set debreath_effect failed for %#x!\n", ret);
        return ret;
    }

    return TD_SUCCESS;
}

static td_s32 sample_venc_pvc_start_encode(ot_size *enc_size, td_s32 chn_num_max, ot_vpss_grp vpss_grp,
    sample_venc_vpss_chn *venc_vpss_chn)
{
    td_s32 ret;
    ot_venc_gop_attr gop_attr;
    sample_comm_venc_chn_param chn_param[CHN_NUM_MAX] = {0};

    if ((ret = sample_comm_venc_get_gop_default_attr(&gop_attr)) != TD_SUCCESS) {
        return ret;
    }

    if ((ret = sample_venc_set_video_param(enc_size, chn_param, CHN_NUM_MAX, gop_attr, TD_FALSE)) != TD_SUCCESS) {
        return ret;
    }

    /* encode h.265 */
    if ((ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[0], &(chn_param[0]))) != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        return ret;
    }

    ret = sample_venc_vpss_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[0], venc_vpss_chn->venc_chn[0]);
    if (ret != TD_SUCCESS) {
        goto EXIT_VENC_H265_STOP;
    }

    /* set pvc mode for chn 0 */
    if ((ret = sample_venc_set_pvc_attr(venc_vpss_chn->venc_chn[0], TD_TRUE)) != TD_SUCCESS) {
        goto EXIT_VENC_H265_UnBind;
    }

    /* encode h.264 */
    if ((ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[1], &(chn_param[1]))) != TD_SUCCESS) {
        sample_print("Venc Start failed for %#x!\n", ret);
        goto EXIT_VENC_H265_UnBind;
    }

    ret = sample_venc_vpss_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[1], venc_vpss_chn->venc_chn[1]);
    if (ret != TD_SUCCESS) {
        goto EXIT_VENC_H264_STOP;
    }

    /* set pvc mode for chn 1 */
    if ((ret = sample_venc_set_pvc_attr(venc_vpss_chn->venc_chn[1], TD_FALSE)) != TD_SUCCESS) {
        goto EXIT_VENC_H264_UnBind;
    }

    /* *****************************************
     stream save process
    ***************************************** */
    if ((ret = sample_comm_venc_start_get_stream(venc_vpss_chn->venc_chn, CHN_NUM_MAX)) != TD_SUCCESS) {
        sample_print("Start Venc failed!\n");
        goto EXIT_VENC_H264_UnBind;
    }

    return TD_SUCCESS;

EXIT_VENC_H264_UnBind:
    sample_comm_vpss_un_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[1], venc_vpss_chn->venc_chn[1]);
EXIT_VENC_H264_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[1]);
EXIT_VENC_H265_UnBind:
    sample_comm_vpss_un_bind_venc(vpss_grp, venc_vpss_chn->vpss_chn[0], venc_vpss_chn->venc_chn[0]);
EXIT_VENC_H265_STOP:
    sample_comm_venc_stop(venc_vpss_chn->venc_chn[0]);

    return ret;
}

static td_s32 sample_venc_pvc(td_void)
{
    td_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg = { 0 };
    const ot_vi_pipe vi_pipe = 0;
    const ot_vi_chn vi_chn = 0;
    const ot_vpss_grp vpss_grp = 0;
    sample_venc_vpss_chn_attr vpss_param = { 0 };
    sample_venc_vb_attr vb_attr = { 0 };
    ot_size enc_size[CHN_NUM_MAX], vi_size;
    sample_venc_vpss_chn venc_vpss_chn = { 0 };

    sample_set_venc_vpss_chn(&venc_vpss_chn, CHN_NUM_MAX);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    sample_comm_vi_get_size_by_sns_type(sns_type, &vi_size);
    /* *****************************************
      step 0: related parameter ready
    ***************************************** */
    if ((ret = sample_venc_init_param(enc_size, CHN_NUM_MAX, &vi_size, &vpss_param)) != TD_SUCCESS) {
        return ret;
    }

    /* *****************************************
      step 1: init sys alloc common vb
    ***************************************** */
    get_vb_attr(&vi_size, &vpss_param, &vb_attr);

    if ((ret = sample_venc_sys_init(&vb_attr)) != TD_SUCCESS) {
        sample_print("Init SYS err for %#x!\n", ret);
        return ret;
    }

    if ((ret = sample_venc_vi_init(&vi_cfg)) != TD_SUCCESS) {
        sample_print("Init VI err for %#x!\n", ret);
        goto EXIT_SYS_STOP;
    }

    if ((ret = sample_venc_vpss_init(vpss_grp, &vpss_param)) != TD_SUCCESS) {
        sample_print("Init VPSS err for %#x!\n", ret);
        goto EXIT_VI_STOP;
    }

    if ((ret = sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0)) != TD_SUCCESS) {
        sample_print("VI Bind VPSS err for %#x!\n", ret);
        goto EXIT_VPSS_STOP;
    }

    /* *****************************************
    start stream venc
    ***************************************** */
    ret = sample_venc_pvc_start_encode(enc_size, CHN_NUM_MAX, vpss_grp, &venc_vpss_chn);
    if (ret != TD_SUCCESS) {
        goto EXIT_VI_VPSS_UNBIND;
    }

    /* *****************************************
     exit process
    ***************************************** */
    sample_venc_exit_process();
    sample_venc_unbind_vpss_stop(vpss_grp, &venc_vpss_chn);

EXIT_VI_VPSS_UNBIND:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0);
EXIT_VPSS_STOP:
    sample_venc_vpss_deinit(vpss_grp, &vpss_param);
EXIT_VI_STOP:
    sample_venc_vi_deinit(&vi_cfg);
EXIT_SYS_STOP:
    sample_comm_sys_exit();

    return ret;
}

static td_s32 sample_venc_set_fmu_video_param(ot_size *enc_size, sample_comm_venc_chn_param *chn_param,
    ot_venc_gop_attr gop_attr, td_s32 chn_num_max, td_s32 grp_num_max)
{
    td_s32 i, ret;
    td_u32 profile[CHN_NUM_MAX] = {0, 0};
    td_bool share_buf_en = TD_TRUE;
    ot_payload_type payload[TYPE_NUM_MAX] = {OT_PT_H265, OT_PT_H264, OT_PT_MJPEG};
    sample_rc rc_mode = 0;

    if ((ret = sample_comm_venc_get_gop_default_attr(&gop_attr)) != TD_SUCCESS) {
        return ret;
    }

    if ((rc_mode = SAMPLE_RC_CBR, TD_SUCCESS) != TD_SUCCESS) {
        return TD_FAILURE;
    }

    for (i = 0; i < 3; i++) { /* 3: is a number */
        chn_param[i].frame_rate = 30; /* 30: is a number */
        chn_param[i].gop = 60; /* 60: is a number */
        chn_param[i].stats_time = 2; /* 2: is a number */
        chn_param[i].is_rcn_ref_share_buf = share_buf_en;
    }

    /* encode h.265 */
    chn_param[0].gop_attr = gop_attr;
    chn_param[0].type = payload[0];
    chn_param[0].size = (grp_num_max == 1) ? sample_comm_sys_get_pic_enum(&enc_size[1]) :
        sample_comm_sys_get_pic_enum(&enc_size[0]);
    chn_param[0].rc_mode = rc_mode;
    chn_param[0].profile = profile[0];

    if (chn_num_max == 1) { /* 1: chn_param array len */
        return TD_SUCCESS;
    }

    /* encode h.264 */
    chn_param[1].gop_attr = gop_attr;
    chn_param[1].type = payload[1];
    chn_param[1].size = sample_comm_sys_get_pic_enum(&enc_size[1]);
    chn_param[1].rc_mode = rc_mode;
    chn_param[1].profile = profile[1];

    if (grp_num_max == 2 || chn_num_max == 2) { /* 2: chn_param array len */
        return TD_SUCCESS;
    }

    printf("please config mjpeg!\n");
    if ((rc_mode = SAMPLE_RC_CBR, TD_SUCCESS) != TD_SUCCESS) { /* 2: mjpege num */
        return TD_SUCCESS;
    }

    /* encode mjpge */
    chn_param[2].gop_attr = gop_attr; /* 2: mjpege num */
    chn_param[2].type = payload[2]; /* 2: mjpege num */
    chn_param[2].size = SMALL_STREAM_SIZE; /* 2: mjpege num */
    chn_param[2].rc_mode = rc_mode;   /* 2: mjpege num */
    chn_param[2].profile = profile[1]; /* 2: mjpege num */
    return TD_SUCCESS;
}

td_void sample_venc_get_vi_cfg_by_fmu_direct(sample_vi_cfg *vi_cfg)
{
    td_u32 i;

    for (i = 0; i < vi_cfg->bind_pipe.pipe_num; i++) {
        vi_cfg->pipe_info[i].chn_info[0].chn_attr.compress_mode = OT_COMPRESS_MODE_TILE;
        vi_cfg->pipe_info[i].chn_info[0].chn_attr.video_format = OT_VIDEO_FORMAT_TILE_32x4;
    }
}

static td_void get_default_vpss_grp_chn_fmu_attr(ot_size *vi_size, ot_size enc_size[], td_s32 chn_num,
    sample_venc_vpss_chn_attr *grp_chn_attr, td_s32 grp_num_max)
{
    td_s32 i, j;
    td_u32 max_width;
    td_u32 max_height;

    max_width = vi_size->width;
    max_height = vi_size->height;

    for (i = 0; (i < grp_num_max) && (i < GRP_NUM_MAX); i++) {
        if (memset_s(&grp_chn_attr[i], sizeof(sample_venc_vpss_chn_attr), 0,
            sizeof(sample_venc_vpss_chn_attr)) != EOK) {
            printf("vpss chn attr call memset_s error\n");
            return;
        }

        if (grp_num_max != GRP_NUM_MAX) {
            for (j = 0; (j < chn_num) && (j < OT_VPSS_MAX_PHYS_CHN_NUM); j++) {
                grp_chn_attr[i].output_size[j].width = (chn_num > 1) ? enc_size[1].width : enc_size[0].width;
                grp_chn_attr[i].output_size[j].height = (chn_num > 1) ? enc_size[1].height : enc_size[0].height;
                grp_chn_attr[i].compress_mode[j] = OT_COMPRESS_MODE_NONE;
                grp_chn_attr[i].enable[j] = TD_TRUE;
                grp_chn_attr[i].fmu_mode[j] = (j == 0) ? OT_FMU_MODE_DIRECT : OT_FMU_MODE_WRAP;

                max_width = MAX2(max_width, enc_size[j].width);
                max_height = MAX2(max_height, enc_size[j].height);
            }
        } else {
            grp_chn_attr[i].output_size[0].width = enc_size[i].width;
            grp_chn_attr[i].output_size[0].height = enc_size[i].height;
            grp_chn_attr[i].compress_mode[0] = OT_COMPRESS_MODE_NONE;
            grp_chn_attr[i].enable[0] = TD_TRUE;
            grp_chn_attr[i].fmu_mode[0] = OT_FMU_MODE_DIRECT;

            max_width = MAX2(max_width, enc_size[i].width);
            max_height = MAX2(max_height, enc_size[i].height);
        }
        grp_chn_attr[i].in_size.width = vi_size->width;
        grp_chn_attr[i].in_size.height = vi_size->height;
        grp_chn_attr[i].max_size.width = max_width;
        grp_chn_attr[i].max_size.height = max_height;
        grp_chn_attr[i].pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    }

    return;
}

static td_s32 sample_venc_grp_fmu_init_param(ot_size *enc_size, td_s32 chn_num_max, ot_size *vi_size,
    sample_venc_vpss_chn_attr *grp_chn_attr, td_s32 grp_num_max)
{
    td_s32 i;
    td_s32 ret;
    ot_pic_size pic_size[CHN_NUM_MAX] = {BIG_STREAM_SIZE, SMALL_STREAM_SIZE};

    enc_size[0].width = vi_size->width;
    enc_size[0].height = vi_size->height;
    for (i = 1; i < chn_num_max && i < CHN_NUM_MAX; i++) {
        ret = sample_comm_sys_get_pic_size(pic_size[i], &enc_size[i]);
        if (ret != TD_SUCCESS) {
            sample_print("sample_comm_sys_get_pic_size failed!\n");
            return ret;
        }
    }

    // get vpss param
    get_default_vpss_grp_chn_fmu_attr(vi_size, enc_size, chn_num_max, grp_chn_attr, grp_num_max);

    return 0;
}

static td_void sample_venc_set_vpss_one_grp_chn(ot_vpss_grp grp, sample_venc_vpss_chn *venc_vpss_chn,
    const sample_venc_vpss_chn_attr *grp_attr, td_s32 grp_num_max)
{
    td_s32 i;
    for (i = 0; i < OT_VPSS_MAX_PHYS_CHN_NUM; i++) {
        if (grp_attr[grp].enable[i] == TD_TRUE && grp_attr[grp].fmu_mode[i] == OT_FMU_MODE_WRAP) {
            venc_vpss_chn->vpss_chn[i] = 0;
        } else if (grp_attr[grp].enable[i] == TD_TRUE && grp_attr[grp].fmu_mode[i] == OT_FMU_MODE_DIRECT) {
            venc_vpss_chn->vpss_chn[i] = OT_VPSS_DIRECT_CHN;
        } else if (grp_attr[grp].enable[i] == TD_TRUE) {
            venc_vpss_chn->vpss_chn[i] = i;
        }
    }
}

static td_void sample_set_venc_vpss_fmu_chn(sample_venc_vpss_chn *venc_vpss_chn, td_s32 chn_num_max,
    const sample_venc_vpss_chn_attr *grp_attr, td_s32 grp_num_max)
{
    td_s32 i, j;

    for (i = 0; i < grp_num_max && i < GRP_NUM_MAX; i++) {
        if (grp_num_max > 1) {
            if (grp_attr[i].enable[0] == TD_TRUE && grp_attr[i].fmu_mode[0] == OT_FMU_MODE_WRAP) {
                venc_vpss_chn->vpss_chn[i] = 0;
            } else if (grp_attr[i].enable[0] == TD_TRUE && grp_attr[i].fmu_mode[0] == OT_FMU_MODE_DIRECT) {
                venc_vpss_chn->vpss_chn[i] = OT_VPSS_DIRECT_CHN;
            } else if (grp_attr[i].enable[0] == TD_TRUE) {
                venc_vpss_chn->vpss_chn[i] = i;
            }
            venc_vpss_chn->venc_chn[i] = i;
        } else {
            sample_venc_set_vpss_one_grp_chn(i, venc_vpss_chn, grp_attr, grp_num_max);
            for (j = 0; j < CHN_NUM_MAX; j++) {
                venc_vpss_chn->venc_chn[j] = j;
            }
        }
    }
}

static td_void get_fmu_vb_attr(const ot_size *vi_size, sample_venc_vb_attr *vb_attr)
{
    // vb for vi-vpss
    update_vb_attr(vb_attr, vi_size, OT_PIXEL_FORMAT_YUV_SEMIPLANAR_422, OT_COMPRESS_MODE_NONE, VI_VB_YUV_CNT);

    if (vb_attr->valid_num >= OT_VB_MAX_COMMON_POOLS) {
        return;
    }

    vb_attr->supplement_config = OT_VB_SUPPLEMENT_JPEG_MASK | OT_VB_SUPPLEMENT_BNR_MOT_MASK;
}

static td_s32 sample_venc_fmu_wrap_init(const sample_venc_vpss_chn_attr *grp_attr, td_s32 grp_num_max,
    ot_size *in_size)
{
    td_u32 cnt = 0;
    ot_fmu_attr fmu_attr;
    ot_vpss_chn i, j;
    td_u32 width, height;

    width = in_size->width;
    height = in_size->height;

    for (i = 0; i < grp_num_max && i < GRP_NUM_MAX; i++) {
        for (j = 0; j < OT_VPSS_MAX_PHYS_CHN_NUM; j++) {
            if (grp_attr[i].enable[j] == TD_TRUE && grp_attr[i].fmu_mode[j] == OT_FMU_MODE_WRAP) {
                width = (width > grp_attr[i].output_size[j].width) ? width : grp_attr[i].output_size[j].width;
                height = (height > grp_attr[i].output_size[j].height) ? height : grp_attr[i].output_size[j].height;
                cnt++;
            }
        }
    }

    if (cnt > 0) {
        fmu_attr.wrap_en = TD_TRUE;
        fmu_attr.page_num = MIN2(ot_common_get_fmu_wrap_page_num(OT_FMU_ID_VPSS,
            width, height) + (cnt - 1) * 3, /* 3: for multi pipe */
            OT_FMU_MAX_Y_PAGE_NUM + OT_FMU_MAX_C_PAGE_NUM);
    } else {
        fmu_attr.wrap_en = TD_FALSE;
    }

    if (ss_mpi_sys_set_fmu_attr(OT_FMU_ID_VPSS, &fmu_attr) != TD_SUCCESS) {
        return TD_FAILURE;
    }

    return TD_SUCCESS;
}

static td_void sample_venc_vpss_cfg_chn(const sample_venc_vpss_chn_attr *grp_attr, ot_vpss_chn_attr *chn_attr,
    td_s32 vpss_max_chn, td_s32 i)
{
    td_s32 j;
    for (j = 0; j < OT_VPSS_MAX_PHYS_CHN_NUM && j < vpss_max_chn; j++) {
        if (grp_attr[i].enable[j] == TD_TRUE) {
            chn_attr[j].width = grp_attr[i].output_size[j].width;
            chn_attr[j].height = grp_attr[i].output_size[j].height;
            chn_attr[j].chn_mode = OT_VPSS_CHN_MODE_USER;
            chn_attr[j].compress_mode = grp_attr[i].compress_mode[j];
            chn_attr[j].pixel_format = grp_attr[i].pixel_format;
            chn_attr[j].depth = 0;
            chn_attr[j].mirror_en = 0;
            chn_attr[j].flip_en = 0;
            chn_attr[j].aspect_ratio.mode = OT_ASPECT_RATIO_NONE;
            chn_attr[j].frame_rate.src_frame_rate = -1;
            chn_attr[j].frame_rate.dst_frame_rate = -1;
#ifdef OT_FPGA
            chn_attr[j].frame_rate.src_frame_rate = 30; /* 30: fpga src frame rate */
            chn_attr[j].frame_rate.dst_frame_rate = 10; /* 10: fpga dst frame rate */
#endif
        }
    }
}

static td_s32 sample_venc_vpss_cfg_fmu_direct_chn(ot_vpss_grp grp, ot_vpss_chn chn,
    const sample_venc_vpss_chn_attr *grp_attr, td_s32 grp_num_max)
{
    if (ss_mpi_vpss_set_chn_fmu_mode(grp, OT_VPSS_DIRECT_CHN,
        grp_attr[grp].fmu_mode[chn]) != TD_SUCCESS) {
        return TD_FAILURE;
    }

    if (ss_mpi_vpss_enable_chn(grp, OT_VPSS_DIRECT_CHN) != TD_SUCCESS) {
        return TD_FAILURE;
    }

    if (grp_num_max == 1) {
        if (ss_mpi_vpss_disable_chn(grp, 1) != TD_SUCCESS) {
            return TD_FAILURE;
        }
    } else {
        if (ss_mpi_vpss_disable_chn(grp, chn) != TD_SUCCESS) {
            return TD_FAILURE;
        }
    }
    return TD_SUCCESS;
}

static td_void sample_venc_get_default_ldc_attr(ot_ldc_attr *ldc_attr)
{
    ldc_attr->enable = TD_TRUE;
    ldc_attr->ldc_version = OT_LDC_V1;
    ldc_attr->ldc_v1_attr.aspect = 0;
    ldc_attr->ldc_v1_attr.x_ratio = 100; // 100: param
    ldc_attr->ldc_v1_attr.y_ratio = 100; // 100: param
    ldc_attr->ldc_v1_attr.xy_ratio = 100; // 100: param
    ldc_attr->ldc_v1_attr.center_x_offset = 0;
    ldc_attr->ldc_v1_attr.center_y_offset = 0;
    ldc_attr->ldc_v1_attr.distortion_ratio = 500; // 500: param
}

static td_s32 sample_venc_vpss_grp_fmu_mode_init(const sample_venc_vpss_chn_attr *grp_attr, td_s32 grp,
    td_s32 grp_num_max)
{
    td_s32 j;
    ot_gdc_param gdc_param = {0};
    ot_ldc_attr ldc_attr = {0};

    gdc_param.in_size.width  = grp_attr[grp].in_size.width;
    gdc_param.in_size.height = grp_attr[grp].in_size.height;
    gdc_param.cell_size = OT_LUT_CELL_SIZE_16;
    if (ss_mpi_vpss_set_grp_gdc_param(grp, &gdc_param) != TD_SUCCESS) {
        return TD_FAILURE;
    }

    sample_venc_get_default_ldc_attr(&ldc_attr);
    if (ss_mpi_vpss_set_grp_ldc(grp, &ldc_attr) != TD_SUCCESS) {
        return TD_FAILURE;
    }

    if (ss_mpi_vpss_set_grp_zme(grp, &grp_attr[grp].output_size[0]) != TD_SUCCESS) {
        return TD_FAILURE;
    }

    for (j = 0; j < OT_VPSS_MAX_PHYS_CHN_NUM; j++) {
        if (grp_attr[grp].enable[j] != TD_TRUE) {
            continue;
        }
        if (grp_attr[grp].fmu_mode[j] == OT_FMU_MODE_DIRECT) {
            if (sample_venc_vpss_cfg_fmu_direct_chn(grp, j, grp_attr, grp_num_max) != TD_SUCCESS) {
                return TD_FAILURE;
            }
        }
    }

    return TD_SUCCESS;
}

static td_s32 sample_venc_vpss_grp_fmu_init(const sample_venc_vpss_chn_attr *grp_attr, td_s32 grp_num_max)
{
    td_s32 i, j, ret;
    ot_vpss_grp_attr vpss_grp_attr = { 0 };
    sample_vpss_chn_attr vpss_chn_attr = {0};

    for (i = 0; i < grp_num_max && i < GRP_NUM_MAX; i++) {
        vpss_grp_attr.max_width = grp_attr[i].max_size.width;
        vpss_grp_attr.max_height = grp_attr[i].max_size.height;
        vpss_grp_attr.dei_mode = OT_VPSS_DEI_MODE_OFF;
        vpss_grp_attr.pixel_format = grp_attr[i].pixel_format;
        vpss_grp_attr.frame_rate.src_frame_rate = -1;
        vpss_grp_attr.frame_rate.dst_frame_rate = -1;

        sample_venc_vpss_cfg_chn(grp_attr, &vpss_chn_attr.chn_attr[0], OT_VPSS_MAX_PHYS_CHN_NUM, i);
        for (j = 0; j < OT_VPSS_MAX_PHYS_CHN_NUM; j++) {
            if (grp_attr[i].enable[j] != TD_TRUE) {
                continue;
            }

            if (grp_attr[i].fmu_mode[j] == OT_FMU_MODE_WRAP) {
                vpss_chn_attr.chn0_wrap = TD_TRUE;
            }
        }
        memcpy_s(vpss_chn_attr.chn_enable, sizeof(grp_attr[i].enable), grp_attr[i].enable, sizeof(grp_attr[i].enable));
        vpss_chn_attr.chn_array_size = OT_VPSS_MAX_PHYS_CHN_NUM;
        ret = sample_common_vpss_start(i, &vpss_grp_attr, &vpss_chn_attr);
        if (ret != TD_SUCCESS) {
            sample_print("failed with %#x!\n", ret);
            goto stop_vpss;
        }

        ret = sample_venc_vpss_grp_fmu_mode_init(grp_attr, i, grp_num_max);
        if (ret != TD_SUCCESS) {
            sample_print("failed with %#x!\n", ret);
            goto stop_vpss;
        }
    }

    return TD_SUCCESS;

stop_vpss:
    for (i = 0; i < grp_num_max && i < GRP_NUM_MAX; i++) {
        sample_common_vpss_stop(i, grp_attr[i].enable, OT_VPSS_MAX_PHYS_CHN_NUM);
    }
    return TD_FAILURE;
}

static td_s32 sample_venc_vi_bind_vpss_init(ot_vi_pipe vi_pipe, ot_vi_chn vi_chn)
{
    td_s32 i, ret;

    for (i = 0; i < GRP_NUM_MAX; i++) {
        if ((ret = sample_comm_vi_bind_vpss(vi_pipe, vi_chn, i, 0)) != TD_SUCCESS) {
            sample_print("VI Bind VPSS err for %#x!\n", ret);
            return TD_FAILURE;
        }
    }

    return TD_SUCCESS;
}

static td_s32 sample_venc_fmu_grp_start_encode(ot_size *enc_size, td_s32 chn_num_max,
    td_s32 grp_num_max, sample_venc_vpss_chn *venc_vpss_chn)
{
    td_s32 i, ret;
    ot_venc_gop_attr gop_attr = { 0 };
    sample_comm_venc_chn_param chn_param[TYPE_NUM_MAX] = {0};
    sample_comm_venc_chn_param *venc_chn_param = TD_NULL;

    if ((ret = sample_venc_set_fmu_video_param(enc_size, chn_param, gop_attr, TYPE_NUM_MAX,
        grp_num_max)) != TD_SUCCESS) {
        return ret;
    }

    for (i = 0; i < grp_num_max && i < GRP_NUM_MAX; i++) {
        venc_chn_param = &(chn_param[i]);
        if ((ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[i], venc_chn_param)) != TD_SUCCESS) {
            sample_print("Venc Start failed for %#x!\n", ret);
            goto UnBind;
        }

        ret = sample_venc_vpss_bind_venc(i, venc_vpss_chn->vpss_chn[i], venc_vpss_chn->venc_chn[i]);
        if (ret != TD_SUCCESS) {
            goto UnBind;
        }

        if (grp_num_max == 1) {
            venc_chn_param = &(chn_param[2]); /* 2: mjpege num */
            if ((ret = sample_comm_venc_start(venc_vpss_chn->venc_chn[1], venc_chn_param)) != TD_SUCCESS) {
                sample_print("Venc Start failed for %#x!\n", ret);
                goto UnBind;
            }

            ret = sample_venc_vpss_bind_venc(i, venc_vpss_chn->vpss_chn[1], venc_vpss_chn->venc_chn[1]);
            if (ret != TD_SUCCESS) {
                goto UnBind;
            }
        }
    }

    /* *****************************************
     stream save process
    ***************************************** */
    if ((ret = sample_comm_venc_start_get_stream(venc_vpss_chn->venc_chn, CHN_NUM_MAX)) != TD_SUCCESS) {
        sample_print("Start Venc failed!\n");
        goto UnBind;
    }

    return TD_SUCCESS;

UnBind:
    for (i = 0; i < grp_num_max && i < GRP_NUM_MAX; i++) {
        sample_comm_vpss_un_bind_venc(i, venc_vpss_chn->vpss_chn[i], venc_vpss_chn->venc_chn[i]);
        sample_comm_venc_stop(venc_vpss_chn->venc_chn[i]);
        if (grp_num_max == 1) {
            sample_comm_vpss_un_bind_venc(i, venc_vpss_chn->vpss_chn[1], venc_vpss_chn->venc_chn[1]);
            sample_comm_venc_stop(venc_vpss_chn->venc_chn[1]);
        }
    }

    return ret;
}

static td_void sample_venc_unbind_vpss_fmu_stop(td_s32 grp_num_max, const sample_venc_vpss_chn *venc_vpss_chn)
{
    td_s32 i;

    if (grp_num_max > 1) {
        for (i = 0; i < grp_num_max && i < GRP_NUM_MAX; i++) {
            sample_comm_vpss_un_bind_venc(i, venc_vpss_chn->vpss_chn[i], venc_vpss_chn->venc_chn[i]);
            sample_comm_venc_stop(venc_vpss_chn->venc_chn[i]);
        }
    } else {
        for (i = 0; i < CHN_NUM_MAX; i++) {
            sample_comm_vpss_un_bind_venc(0, venc_vpss_chn->vpss_chn[i], venc_vpss_chn->venc_chn[i]);
            sample_comm_venc_stop(venc_vpss_chn->venc_chn[i]);
        }
    }
}

static td_s32 sample_venc_fmu_one_grp(td_void)
{
    td_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg = { 0 };
    const ot_vi_pipe vi_pipe = 0;
    const ot_vi_chn vi_chn = 0;
    const ot_vpss_grp vpss_grp = 0;
    sample_venc_vb_attr vb_attr = { 0 };
    ot_size enc_size[CHN_NUM_MAX], vi_size;
    sample_venc_vpss_chn venc_vpss_chn = { 0 };
    sample_venc_vpss_chn_attr grp_chn_attr[GRP_NUM_MAX] = { 0 };

    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);

    sample_venc_get_vi_cfg_by_fmu_direct(&vi_cfg);
    sample_comm_vi_get_size_by_sns_type(sns_type, &vi_size);

    /* *****************************************
      step 0: related parameter ready
    ***************************************** */
    ret = sample_venc_grp_fmu_init_param(enc_size, CHN_NUM_MAX, &vi_size, grp_chn_attr, 1);
    if (ret != TD_SUCCESS) {
        return ret;
    }
    sample_set_venc_vpss_fmu_chn(&venc_vpss_chn, CHN_NUM_MAX, grp_chn_attr, 1);

    /******************************************
      step 1: init sys alloc common vb
    ******************************************/
    get_fmu_vb_attr(&vi_size, &vb_attr);
    if ((ret = sample_venc_sys_init(&vb_attr)) != TD_SUCCESS) {
        sample_print("Init SYS err for %#x!\n", ret);
        return ret;
    }

    if ((ret = sample_venc_fmu_wrap_init(grp_chn_attr, 1, &vi_size)) != TD_SUCCESS) {
        sample_print("Init SYS err for %#x!\n", ret);
        return ret;
    }

    if ((ret = sample_venc_vi_init(&vi_cfg)) != TD_SUCCESS) {
        sample_print("Init VI err for %#x!\n", ret);
        goto EXIT_SYS_STOP;
    }

    if ((ret = sample_venc_vpss_grp_fmu_init(grp_chn_attr, 1)) != TD_SUCCESS) {
        sample_print("Init VPSS err for %#x!\n", ret);
        goto EXIT_VI_STOP;
    }

    if ((ret = sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0)) != TD_SUCCESS) {
        sample_print("VI Bind VPSS err for %#x!\n", ret);
        goto EXIT_VPSS_STOP;
    }

    /* *****************************************
    start stream venc
    ***************************************** */
    ret = sample_venc_fmu_grp_start_encode(enc_size, CHN_NUM_MAX, 1, &venc_vpss_chn);
    if (ret != TD_SUCCESS) {
        goto EXIT_VI_VPSS_UNBIND;
    }

    /* *****************************************
     exit process
    ***************************************** */
    sample_venc_exit_process();
    sample_venc_unbind_vpss_fmu_stop(1, &venc_vpss_chn);

EXIT_VI_VPSS_UNBIND:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0);
EXIT_VPSS_STOP:
    sample_venc_vpss_deinit(vpss_grp, &grp_chn_attr[vpss_grp]);
EXIT_VI_STOP:
    sample_venc_vi_deinit(&vi_cfg);
EXIT_SYS_STOP:
    sample_comm_sys_exit();

    return ret;
}

static td_s32 sample_venc_fmu_two_grp(td_void)
{
    td_s32 i, ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg = { 0 };
    const ot_vi_pipe vi_pipe = 0;
    const ot_vi_chn vi_chn = 0;
    sample_venc_vb_attr vb_attr = { 0 };
    ot_size enc_size[CHN_NUM_MAX], vi_size;
    sample_venc_vpss_chn venc_vpss_chn = { 0 };
    sample_venc_vpss_chn_attr grp_chn_attr[GRP_NUM_MAX] = { 0 };

    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);

    sample_venc_get_vi_cfg_by_fmu_direct(&vi_cfg);
    sample_comm_vi_get_size_by_sns_type(sns_type, &vi_size);

    /******************************************
      step 0: related parameter ready
    ******************************************/
    ret = sample_venc_grp_fmu_init_param(enc_size, CHN_NUM_MAX, &vi_size, grp_chn_attr, GRP_NUM_MAX);
    if (ret != TD_SUCCESS) {
        return ret;
    }
    sample_set_venc_vpss_fmu_chn(&venc_vpss_chn, CHN_NUM_MAX, grp_chn_attr, GRP_NUM_MAX);

    /* *****************************************
      step 1: init sys alloc common vb
    ***************************************** */
    get_fmu_vb_attr(&vi_size, &vb_attr);
    if ((ret = sample_venc_sys_init(&vb_attr)) != TD_SUCCESS) {
        sample_print("Init SYS err for %#x!\n", ret);
        return ret;
    }

    if ((ret = sample_venc_fmu_wrap_init(grp_chn_attr, GRP_NUM_MAX, &vi_size)) != TD_SUCCESS) {
        sample_print("Init SYS err for %#x!\n", ret);
        return ret;
    }

    if ((ret = sample_venc_vi_init(&vi_cfg)) != TD_SUCCESS) {
        sample_print("Init VI err for %#x!\n", ret);
        goto EXIT_SYS_STOP;
    }

    if ((ret = sample_venc_vpss_grp_fmu_init(grp_chn_attr, GRP_NUM_MAX)) != TD_SUCCESS) {
        sample_print("Init VPSS err for %#x!\n", ret);
        goto EXIT_VI_STOP;
    }

    if ((ret = sample_venc_vi_bind_vpss_init(vi_pipe, vi_chn)) != TD_SUCCESS) {
        sample_print("Init vi bind vpss err for %#x!\n", ret);
        goto EXIT_VPSS_STOP;
    }

    /* *****************************************
    start stream venc
    ***************************************** */
    ret = sample_venc_fmu_grp_start_encode(enc_size, CHN_NUM_MAX, GRP_NUM_MAX, &venc_vpss_chn);
    if (ret != TD_SUCCESS) {
        goto EXIT_VI_VPSS_UNBIND;
    }

    /* *****************************************
     exit process
    ***************************************** */
    sample_venc_exit_process();
    sample_venc_unbind_vpss_fmu_stop(GRP_NUM_MAX, &venc_vpss_chn);

EXIT_VI_VPSS_UNBIND:
    for (i = 0; i < GRP_NUM_MAX; i++) {
        sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, i, 0);
    }
EXIT_VPSS_STOP:
    for (i = 0; i < GRP_NUM_MAX; i++) {
        sample_venc_vpss_deinit(i, &grp_chn_attr[i]);
    }
EXIT_VI_STOP:
    sample_venc_vi_deinit(&vi_cfg);
EXIT_SYS_STOP:
    sample_comm_sys_exit();

    return ret;
}


/* ================= IMX678 web: live MJPEG stream + ISP controls + snapshot ================= */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>
#include <stdint.h>
#include "ot_mpi_isp.h"

/* newest JPEG frame, shared: one reader thread fills, many HTTP clients read */
static unsigned char g_web_jpeg[1024 * 1024];
static volatile int g_web_jlen = 0;
static pthread_mutex_t g_web_lock = PTHREAD_MUTEX_INITIALIZER;
static ot_venc_chn g_web_chn = 0;
static int g_web_save_cnt = 0;
/* 4K JPEG snapshot channel (chn3): continuous low-fps MJPEG, newest frame kept */
static unsigned char g_snap4k[4 * 1024 * 1024];
static volatile int g_snap4k_len = 0;
static pthread_mutex_t g_snap4k_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_snap4k_ok = 0;   /* 1 = chn3 came up ok */
#define SNAP4K_CHN 3

static const char g_web_html[] =
"<!doctype html><html lang=\"zh\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>甜源创新 · IMX678 相机</title>"
"<style>"
":root{--bg:#0e1116;--card:#171c24;--line:#232a34;--fg:#e6ebf2;--mut:#8a94a3;--acc:#3a7bd5;--ok:#2fb56b;--warn:#e6a23c;--danger:#e2504a}"
"*{box-sizing:border-box}"
"body{margin:0;background:var(--bg);color:var(--fg);font-family:-apple-system,\"Segoe UI\",Roboto,\"PingFang SC\",\"Microsoft YaHei\",sans-serif;font-size:15px;line-height:1.5}"
"header{display:flex;align-items:center;gap:12px;padding:12px 16px;background:linear-gradient(180deg,#141a22,#0e1116);border-bottom:1px solid var(--line);position:sticky;top:0;z-index:10}"
".logo{width:34px;height:34px;border-radius:8px;background:linear-gradient(135deg,#36d1b0,#3a7bd5);display:flex;align-items:center;justify-content:center;font-weight:700;color:#fff;font-size:18px;flex:0 0 auto}"
".brand{display:flex;flex-direction:column;line-height:1.15}"
".brand b{font-size:16px;letter-spacing:.5px}"
".brand small{color:var(--mut);font-size:11px}"
".hstat{margin-left:auto;font-size:12px;color:var(--mut);text-align:right;cursor:pointer}"
".hstat b{color:var(--ok)}"
"nav{display:flex;gap:4px;padding:8px 12px;background:var(--bg);border-bottom:1px solid var(--line);position:sticky;top:59px;z-index:9;overflow-x:auto}"
"nav button{flex:0 0 auto;background:none;border:0;color:var(--mut);padding:8px 14px;border-radius:8px;font-size:14px;cursor:pointer}"
"nav button.on{background:var(--card);color:var(--fg)}"
"main{max-width:880px;margin:0 auto;padding:14px}"
".tab{display:none}.tab.on{display:block}"
".card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:14px}"
".card h4{margin:0 0 10px;font-size:13px;color:var(--mut);font-weight:600;text-transform:uppercase;letter-spacing:.5px}"
"#pvwrap{text-align:left}"
"#v{max-width:100%;width:100%;height:auto;border-radius:10px;transform:rotate(180deg);background:#000;border:1px solid var(--line)}"
".pvlayout{display:flex;gap:14px;align-items:flex-start}"
".pvside{flex:0 0 150px;display:flex;flex-direction:column;gap:8px}"
".pvside .btn{width:100%;text-align:center}"
".pvmain{flex:1;min-width:0}"
".pvmini{display:flex;flex-direction:column;gap:6px;margin-top:4px}"
".pm{display:flex;justify-content:space-between;align-items:baseline;background:#0d1117;border:1px solid var(--line);border-radius:8px;padding:6px 9px}"
".pm span{font-size:11px;color:var(--mut)}"
".pm b{font-size:13px;color:#7cc4ff;font-family:ui-monospace,Menlo,Consolas,monospace}"
".regt{width:100%;border-collapse:collapse;font-size:13px}"
".regt th,.regt td{padding:7px 8px;border-bottom:1px solid var(--line);text-align:left;vertical-align:top}"
".regt th{color:var(--mut);font-size:11px;text-transform:uppercase;letter-spacing:.5px}"
".regt td.a{font-family:ui-monospace,Menlo,Consolas,monospace;color:#9fd6ff;white-space:nowrap}"
".regt td.v{font-family:ui-monospace,Menlo,Consolas,monospace;color:#7cf0b0;white-space:nowrap;font-variant-numeric:tabular-nums}"
".regt td.d{color:var(--mut);font-size:12px}"
".regrow2{display:flex;gap:8px;margin-top:10px}.regrow2 input{flex:1}.regrow2 button{flex:0 0 auto}"
"@media(max-width:560px){.pvlayout{flex-direction:column}.pvside{flex:0 0 auto;flex-direction:row;flex-wrap:wrap}.pvside .btn{width:auto;flex:1 1 40%}.pvmini{flex-direction:row;width:100%}.pm{flex:1}#pvwrap{text-align:center}}"
".row{display:flex;align-items:center;margin:12px 0;gap:10px}"
".row label{width:64px;font-size:14px;color:var(--mut)}"
".row input[type=range]{flex:1;accent-color:var(--acc)}"
".row .val{width:32px;text-align:right;font-size:13px;color:#7cc4ff}"
"input[type=text],input[type=password],input[type=datetime-local]{background:#0d1117;border:1px solid var(--line);color:var(--fg);border-radius:8px;padding:9px 11px;font-size:14px;width:100%}"
"input:focus{outline:none;border-color:var(--acc)}"
".fld{margin:10px 0}.fld label{display:block;font-size:12px;color:var(--mut);margin-bottom:5px}"
"button.btn{background:var(--acc);color:#fff;border:0;padding:10px 16px;border-radius:8px;font-size:14px;cursor:pointer}"
"button.btn.g{background:var(--ok)}button.btn.gray{background:#2a323d}"
".btnrow{display:flex;gap:8px;flex-wrap:wrap;margin-top:6px}"
".msg{font-size:13px;color:var(--ok);min-height:18px;margin-top:8px}"
".sw{display:flex;align-items:center;gap:8px;margin:6px 0 12px}"
".sw input{width:18px;height:18px;accent-color:var(--ok)}"
".url{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:13px;background:#0d1117;border:1px solid var(--line);border-radius:8px;padding:8px 10px;color:#9fd6ff;word-break:break-all;margin:6px 0}"
".grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
".stat{background:#0d1117;border:1px solid var(--line);border-radius:10px;padding:10px 12px}"
".stat .k{font-size:11px;color:var(--mut);text-transform:uppercase;letter-spacing:.4px}"
".stat .n{font-size:19px;font-weight:600;margin-top:3px}"
".stat .n small{font-size:12px;color:var(--mut);font-weight:400}"
".bar{height:6px;background:#232a34;border-radius:4px;overflow:hidden;margin-top:7px}"
".bar>i{display:block;height:100%;background:var(--acc);border-radius:4px;transition:width .4s}"
"table.kv{width:100%;border-collapse:collapse;font-size:13px}"
"table.kv td{padding:6px 4px;border-bottom:1px solid var(--line)}"
"table.kv td:first-child{color:var(--mut);width:42%}"
"table.kv td:last-child{font-family:ui-monospace,Menlo,Consolas,monospace}"
".full{grid-column:1/3}"
"/* login */"
"#login{position:fixed;inset:0;z-index:100;display:flex;background:#0b0e13}"
"#login .lpanel{flex:0 0 min(46%,460px);display:flex;align-items:center;justify-content:flex-start;"
" padding:40px min(6%,56px);background:linear-gradient(180deg,#12161d,#0b0e13)}"
"#login .box{width:100%;max-width:320px}"
"#login .lg{width:52px;height:52px;border-radius:14px;margin:0 0 18px;background:linear-gradient(135deg,#36d1b0,#3a7bd5);"
" display:flex;align-items:center;justify-content:center;font-weight:700;color:#fff;font-size:24px;box-shadow:0 8px 24px rgba(58,123,213,.4)}"
"#login h2{text-align:left;margin:0 0 3px;font-size:22px;letter-spacing:1px}"
"#login p{text-align:left;margin:0 0 26px;color:var(--mut);font-size:12px}"
"#login .fld{margin:12px 0}"
"#login .fld label{font-size:12px;color:var(--mut);margin-bottom:6px;display:block}"
"#login input{padding:12px 13px}"
"#login button{width:100%;margin-top:20px;padding:12px;font-size:15px;font-weight:600;"
" background:linear-gradient(135deg,#36d1b0,#3a7bd5);color:#fff;border:0;border-radius:10px;cursor:pointer}"
"#login button:hover{filter:brightness(1.08)}"
"#login .err{color:var(--danger);font-size:13px;text-align:left;min-height:18px;margin-top:12px}"
"#login .foot{margin-top:28px;font-size:11px;color:#4b5563}"
"/* right hero */"
"#login .rpanel{flex:1;position:relative;overflow:hidden;display:flex;flex-direction:column;"
" align-items:center;justify-content:center;gap:26px;"
" background:radial-gradient(900px 500px at 70% 20%,#1d3a4d 0%,#0e1a24 55%,#0b0e13 100%)}"
"#login .rpanel .glow{position:absolute;width:520px;height:520px;border-radius:50%;"
" background:radial-gradient(circle,rgba(54,209,176,.18),transparent 70%);top:-120px;right:-120px}"
"#login .hero-svg{width:min(56%,320px);height:auto;filter:drop-shadow(0 20px 40px rgba(0,0,0,.5));z-index:1}"
"#login .htitle{z-index:1;text-align:center}"
"#login .htitle b{font-size:22px;letter-spacing:2px;display:block}"
"#login .htitle span{color:var(--mut);font-size:13px}"
"#login .chips{display:flex;gap:10px;flex-wrap:wrap;justify-content:center;z-index:1;max-width:80%}"
"#login .chip{background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.1);border-radius:999px;"
" padding:6px 14px;font-size:12px;color:#cfe6f5}"
"@media(max-width:820px){#login .rpanel{display:none}#login .lpanel{flex:1;justify-content:center;background:radial-gradient(1000px 500px at 50% -10%,#1c2b3a,#0b0e13 60%)}#login h2,#login p,#login .err{text-align:center}#login .lg{margin-left:auto;margin-right:auto}}"
"#app{display:none}"
"</style></head><body>"
"<!-- 登录 -->"
"<div id=\"login\">"
"  <div class=\"lpanel\">"
"    <div class=\"box\">"
"      <div class=\"lg\">甜</div>"
"      <h2>甜源创新</h2><p>IMX678 · 4K 智能相机管理后台</p>"
"      <div class=\"fld\"><label>用户名</label><input type=\"text\" id=\"lu\" placeholder=\"用户名\" value=\"admin\" autocomplete=\"username\"></div>"
"      <div class=\"fld\"><label>密码</label><input type=\"password\" id=\"lp\" placeholder=\"密码\" autocomplete=\"current-password\" onkeydown=\"if(event.key=='Enter')doLogin()\"></div>"
"      <button onclick=\"doLogin()\">登 录</button>"
"      <div class=\"err\" id=\"lerr\"></div>"
"      <div class=\"foot\">© 甜源创新 · Hi3519DV500 + Sony IMX678</div>"
"    </div>"
"  </div>"
"  <div class=\"rpanel\">"
"    <div class=\"glow\"></div>"
"    <svg class=\"hero-svg\" viewBox=\"0 0 240 180\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
"      <defs>"
"        <linearGradient id=\"bg\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\"><stop offset=\"0\" stop-color=\"#2a3947\"/><stop offset=\"1\" stop-color=\"#151d26\"/></linearGradient>"
"        <radialGradient id=\"ls\" cx=\"0.5\" cy=\"0.5\" r=\"0.5\"><stop offset=\"0\" stop-color=\"#0b0f14\"/><stop offset=\"0.6\" stop-color=\"#0b0f14\"/><stop offset=\"0.75\" stop-color=\"#2b6cb0\"/><stop offset=\"1\" stop-color=\"#0b0f14\"/></radialGradient>"
"      </defs>"
"      <rect x=\"24\" y=\"46\" width=\"192\" height=\"118\" rx=\"16\" fill=\"url(#bg)\" stroke=\"#3a4a5a\" stroke-width=\"1.5\"/>"
"      <rect x=\"70\" y=\"30\" width=\"60\" height=\"22\" rx=\"6\" fill=\"#1c2732\" stroke=\"#3a4a5a\" stroke-width=\"1.5\"/>"
"      <circle cx=\"120\" cy=\"106\" r=\"46\" fill=\"#0e141b\" stroke=\"#3a4a5a\" stroke-width=\"2\"/>"
"      <circle cx=\"120\" cy=\"106\" r=\"38\" fill=\"url(#ls)\"/>"
"      <circle cx=\"120\" cy=\"106\" r=\"24\" fill=\"#0a0e13\" stroke=\"#2b6cb0\" stroke-width=\"2\"/>"
"      <circle cx=\"110\" cy=\"96\" r=\"8\" fill=\"#3a7bd5\" opacity=\"0.55\"/>"
"      <circle cx=\"188\" cy=\"62\" r=\"5\" fill=\"#36d1b0\"/>"
"      <rect x=\"150\" y=\"150\" width=\"52\" height=\"18\" rx=\"5\" fill=\"#36d1b0\"/>"
"      <text x=\"176\" y=\"163\" font-family=\"sans-serif\" font-size=\"11\" font-weight=\"700\" fill=\"#0b0e13\" text-anchor=\"middle\">4K</text>"
"    </svg>"
"    <div class=\"htitle\"><b>4K · 60fps</b><span>Sony STARVIS 2 IMX678 · H.265 / H.264</span></div>"
"    <div class=\"chips\">"
"      <div class=\"chip\">4K H.265 RTSP</div><div class=\"chip\">无插件预览</div>"
"      <div class=\"chip\">ISP 实时调节</div><div class=\"chip\">星光级低照</div>"
"    </div>"
"  </div>"
"</div>"
"<!-- 主体 -->"
"<div id=\"app\">"
"<header>"
"  <div class=\"logo\">甜</div>"
"  <div class=\"brand\"><b>甜源创新</b><small>IMX678 · 4K 智能相机</small></div>"
"  <div class=\"hstat\" id=\"hstat\" onclick=\"logout()\" title=\"点击退出\">连接中…</div>"
"</header>"
"<nav id=\"nav\">"
"  <button class=\"on\" data-t=\"pv\">预览</button>"
"  <button data-t=\"img\">图像</button>"
"  <button data-t=\"time\">时间</button>"
"  <button data-t=\"rtsp\">RTSP</button>"
"  <button data-t=\"sys\">系统</button>"
"  <button data-t=\"reg\">寄存器</button>"
"  <button data-t=\"gpio\">GPIO</button>"
"  <button data-t=\"scr\">屏幕</button>"
"  <button data-t=\"term\">终端</button>"
"</nav>"
"<main>"
"  <section class=\"tab on\" id=\"pv\">"
"    <div class=\"card\"><div class=\"pvlayout\">"
"      <div class=\"pvmain\"><div id=\"pvwrap\"><img id=\"v\" alt=\"live\"></div></div>"
"      <div class=\"pvside\">"
"        <button class=\"btn g\" onclick=\"snap()\">📸 拍照存卡</button>"
"        <button class=\"btn gray\" onclick=\"dl4k()\">⬇ 下载4K原图</button>"
"        <button class=\"btn gray\" onclick=\"rl()\">🔄 重载画面</button>"
"        <button class=\"btn gray\" onclick=\"fs()\">⛶ 全屏</button>"
"        <button class=\"btn gray\" onclick=\"flip()\">🔃 翻转</button>"
"        <div class=\"pvmini\">"
"          <div class=\"pm\"><span>帧率</span><b id=\"pv_fps\">--</b></div>"
"          <div class=\"pm\"><span>码率</span><b id=\"pv_kbps\">--</b></div>"
"          <div class=\"pm\"><span>温度</span><b id=\"pv_temp\">--</b></div>"
"        </div>"
"        <div class=\"msg\" id=\"pvmsg\"></div>"
"      </div>"
"    </div>"
"    </div>"
"  </section>"
"  <section class=\"tab\" id=\"img\">"
"    <div class=\"card\"><h4>ISP 图像调节</h4>"
"      <div style=\"text-align:center;margin-bottom:12px\"><img id=\"v2\" style=\"max-width:100%;border-radius:10px;transform:rotate(180deg);background:#000;border:1px solid var(--line)\" alt=\"live\"></div>"
"      <div class=\"row\"><label>亮度</label><input type=range min=0 max=100 value=50 id=luma><span class=val id=vl>50</span></div>"
"      <div class=\"row\"><label>对比度</label><input type=range min=0 max=100 value=50 id=contr><span class=val id=vc>50</span></div>"
"      <div class=\"row\"><label>饱和度</label><input type=range min=0 max=100 value=50 id=satu><span class=val id=vs>50</span></div>"
"      <div class=\"row\"><label>色度</label><input type=range min=0 max=100 value=50 id=hue><span class=val id=vh>50</span></div>"
"      <div class=\"btnrow\"><button class=\"btn g\" onclick=\"imgSave()\">💾 保存设置（重启保留）</button></div>"
"      <div class=\"msg\" id=\"imgmsg\"></div>"
"    </div>"
"    <div class=\"card\"><h4>🎯 对焦助手</h4>"
"      <div class=\"stat full\"><div class=\"k\">焦点值（越大越清晰）</div><div class=\"n\" id=\"fv_now\">--</div><div class=\"bar\"><i id=\"fv_bar\" style=\"width:0\"></i></div></div>"
"      <div class=\"row\" style=\"margin-top:8px\"><label>峰值</label><span class=\"val\" id=\"fv_peak\">--</span></div>"
"      <div class=\"btnrow\"><button class=\"btn g\" id=\"fvbtn\" onclick=\"fvToggle()\">开始对焦</button><button class=\"btn gray\" onclick=\"fvReset()\">清零峰值</button></div>"
"      <small style=\"color:var(--mut)\">拧动镜头，把焦点值/峰值拧到最大即最清晰。取 ISP 画面中心区的高频对比度统计。</small>"
"    </div>"
"    <div class=\"card\"><h4>🕒 OSD 时间水印</h4>"
"      <div class=\"sw\"><input type=\"checkbox\" id=\"osd_en\"><label for=\"osd_en\">在画面左上角叠加时间戳</label></div>"
"      <div class=\"sw\"><input type=\"checkbox\" id=\"osd_focus\"><label for=\"osd_focus\">叠加焦点值+对焦条（对焦时用，看着屏幕拧镜头）</label></div>"
"      <div class=\"sw\"><input type=\"checkbox\" id=\"focus_spot\" checked><label for=\"focus_spot\">点对焦（只测画面中心，并画框）——取消则全画幅平均</label></div>"
"      <div class=\"btnrow\"><button class=\"btn g\" onclick=\"saveOsd()\">保存并生效</button></div>"
"      <div class=\"msg\" id=\"osdmsg\"></div>"
"      <small style=\"color:var(--mut)\">叠加到主码流(4K)与网页预览；时间取设备时钟(见「时间」页)。焦点条会随清晰度峰化。</small>"
"    </div>"
"  </section>"
"  <section class=\"tab\" id=\"time\">"
"    <div class=\"card\"><h4>设备时间</h4>"
"      <div class=\"stat full\"><div class=\"k\">当前设备时间</div><div class=\"n\" id=\"devtime\">--</div></div>"
"      <div class=\"btnrow\" style=\"margin-top:12px\"><button class=\"btn g\" onclick=\"syncNow()\">同步为本机时间</button></div>"
"      <div class=\"fld\" style=\"margin-top:14px\"><label>或手动设置</label><input type=\"datetime-local\" id=\"dtman\" step=\"1\"></div>"
"      <div class=\"btnrow\"><button class=\"btn\" onclick=\"setManual()\">应用手动时间</button></div>"
"      <div class=\"msg\" id=\"timemsg\"></div>"
"    </div>"
"  </section>"
"  <section class=\"tab\" id=\"rtsp\">"
"    <div class=\"card\"><h4>RTSP 流地址</h4>"
"      <div class=\"url\" id=\"url_main\">rtsp://…/main</div>"
"      <div class=\"url\" id=\"url_sub\">rtsp://…/sub</div>"
"      <small style=\"color:var(--mut)\">main = 4K H.265 · sub = 1080p H.264</small>"
"    </div>"
"    <div class=\"card\"><h4>访问密码（网页登录 + RTSP 通用）</h4>"
"      <div class=\"fld\"><label>用户名</label><input type=\"text\" id=\"ruser\" value=\"admin\" autocomplete=\"off\"></div>"
"      <div class=\"fld\"><label>新密码</label><input type=\"password\" id=\"rpass\" placeholder=\"留空＝不修改\" autocomplete=\"new-password\"></div>"
"      <div class=\"sw\"><input type=\"checkbox\" id=\"authen\"><label for=\"authen\">RTSP 拉流也要求此密码（Basic Auth）</label></div>"
"      <div class=\"btnrow\"><button class=\"btn g\" onclick=\"saveRtsp()\">保存并生效</button></div>"
"      <div class=\"msg\" id=\"rtspmsg\"></div>"
"    </div>"
"  </section>"
"  <section class=\"tab\" id=\"sys\">"
"    <div class=\"card\"><h4>网络设置</h4>"
"      <div class=\"fld\"><label>IP 地址</label><input type=\"text\" id=\"net_ip\" placeholder=\"192.168.1.160\"></div>"
"      <div class=\"fld\"><label>子网掩码</label><input type=\"text\" id=\"net_mask\" value=\"255.255.255.0\"></div>"
"      <div class=\"fld\"><label>网关（可选）</label><input type=\"text\" id=\"net_gw\" placeholder=\"192.168.1.1\"></div>"
"      <div class=\"btnrow\"><button class=\"btn g\" onclick=\"saveNet()\">保存并切换</button></div>"
"      <div class=\"msg\" id=\"netmsg\"></div>"
"      <small style=\"color:var(--warn)\">改 IP 后当前页面会断开，请用新 IP 重新打开</small>"
"    </div>"
"    <div class=\"card\"><h4>运行状态</h4>"
"      <div class=\"grid\">"
"        <div class=\"stat\"><div class=\"k\">SoC 温度</div><div class=\"n\" id=\"s_temp\">--</div></div>"
"        <div class=\"stat\"><div class=\"k\">CPU 频率</div><div class=\"n\" id=\"s_freq\">--</div></div>"
"        <div class=\"stat full\"><div class=\"k\">内存 <span id=\"s_memtxt\" style=\"color:var(--mut);font-size:11px\"></span></div><div class=\"n\" id=\"s_mem\">--</div><div class=\"bar\"><i id=\"s_membar\" style=\"width:0\"></i></div></div>"
"        <div class=\"stat\"><div class=\"k\">运行时长</div><div class=\"n\" id=\"s_up\">--</div></div>"
"        <div class=\"stat\"><div class=\"k\">负载 1/5/15</div><div class=\"n\" id=\"s_load\" style=\"font-size:15px\">--</div></div>"
"      </div>"
"    </div>"
"    <div class=\"card\"><h4>编码流</h4>"
"      <table class=\"kv\">"
"        <tr><td>main (H.265 4K)</td><td id=\"s_main\">--</td></tr>"
"        <tr><td>sub (H.264 1080p)</td><td id=\"s_sub\">--</td></tr>"
"        <tr><td>preview (MJPEG)</td><td id=\"s_pv\">--</td></tr>"
"      </table>"
"    </div>"
"    <div class=\"card\"><h4>设备信息</h4>"
"      <table class=\"kv\">"
"        <tr><td>传感器</td><td id=\"s_sns\">IMX678</td></tr>"
"        <tr><td>采集分辨率</td><td id=\"s_res\">3840×2160 RAW12</td></tr>"
"        <tr><td>IP 地址</td><td id=\"s_ip\">--</td></tr>"
"        <tr><td>MMZ 已用/总</td><td id=\"s_mmz\">--</td></tr>"
"        <tr><td>内核</td><td id=\"s_kern\">--</td></tr>"
"      </table>"
"    </div>"
"  </section>"
"  <section class=\"tab\" id=\"reg\">"
"    <div class=\"card\"><h4>IMX678 关键寄存器（实时 · I2C 直读）</h4>"
"      <table class=\"regt\"><thead><tr><th>地址</th><th>名称</th><th>值</th><th>说明</th></tr></thead>"
"      <tbody id=\"regbody\"><tr><td colspan=\"4\" class=\"d\">读取中…</td></tr></tbody></table>"
"      <div class=\"btnrow\" style=\"margin-top:10px\"><button class=\"btn g\" onclick=\"pollReg()\">刷新</button></div>"
"    </div>"
"    <div class=\"card\"><h4>读任意寄存器</h4>"
"      <div class=\"regrow2\"><input type=\"text\" id=\"raddr\" placeholder=\"地址，如 0x3070\" value=\"0x3070\"><button class=\"btn\" onclick=\"readReg()\">读取</button></div>"
"      <div class=\"msg\" id=\"regmsg\"></div>"
"    </div>"
"  </section>"
"  <section class=\"tab\" id=\"gpio\">"
"    <div class=\"card\"><h4>GPIO 监测（实时刷新）</h4>"
"      <table class=\"regt\"><thead><tr><th>组</th><th>bit7 … bit0</th><th>十六进制</th></tr></thead>"
"      <tbody id=\"gpiobody\"><tr><td colspan=\"3\" class=\"d\">读取中…</td></tr></tbody></table>"
"      <small style=\"color:var(--mut)\">切日夜 / 触发某功能时看哪一位变化，即可定位 IR-CUT、红外灯等接在哪个 GPIO</small>"
"    </div>"
"    <div class=\"card\"><h4>GPIO 输出测试（找 IR-CUT / LED）</h4>"
"      <div class=\"regrow2\"><input type=\"text\" id=\"gg\" value=\"2\" placeholder=\"组\" style=\"flex:0 0 56px\"><input type=\"text\" id=\"gp\" value=\"0\" placeholder=\"脚0-7\" style=\"flex:0 0 66px\"><button class=\"btn g\" onclick=\"gpioSet(1)\">置高</button><button class=\"btn gray\" onclick=\"gpioSet(0)\">置低</button></div>"
"      <div class=\"msg\" id=\"gpiomsg\"></div>"
"      <small style=\"color:var(--warn)\">谨慎：会把该脚设为输出并驱动；只对确认可用的脚操作</small>"
"    </div>"
"  </section>"
"  <section class=\"tab\" id=\"scr\">"
"    <div class=\"card\"><h4>4.3\" DSI 屏幕（本地显示）</h4>"
"      <div class=\"sw\"><input type=\"checkbox\" id=\"scr_en\"><label for=\"scr_en\">启用屏幕显示</label></div>"
"      <div class=\"row\"><label>背光亮度</label><input type=range min=0 max=255 value=200 id=scr_bright><span class=val id=scr_bv>200</span></div>"
"      <div class=\"fld\"><label>画面来源（VPSS 通道）</label>"
"        <select id=\"scr_src\"><option value=\"0\">通道0 · 4K</option><option value=\"1\">通道1 · 1080p</option><option value=\"2\" selected>通道2 · 720p（推荐）</option><option value=\"3\">通道3 · 4K快照</option></select></div>"
"      <div class=\"sw\"><input type=\"checkbox\" id=\"scr_lr\"><label for=\"scr_lr\">左右镜像</label></div>"
"      <div class=\"btnrow\"><button class=\"btn g\" onclick=\"saveScr()\">保存并生效</button></div>"
"      <div class=\"msg\" id=\"scrmsg\"></div>"
"      <small style=\"color:var(--mut)\">状态：<span id=\"scr_run\">--</span>。屏接板子 J5（MIPI-TX），需回国接硬件后实测。来源/镜像改动在下次开屏时生效。</small>"
"    </div>"
"  </section>"
"  <section class=\"tab\" id=\"term\">"
"    <div class=\"card\"><h4>摄像头终端（root · 登录后可用）</h4>"
"      <div class=\"regrow2\"><input type=\"text\" id=\"cmd\" placeholder=\"命令，如 ls -la /opt  或  cat /proc/loadavg\" autocomplete=\"off\" onkeydown=\"if(event.key=='Enter')runCmd()\"><button class=\"btn g\" onclick=\"runCmd()\">运行</button><button class=\"btn gray\" onclick=\"$('termout').textContent=''\">清屏</button></div>"
"      <pre id=\"termout\" style=\"background:#0d1117;border:1px solid var(--line);border-radius:8px;padding:10px;max-height:52vh;overflow:auto;font-size:12px;line-height:1.45;color:#c9d5e3;white-space:pre-wrap;word-break:break-all;margin-top:10px\"></pre>"
"      <small style=\"color:var(--warn)\">直接 root 执行，命令超 10 秒自动终止。请尽快改掉默认密码 admin/admin。</small>"
"    </div>"
"  </section>"
"</main>"
"</div>"
"<script>"
"var $=function(i){return document.getElementById(i)};"
"var started=false;"
"function doLogin(){"
"  var u=$('lu').value,p=$('lp').value;"
"  fetch('/login?user='+encodeURIComponent(u)+'&pass='+encodeURIComponent(p)).then(function(r){"
"    if(r.ok){$('login').style.display='none';$('app').style.display='block';start()}"
"    else{$('lerr').innerText='用户名或密码错误';$('lp').value=''}"
"  })['catch'](function(){$('lerr').innerText='连接失败'})}"
"function logout(){fetch('/logout')['catch'](function(){});location.reload()}"
"function start(){"
"  if(started)return;started=true;"
"  $('v').src='/stream';pollInfo();"
"  setInterval(function(){if($('sys').classList.contains('on'))pollInfo();else beat();if($('gpio').classList.contains('on'))pollGpio();},3000);"
"  setInterval(function(){if($('time').classList.contains('on'))pollTime()},1000);"
"}"
"/* tabs */"
"document.querySelectorAll('#nav button').forEach(function(b){b.onclick=function(){"
"  document.querySelectorAll('#nav button').forEach(function(x){x.classList.remove('on')});b.classList.add('on');"
"  document.querySelectorAll('.tab').forEach(function(x){x.classList.remove('on')});$(b.dataset.t).classList.add('on');"
"  if(b.dataset.t=='sys'){pollInfo();loadNet();}if(b.dataset.t=='time')pollTime();if(b.dataset.t=='rtsp')loadRtsp();if(b.dataset.t=='reg')pollReg();if(b.dataset.t=='gpio')pollGpio();if(b.dataset.t=='scr')loadScr();"
"  if(b.dataset.t=='img'){$('v2').src='/stream?'+Date.now();loadOsd();}else{if($('v2'))$('v2').removeAttribute('src');fvStop();}"
"};});"
"/* preview */"
"function snap(){fetch('/save').then(function(r){return r.text()}).then(function(t){msg('pvmsg',t)})}"
"function rl(){$('v').src='/stream?'+Date.now()}"
"function fs(){var e=$('pvwrap');if(e.requestFullscreen)e.requestFullscreen();else if(e.webkitRequestFullscreen)e.webkitRequestFullscreen()}"
"var flp=1;function flip(){flp^=1;$('v').style.transform='rotate('+(flp?180:0)+'deg)'}"
"var fb=0;$('v').onerror=function(){if(fb)return;fb=1;poll()};"
"function poll(){$('v').src='/snap?'+Date.now()}"
"$('v').addEventListener('load',function(){if(fb)setTimeout(poll,150)});"
"/* isp */"
"function apply(){fetch('/set?luma='+$('luma').value+'&contr='+$('contr').value+'&satu='+$('satu').value+'&hue='+$('hue').value)}"
"[['luma','vl'],['contr','vc'],['satu','vs'],['hue','vh']].forEach(function(p){"
"  $(p[0]).oninput=function(){$(p[1]).innerText=$(p[0]).value;apply()}});"
"/* time */"
"function pad(n){return(n<10?'0':'')+n}"
"function pollTime(){fetch('/gettime').then(function(r){return r.json()}).then(function(j){$('devtime').innerText=j.t})}"
"function syncNow(){fetch('/settime?epoch='+Math.floor(Date.now()/1000)).then(function(r){return r.text()}).then(function(t){msg('timemsg',t);pollTime()})}"
"function setManual(){var v=$('dtman').value;if(!v){msg('timemsg','请先选择时间');return}"
"  var e=Math.floor(new Date(v).getTime()/1000);fetch('/settime?epoch='+e).then(function(r){return r.text()}).then(function(t){msg('timemsg',t);pollTime()})}"
"/* rtsp / account */"
"function loadRtsp(){"
"  var h=location.hostname;$('url_main').innerText='rtsp://'+h+'/main';$('url_sub').innerText='rtsp://'+h+'/sub';"
"  fetch('/getrtsp').then(function(r){return r.json()}).then(function(j){$('authen').checked=!!j.en;$('ruser').value=j.user||'admin'})}"
"function saveRtsp(){"
"  var u='/setrtsp?en='+($('authen').checked?1:0)+'&user='+encodeURIComponent($('ruser').value)+'&pass='+encodeURIComponent($('rpass').value);"
"  fetch(u).then(function(r){return r.text()}).then(function(t){msg('rtspmsg',t);$('rpass').value=''})}"
"/* sys */"
"function kb(n){n=+n;if(n>=1048576)return(n/1048576).toFixed(1)+' GB';if(n>=1024)return(n/1024).toFixed(0)+' MB';return n+' KB'}"
"function beat(){fetch('/info').then(function(r){return r.json()}).then(function(j){"
"  $('hstat').innerHTML='<b>●</b> 在线 · '+(j.temp!=null?j.temp.toFixed(0)+'°C':'');"
"  var e;if(e=$('pv_fps'))e.innerText=(j.f2!=null?j.f2.toFixed(1):'0');"
"  if(e=$('pv_kbps'))e.innerText=(j.k2!=null?(j.k2/1000).toFixed(1)+'M':'0');"
"  if(e=$('pv_temp'))e.innerText=(j.temp!=null?j.temp.toFixed(0)+'°':'--');"
"})['catch'](function(){$('hstat').innerText='离线'})}"
"function pollInfo(){fetch('/info').then(function(r){return r.json()}).then(function(j){"
"  $('s_temp').innerHTML=(j.temp!=null?j.temp.toFixed(1):'--')+'<small> °C</small>';"
"  $('s_freq').innerHTML=(j.cpumhz?j.cpumhz:'--')+'<small> MHz</small>';"
"  var mu=j.memtotal-j.memavail,pct=j.memtotal?Math.round(mu/j.memtotal*100):0;"
"  $('s_mem').innerHTML=pct+'<small>%</small>';$('s_memtxt').innerText=kb(mu)+' / '+kb(j.memtotal);$('s_membar').style.width=pct+'%';"
"  $('s_up').innerHTML=fmtup(j.uptime);$('s_load').innerText=j.load;"
"  $('s_main').innerText=stx(j.f0,j.k0);$('s_sub').innerText=stx(j.f1,j.k1);$('s_pv').innerText=stx(j.f2,j.k2);"
"  $('s_ip').innerText=j.ip||'--';$('s_mmz').innerText=j.mmz||'--';$('s_kern').innerText=j.kern||'--';"
"  $('hstat').innerHTML='<b>●</b> 在线 · '+(j.temp!=null?j.temp.toFixed(0)+'°C':'');"
"})['catch'](function(){$('hstat').innerText='离线'})}"
"function stx(f,k){return (f!=null?f.toFixed(1):'0')+' fps · '+(k!=null?(k/1000).toFixed(1):'0')+' Mbps'}"
"function fmtup(s){s=Math.floor(s);var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);"
"  return(d?d+'d ':'')+pad(h)+':'+pad(m)}"
"function msg(id,t){$(id).innerText=t;setTimeout(function(){if($(id).innerText==t)$(id).innerText=''},3000)}"
"/* network */"
"function loadNet(){fetch('/getnet').then(function(r){return r.json()}).then(function(j){"
"  $('net_ip').value=j.ip||j.cur||'';$('net_mask').value=j.mask||'255.255.255.0';$('net_gw').value=j.gw||''})['catch'](function(){})}"
"function saveNet(){var u='/setnet?ip='+encodeURIComponent($('net_ip').value)+'&mask='+encodeURIComponent($('net_mask').value)+'&gw='+encodeURIComponent($('net_gw').value);"
"  msg('netmsg','正在保存…');fetch(u).then(function(r){return r.text()}).then(function(t){msg('netmsg',t)})['catch'](function(){msg('netmsg','已切换，请用新 IP 打开')})}"
"/* screen */"
"function scrStat(r){$('scr_run').innerText=r?'已开启':'已关闭'}"
"function loadScr(){fetch('/getscr').then(function(r){return r.json()}).then(function(j){"
"  $('scr_en').checked=!!j.en;$('scr_bright').value=j.bright;$('scr_bv').innerText=j.bright;"
"  $('scr_src').value=j.src;$('scr_lr').checked=!!j.lr;scrStat(j.run)})['catch'](function(){})}"
"$('scr_bright').oninput=function(){$('scr_bv').innerText=$('scr_bright').value};"
"function saveScr(){var u='/setscr?en='+($('scr_en').checked?1:0)+'&bright='+$('scr_bright').value+'&src='+$('scr_src').value+'&lr='+($('scr_lr').checked?1:0);"
"  msg('scrmsg','正在应用…');fetch(u).then(function(r){return r.text()}).then(function(t){msg('scrmsg',t);loadScr()})['catch'](function(){msg('scrmsg','失败')})}"
"/* focus assist */"
"var fvTimer=null,fvMax=0;"
"function fvStop(){if(fvTimer){clearInterval(fvTimer);fvTimer=null;}var e=$('fvbtn');if(e)e.textContent='开始对焦'}"
"function fvReset(){fvMax=0;var e;if(e=$('fv_peak'))e.innerText='--';if(e=$('fv_bar'))e.style.width='0'}"
"function fvToggle(){if(fvTimer){fvStop();return}$('fvbtn').textContent='停止';fvTimer=setInterval(function(){"
"  fetch('/focus').then(function(r){return r.json()}).then(function(j){"
"    if(!j.ok){$('fv_now').innerText='AF未启用';return}var v=j.fv;if(v>fvMax)fvMax=v;"
"    $('fv_now').innerText=v;$('fv_peak').innerText=fvMax;$('fv_bar').style.width=(fvMax>0?Math.round(v/fvMax*100):0)+'%'"
"  })['catch'](function(){})},300)}"
"/* osd */"
"function loadOsd(){fetch('/getosd').then(function(r){return r.json()}).then(function(j){$('osd_en').checked=!!j.en;if($('osd_focus'))$('osd_focus').checked=!!j.focus;if($('focus_spot'))$('focus_spot').checked=!!j.spot})['catch'](function(){})}"
"function saveOsd(){fetch('/setosd?en='+($('osd_en').checked?1:0)+'&focus='+($('osd_focus').checked?1:0)+'&spot='+($('focus_spot').checked?1:0)).then(function(r){return r.text()}).then(function(t){msg('osdmsg',t)})['catch'](function(){msg('osdmsg','失败')})}"
"/* isp save + gpio */"
"function imgSave(){fetch('/imgsave').then(function(r){return r.text()}).then(function(t){msg('imgmsg',t)})['catch'](function(){msg('imgmsg','保存失败')})}"
"function pollGpio(){fetch('/gpio').then(function(r){return r.json()}).then(function(j){"
"  var h='';for(var g in j){var v=j[g];var b=(v<0)?'--':('0000000'+v.toString(2)).slice(-8);var hx=(v<0)?'--':'0x'+('0'+v.toString(16)).slice(-2);"
"    h+='<tr><td class=a>组'+g+'</td><td class=v>'+b+'</td><td class=v>'+hx+'</td></tr>';}"
"  $('gpiobody').innerHTML=h})['catch'](function(){$('gpiobody').innerHTML='<tr><td colspan=3 class=d>读取失败</td></tr>'})}"
"function gpioSet(v){var u='/gpioset?g='+encodeURIComponent($('gg').value)+'&p='+encodeURIComponent($('gp').value)+'&v='+v;"
"  fetch(u).then(function(r){return r.text()}).then(function(t){msg('gpiomsg','组'+$('gg').value+' 脚'+$('gp').value+' → '+(v?'高':'低'));pollGpio()})['catch'](function(){msg('gpiomsg','失败')})}"
"/* web terminal */"
"function runCmd(){var c=$('cmd').value;if(!c)return;var o=$('termout');o.textContent+='$ '+c+'\\n';$('cmd').value='';"
"  fetch('/exec?cmd='+encodeURIComponent(c)).then(function(r){return r.text()}).then(function(t){o.textContent+=t+((t&&t.slice(-1)=='\\n')?'':'\\n');o.scrollTop=o.scrollHeight})['catch'](function(){o.textContent+='[执行失败]\\n'})}"
"/* registers + 4K download */"
"var REGS=["
" {a:['3000'],n:'STANDBY',d:'待机控制：0=工作，1=待机'},"
" {a:['3002'],n:'XMSTA',d:'流输出：0=输出中，1=停止'},"
" {a:['3014'],n:'INCK_SEL',d:'输入参考时钟：本板 0=74.25MHz'},"
" {a:['302a','3029','3028'],n:'VMAX',d:'帧长(20bit)：越大帧率越低'},"
" {a:['302d','302c'],n:'HMAX',d:'行长：每行像素周期'},"
" {a:['3052','3051','3050'],n:'SHR0',d:'曝光快门：越大曝光时间越短'},"
" {a:['3071','3070'],n:'GAIN',d:'模拟增益(11bit)：约 0.3dB/步'}"
"];"
"function rcompose(j,arr){var v=0,ok=false;for(var i=0;i<arr.length;i++){var x=j[arr[i]];if(x==null||x<0)return null;v=(v*256)+x;ok=true}return ok?v:null}"
"function pollReg(){fetch('/regs').then(function(r){return r.json()}).then(function(j){"
"  var h='';for(var i=0;i<REGS.length;i++){var R=REGS[i],v=rcompose(j,R.a),base=R.a[R.a.length-1];"
"    var vs=(v==null)?'<span style=color:var(--danger)>读失败</span>':(v+' (0x'+v.toString(16)+')');"
"    h+='<tr><td class=a>0x'+base+'</td><td>'+R.n+'</td><td class=v>'+vs+'</td><td class=d>'+R.d+'</td></tr>'}"
"  $('regbody').innerHTML=h})['catch'](function(){$('regbody').innerHTML='<tr><td colspan=4 class=d>读取失败</td></tr>'})}"
"function readReg(){var a=$('raddr').value;fetch('/reg?a='+encodeURIComponent(a)).then(function(r){return r.json()}).then(function(j){"
"  msg('regmsg','0x'+j.a+' = '+(j.v<0?'读失败':(j.v+' (0x'+j.v.toString(16)+')')))})['catch'](function(){msg('regmsg','读失败')})}"
"function dl4k(){msg('pvmsg','正在生成4K原图…');fetch('/down4k').then(function(r){if(!r.ok)throw 0;return r.blob()}).then(function(b){"
"  var u=URL.createObjectURL(b),a=document.createElement('a');a.href=u;a.download='imx678_4k.jpg';document.body.appendChild(a);a.click();a.remove();"
"  setTimeout(function(){URL.revokeObjectURL(u)},2000);msg('pvmsg','4K原图已下载')})['catch'](function(){msg('pvmsg','4K原图未就绪')})}"
"/* auto: if already authorized (cookie), skip login */"
"fetch('/info').then(function(r){if(r.ok){$('login').style.display='none';$('app').style.display='block';start()}});"
"</script></body></html>";


/* =============== 甜源创新 unified cam: account/session, live stats, sysinfo =============== */
#include <time.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/mman.h>
#include "ot_common_region.h"
#include "ss_mpi_region.h"

/* shared credentials: web login + optional RTSP basic-auth */
static char g_cam_user[32] = "admin";
static char g_cam_pass[32] = "admin";
static int  g_rtsp_auth_en = 0;
static char g_net_ip[20]   = "";                /* saved static IP; empty = keep boot default (.160) */
static char g_net_mask[20] = "255.255.255.0";
static char g_net_gw[20]   = "";
static int  g_isp_luma = 50, g_isp_contr = 50, g_isp_satu = 50, g_isp_hue = 50;  /* saved ISP CSC */
/* 4.3" DSI 屏 (VO+mipi_tx). 默认关闭 —— g_scr_en=0 时不碰 VO/mipi_tx/i2c, 未接硬件也可安全固化 */
static int  g_scr_en = 0;          /* 0=关(默认) 1=开 */
static int  g_scr_bright = 200;    /* 背光 0..255 */
static int  g_scr_src = 2;         /* 镜像哪个 VPSS 物理通道到屏 (2=720p 预览) */
static int  g_scr_lr = 0;          /* 0=正常 1=左右镜像 (attiny LCD_LR) */
static volatile int g_scr_running = 0;
/* OSD 时间戳水印 (MPP region overlay, ARGB1555). 默认开 */
static int  g_osd_en = 1;
static int  g_osd_ready = 0;       /* region 创建+attach 成功 */
/* 对焦助手 (ISP AF 统计焦点值). g_af_ok = AF 统计已启用 */
static int  g_af_ok = 0;
static int  g_osd_focus_en = 0;    /* 把焦点值+对焦条叠到画面上(对焦时开, 默认关) */
static int  g_focus_spot = 1;      /* 1=中心点对焦(默认) 0=全画幅平均 */
static unsigned long long g_focus_peak = 0;  /* 对焦条峰值(缩放用) */
static pthread_mutex_t g_cfg_lock = PTHREAD_MUTEX_INITIALIZER;
#define CAM_CFG_PATHS_N 3
static const char *g_cfg_paths[CAM_CFG_PATHS_N] = { "/tmp/param/cam.conf", "/root/cam.conf", "/tmp/cam.conf" };

/* mount flash 'param' partition (mtd6, jffs2, ~1.4MB) for config that survives reboot.
 * Only touches mtd6 (never boot/kernel) so it can't brick U-Boot. */
static void cam_param_mount(void)
{
    (void)system("mkdir -p /tmp/param 2>/dev/null; "
                 "grep -q ' /tmp/param ' /proc/mounts || mount -t jffs2 /dev/mtdblock6 /tmp/param 2>/dev/null");
}

/* live per-stream stats: 0=main(H265) 1=sub(H264) 2=preview(MJPEG) */
static volatile unsigned long g_enc_frames[3] = {0, 0, 0};
static volatile unsigned long g_enc_bytes[3]  = {0, 0, 0};
static volatile double g_enc_fps[3]  = {0, 0, 0};
static volatile double g_enc_kbps[3] = {0, 0, 0};

/* session tokens (cookie: camtk=...) */
#define CAM_TOK_N 8
static char g_tok[CAM_TOK_N][24];
static int  g_tok_slot = 0;
static pthread_mutex_t g_tok_lock = PTHREAD_MUTEX_INITIALIZER;

static void cam_cfg_save(void)
{
    int i; FILE *f = TD_NULL;
    for (i = 0; i < CAM_CFG_PATHS_N; i++) { f = fopen(g_cfg_paths[i], "w"); if (f != TD_NULL) { break; } }
    if (f == TD_NULL) { return; }
    pthread_mutex_lock(&g_cfg_lock);
    fprintf(f, "user=%s\npass=%s\nrtsp_auth=%d\nip=%s\nmask=%s\ngw=%s\nluma=%d\ncontr=%d\nsatu=%d\nhue=%d\n"
               "scr_en=%d\nscr_bright=%d\nscr_src=%d\nscr_lr=%d\nosd_en=%d\nosd_focus=%d\nfocus_spot=%d\n",
        g_cam_user, g_cam_pass, g_rtsp_auth_en, g_net_ip, g_net_mask, g_net_gw,
        g_isp_luma, g_isp_contr, g_isp_satu, g_isp_hue,
        g_scr_en, g_scr_bright, g_scr_src, g_scr_lr, g_osd_en, g_osd_focus_en, g_focus_spot);
    pthread_mutex_unlock(&g_cfg_lock);
    fclose(f);
}

static void cam_cfg_load(void)
{
    int i; FILE *f = TD_NULL; char line[128];
    for (i = 0; i < CAM_CFG_PATHS_N; i++) { f = fopen(g_cfg_paths[i], "r"); if (f != TD_NULL) { break; } }
    if (f == TD_NULL) { return; }
    pthread_mutex_lock(&g_cfg_lock);
    while (fgets(line, sizeof(line), f) != TD_NULL) {
        char *nl = strpbrk(line, "\r\n"); if (nl != TD_NULL) { *nl = '\0'; }
        if (!strncmp(line, "user=", 5)) { snprintf(g_cam_user, sizeof(g_cam_user), "%s", line + 5); }
        else if (!strncmp(line, "pass=", 5)) { snprintf(g_cam_pass, sizeof(g_cam_pass), "%s", line + 5); }
        else if (!strncmp(line, "rtsp_auth=", 10)) { g_rtsp_auth_en = atoi(line + 10); }
        else if (!strncmp(line, "ip=", 3)) { snprintf(g_net_ip, sizeof(g_net_ip), "%s", line + 3); }
        else if (!strncmp(line, "mask=", 5)) { snprintf(g_net_mask, sizeof(g_net_mask), "%s", line + 5); }
        else if (!strncmp(line, "gw=", 3)) { snprintf(g_net_gw, sizeof(g_net_gw), "%s", line + 3); }
        else if (!strncmp(line, "luma=", 5)) { g_isp_luma = atoi(line + 5); }
        else if (!strncmp(line, "contr=", 6)) { g_isp_contr = atoi(line + 6); }
        else if (!strncmp(line, "satu=", 5)) { g_isp_satu = atoi(line + 5); }
        else if (!strncmp(line, "hue=", 4)) { g_isp_hue = atoi(line + 4); }
        else if (!strncmp(line, "scr_en=", 7)) { g_scr_en = atoi(line + 7) ? 1 : 0; }
        else if (!strncmp(line, "scr_bright=", 11)) { g_scr_bright = atoi(line + 11); }
        else if (!strncmp(line, "scr_src=", 8)) { g_scr_src = atoi(line + 8); }
        else if (!strncmp(line, "scr_lr=", 7)) { g_scr_lr = atoi(line + 7) ? 1 : 0; }
        else if (!strncmp(line, "osd_en=", 7)) { g_osd_en = atoi(line + 7) ? 1 : 0; }
        else if (!strncmp(line, "osd_focus=", 10)) { g_osd_focus_en = atoi(line + 10) ? 1 : 0; }
        else if (!strncmp(line, "focus_spot=", 11)) { g_focus_spot = atoi(line + 11) ? 1 : 0; }
    }
    pthread_mutex_unlock(&g_cfg_lock);
    fclose(f);
}

/* apply saved static IP (called at startup after cfg load, and on /setnet) */
static void cam_net_apply(void)
{
    char ip[20], mask[20], gw[20], cmd[192];
    pthread_mutex_lock(&g_cfg_lock);
    snprintf(ip, sizeof(ip), "%s", g_net_ip);
    snprintf(mask, sizeof(mask), "%s", g_net_mask[0] ? g_net_mask : "255.255.255.0");
    snprintf(gw, sizeof(gw), "%s", g_net_gw);
    pthread_mutex_unlock(&g_cfg_lock);
    if (ip[0] == '\0') { return; }
    snprintf(cmd, sizeof(cmd), "ifconfig eth0 %s netmask %s up 2>/dev/null", ip, mask);
    (void)system(cmd);
    if (gw[0] != '\0') {
        snprintf(cmd, sizeof(cmd), "route add default gw %s 2>/dev/null", gw);
        (void)system(cmd);
    }
}

/* start dropbear SSH (:22). host key persisted in param partition so it's stable across reboots. */
static void cam_start_sshd(void)
{
    /* generate ed25519 host key once (into param), then launch dropbear (it daemonizes) */
    (void)system("[ -s /tmp/param/dropbear_hk ] || /opt/bin/dropbearmulti dropbearkey -t ed25519 -f /tmp/param/dropbear_hk >/dev/null 2>&1");
    (void)system("pidof dropbearmulti >/dev/null 2>&1 || /opt/bin/dropbearmulti dropbear -r /tmp/param/dropbear_hk -p 22 >/dev/null 2>&1");
}

/* ===================== 4.3in DSI 屏 (VO + mipi_tx) —— 默认关闭 =====================
 * Waveshare 800x480 RGB888 MIPI-DSI, 桥片 TC358762, 伴随 attiny@i2c-2 0x45 管电源/背光.
 * g_scr_en=0(默认)时完全不碰 VO/mipi_tx/i2c —— 未接硬件也能安全固化, 无任何副作用.
 * 硬件接板子 J5 后回国实测; sync/phy_data_rate 等标 [HW] 项待微调.
 * 完整硬件接线与规格见 tianyuan_pkg/display/dsi_panel_43.c */
#define SCR_VO_DEV_FRAME_RATE 60
#define SCR_I2C_BUS     "/dev/i2c-2"    /* J5 的 I2C2(pin23/24); 若非 i2c-2, i2cdetect 各总线找 0x45 */
#define SCR_ATTINY_ADDR 0x45
#define SCR_REG_PORTA 0x81
#define SCR_REG_PORTB 0x82
#define SCR_REG_PORTC 0x83
#define SCR_REG_PWM   0x86
#define SCR_PA_LCD_LR       0x04
#define SCR_PB_LCD_MAIN     0x80
#define SCR_PC_LED_EN       0x01
#define SCR_PC_RST_TP_N     0x02
#define SCR_PC_RST_LCD_N    0x04
#define SCR_PC_RST_BRIDGE_N 0x08

static int scr_attiny_wr(int fd, unsigned char reg, unsigned char val)
{
    unsigned char b[2] = { reg, val };
    struct i2c_msg m;
    struct i2c_rdwr_ioctl_data x;
    (td_void)memset_s(&m, sizeof(m), 0, sizeof(m));
    m.addr = SCR_ATTINY_ADDR; m.flags = 0; m.len = 2; m.buf = b;
    x.msgs = &m; x.nmsgs = 1;
    return ioctl(fd, I2C_RDWR, &x) < 0 ? -1 : 0;
}

/* attiny 上电点亮 (照 rpi attiny_lcd_power_enable 时序); lr=1 左右镜像 */
static int scr_power_on(int brightness, int lr)
{
    int fd = open(SCR_I2C_BUS, O_RDWR);
    if (fd < 0) { return -1; }
    scr_attiny_wr(fd, SCR_REG_PORTC, 0x00);                      usleep(8000);
    scr_attiny_wr(fd, SCR_REG_PORTA, lr ? SCR_PA_LCD_LR : 0x00); usleep(8000);
    scr_attiny_wr(fd, SCR_REG_PORTB, SCR_PB_LCD_MAIN);           usleep(8000);
    scr_attiny_wr(fd, SCR_REG_PORTC,
                  SCR_PC_LED_EN | SCR_PC_RST_LCD_N | SCR_PC_RST_BRIDGE_N | SCR_PC_RST_TP_N);
    usleep(80000);
    scr_attiny_wr(fd, SCR_REG_PWM, (unsigned char)(0xff - (brightness & 0xff)));
    close(fd);
    return 0;
}
static void scr_power_off(void)
{
    int fd = open(SCR_I2C_BUS, O_RDWR);
    if (fd < 0) { return; }
    scr_attiny_wr(fd, SCR_REG_PWM, 0x00);   usleep(8000);
    scr_attiny_wr(fd, SCR_REG_PORTA, 0x00); usleep(8000);
    scr_attiny_wr(fd, SCR_REG_PORTB, 0x02); usleep(8000);   /* PB_LCD_VCC_N */
    scr_attiny_wr(fd, SCR_REG_PORTC, 0x00); usleep(30000);
    close(fd);
}
static void scr_set_bright(int brightness)   /* 运行中改背光 */
{
    int fd = open(SCR_I2C_BUS, O_RDWR);
    if (fd < 0) { return; }
    scr_attiny_wr(fd, SCR_REG_PWM, (unsigned char)(0xff - (brightness & 0xff)));
    close(fd);
}

/* 无 DSI DCS 初始化(桥片由 attiny 上电就绪), cmd_count=0 */
static const sample_vo_mipi_tx_cfg g_scr_cfg = {
    .vo_config = {
        .vo_dev = SAMPLE_VO_DEV_UHD,
        .vo_layer = SAMPLE_VO_LAYER_VHD0,
        .vo_intf_type = OT_VO_INTF_MIPI,
        .intf_sync = OT_VO_OUT_USER,
        .bg_color = COLOR_RGB_BLACK,
        .pix_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420,
        .disp_rect = {0, 0, 800, 480},
        .image_size = {800, 480},
        .vo_part_mode = OT_VO_PARTITION_MODE_SINGLE,
        .dis_buf_len = 3,
        .dst_dynamic_range = OT_DYNAMIC_RANGE_SDR8,
        .vo_mode = VO_MODE_1MUX,
        .compress_mode = OT_COMPRESS_MODE_NONE,
        /* [HW3] ot_vo_sync_info 18 字段(已按 out/include/ot_common_vo_dev.h 顺序核对):
         * syncm iop intfb | vact vbb vfb | hact hbb hfb hmid | bvact bvbb bvfb | hpw vpw | idv ihs ivs */
        .sync_info = {1, 1, 0, 480, 22, 7, 800, 45, 59, 0, 0, 0, 0, 2, 2, 0, 0, 0},
        .user_sync = {
            .clk_reverse_en = TD_FALSE,
            .op_mode = OT_OP_MODE_AUTO,
            .auto_user_sync_info = { .pixel_clk = 27777000 },   /* Hz (27.777MHz), AUTO 模式 SDK 自算 PLL */
        },
        .dev_frame_rate = SCR_VO_DEV_FRAME_RATE,
    },
    .tx_config = {
        .intf_sync = OT_MIPI_TX_OUT_USER,
        .cmd_count = 0,
        .cmd_info = NULL,
        .combo_dev_cfg = {
            .devno = 0,
            .lane_id = {0, -1, -1, -1},          /* 1 lane; 2-lane 屏用 {0,1,-1,-1} */
            .out_mode = OUT_MODE_DSI_VIDEO,
            .video_mode = BURST_MODE,            /* 花屏可试 NON_BURST_MODE_SYNC_PULSES */
            .out_format = OUT_FORMAT_RGB_24BIT,  /* RGB888 */
            .sync_info = { .hact = 800, .hfp = 59, .hbp = 45, .hpw = 2,
                           .vact = 480, .vfp = 7,  .vbp = 22, .vpw = 2 },
            .phy_data_rate = 720,                /* Mbps/lane [HW4] 微调 */
            .pixel_clk = 27777,                  /* KHz */
        },
    },
};

/* 开屏: attiny 上电 -> 起 VO -> 起 mipi_tx -> 把一个正在产帧的 VPSS 通道镜像到 VO.
 * 全程非致命: 任一步失败只打印, 不影响相机主流程. */
static int cam_scr_start(void)
{
    int ret, src;
    if (g_scr_running) { return TD_SUCCESS; }
    scr_power_on(g_scr_bright, g_scr_lr);
    ret = sample_comm_vo_start_vo(&g_scr_cfg.vo_config);
    if (ret != TD_SUCCESS) { printf("[scr] vo start fail %#x\n", ret); scr_power_off(); return ret; }
    ret = sample_comm_start_mipi_tx(&g_scr_cfg.tx_config);
    if (ret != TD_SUCCESS) {
        printf("[scr] mipi_tx start fail %#x\n", ret);
        sample_comm_vo_stop_vo(&g_scr_cfg.vo_config); scr_power_off(); return ret;
    }
    /* ss_mpi_sys_bind 以"目的地(VO)"为键: 同一 VPSS 源可同时喂 venc 与 VO,
     * 故此绑定不破坏原有 vpss->venc; VO 内部把源缩放到 800x480. */
    src = (g_scr_src >= 0 && g_scr_src < OT_VPSS_MAX_PHYS_CHN_NUM) ? g_scr_src : 2;
    ret = sample_comm_vpss_bind_vo(0, src, g_scr_cfg.vo_config.vo_layer, 0);
    if (ret != TD_SUCCESS) { printf("[scr] bind vpss%d->vo fail %#x (画面可能不出)\n", src, ret); }
    g_scr_running = 1;
    printf("[scr] 4.3in DSI 800x480 up (src vpss chn %d, bright %d)\n", src, g_scr_bright);
    return TD_SUCCESS;
}

static void cam_scr_stop(void)
{
    int src;
    if (!g_scr_running) { return; }
    src = (g_scr_src >= 0 && g_scr_src < OT_VPSS_MAX_PHYS_CHN_NUM) ? g_scr_src : 2;
    (td_void)sample_comm_vpss_un_bind_vo(0, src, g_scr_cfg.vo_config.vo_layer, 0);
    sample_comm_stop_mipi_tx(g_scr_cfg.vo_config.vo_intf_type);
    (td_void)sample_comm_vo_stop_vo(&g_scr_cfg.vo_config);
    scr_power_off();
    g_scr_running = 0;
}

/* ===================== 对焦助手 (ISP AF 焦点统计) =====================
 * 手动镜头拧到"焦点值"最大即最清晰. ISP 硬件按 15x17 分区算高频对比度(FIR/IIR),
 * 取中心区 h1+v1 之和作为焦点值. 需先启用 BE-AF 统计. */
static void cam_af_enable(void)
{
    ot_isp_stats_cfg sc;
    /* 厂商默认 AF 配置(取自 isp_af.c g_focus_cfg): af_en + IIR/FIR 滤波系数.
     * 只置 af_en 而不给滤波系数会导致对比度恒为 0 —— 必须整份下发. */
    static const ot_isp_focus_stats_cfg fdef = {
        {1, 17, 15, 1, 0, {0, 0, 0, 1920, 1080}, {0, 0, 0, 1920, 1080}, 0, {0x0, 0x1, 0}, {1, 0x9bff}, 0xf0},
        {1, {1, 1, 1}, 15, {188, 414, -330, 486, -461, 400, -328}, {7, 0, 3, 1}, {1, 0, 255, 0, 240, 8, 14}, {127, 12, 2047}},
        {0, {1, 1, 0}, 2, {200, 200, -110, 461, -415, 0, 0}, {6, 0, 1, 0}, {0, 0, 0, 0, 0, 0, 0}, {15, 12, 2047}},
        {{20, 16, 0, -16, -20}, {1, 0, 255, 0, 220, 8, 14}, {38, 12, 1800}},
        {{-12, -24, 0, 24, 12}, {1, 0, 255, 0, 220, 8, 14}, {15, 12, 2047}},
        {4, {0, 0}, {1, 1}, 0}
    };
    if (ot_mpi_isp_get_stats_cfg(0, &sc) != TD_SUCCESS) { return; }
    sc.ctrl.bit1_be_af_stat = 1;
    sc.update.bit1_be_af_stat = 1;
    sc.focus_cfg = fdef;
    if (ot_mpi_isp_set_stats_cfg(0, &sc) == TD_SUCCESS) { g_af_ok = 1; }
}

/* 中心点对焦: 只取画面正中 3x3 分区 (15x17 网格的 行6-8 × 列7-9). 画面上画的框正对这块区域 */
#define FOCUS_SPOT_R0 6
#define FOCUS_SPOT_R1 8
#define FOCUS_SPOT_C0 7
#define FOCUS_SPOT_C1 9
/* 焦点值 = 所选区分区 (h1+h2+v1+v2) 之和; 越大越锐. g_focus_spot=1 只算中心块, 0 算全画幅. 失败 -1 */
static long long cam_focus_value(void)
{
    ot_isp_af_stats af;
    int r, c, r0, r1, c0, c1;
    unsigned long long s = 0;
    if (!g_af_ok) { return -1; }
    if (ot_mpi_isp_get_focus_stats(0, &af) != TD_SUCCESS) { return -1; }
    if (g_focus_spot) {
        r0 = FOCUS_SPOT_R0; r1 = FOCUS_SPOT_R1; c0 = FOCUS_SPOT_C0; c1 = FOCUS_SPOT_C1;
    } else {
        r0 = 0; r1 = OT_ISP_AF_ZONE_ROW - 1; c0 = 0; c1 = OT_ISP_AF_ZONE_COLUMN - 1;
    }
    for (r = r0; r <= r1; r++) {
        for (c = c0; c <= c1; c++) {
            ot_isp_focus_zone *z = &af.be_af_stat.zone_metrics[r][c];
            s += (unsigned long long)z->h1 + z->h2 + z->v1 + z->v2;
        }
    }
    return (long long)s;
}

/* ===================== OSD 时间戳水印 (MPP region overlay) =====================
 * ARGB1555 overlay 贴到 venc chn0(主4K) + chn2(预览) 左上角, 每秒刷新. 8x16 字体放大 OSD_SCALE 倍. */
/* 8x16 1-bit font: '0'-'9' '-' ':' ' ' (DejaVuSansMono-Bold, idx: 0-9=digits,10='-',11=':',12=' ') */
static const unsigned char g_osd_font[13][16] = {
  {0x00,0x00,0x38,0x6c,0xc6,0xc6,0xd6,0xd6,0xc6,0xc6,0xc6,0x6c,0x38,0x00,0x00,0x00}, /* '0' */
  {0x00,0x00,0x78,0xd8,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0xfe,0x00,0x00,0x00}, /* '1' */
  {0x00,0x00,0x78,0x8c,0x0e,0x0e,0x0e,0x1c,0x18,0x38,0x70,0xe0,0xfe,0x00,0x00,0x00}, /* '2' */
  {0x00,0x00,0x7c,0x8e,0x06,0x0e,0x3c,0x0c,0x06,0x06,0x06,0x8e,0x7c,0x00,0x00,0x00}, /* '3' */
  {0x00,0x00,0x1c,0x1c,0x3c,0x7c,0x6c,0xcc,0xcc,0xfe,0x0c,0x0c,0x0c,0x00,0x00,0x00}, /* '4' */
  {0x00,0x00,0xfc,0xc0,0xc0,0xc0,0xf8,0x8e,0x06,0x06,0x06,0x8c,0x78,0x00,0x00,0x00}, /* '5' */
  {0x00,0x00,0x3c,0x62,0xc0,0xc0,0xfc,0xee,0xc6,0xc6,0xc6,0x6e,0x3c,0x00,0x00,0x00}, /* '6' */
  {0x00,0x00,0xfe,0x0e,0x0e,0x0c,0x1c,0x18,0x38,0x38,0x30,0x70,0x70,0x00,0x00,0x00}, /* '7' */
  {0x00,0x00,0x7c,0xee,0xc6,0xee,0x7c,0xee,0xc6,0xc6,0xc6,0xee,0x7c,0x00,0x00,0x00}, /* '8' */
  {0x00,0x00,0x78,0xec,0xc6,0xc6,0xc6,0xee,0x7e,0x06,0x06,0x8c,0x78,0x00,0x00,0x00}, /* '9' */
  {0x00,0x00,0x00,0x00,0x00,0x7c,0x7c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* '-' */
  {0x00,0x00,0x00,0x00,0x38,0x38,0x38,0x00,0x00,0x38,0x38,0x38,0x00,0x00,0x00,0x00}, /* ':' */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
};
#define OSD_HANDLE 0
#define OSD_SCALE  3
#define OSD_NCH    19              /* "YYYY-MM-DD HH:MM:SS" */
#define OSD_CW     (8 * OSD_SCALE)         /* 24 字符宽 */
#define OSD_LH     (16 * OSD_SCALE)        /* 48 行高 */
#define OSD_W      (OSD_NCH * OSD_CW)      /* 456 */
#define OSD_L2_Y   (OSD_LH + 8)            /* 56  第二行=焦点值 */
#define OSD_BAR_Y  (OSD_L2_Y + OSD_LH + 6) /* 110 对焦条 */
#define OSD_BAR_H  18
#define OSD_H      (OSD_BAR_Y + OSD_BAR_H + 2)  /* 130 (含时间行+焦点行+对焦条) */
/* 中心对焦框(独立 region 贴预览通道 chn2 720p): 正对焦点测量的中心 3x3 分区.
 * 分区网格 15行×17列 -> 中心块 = 列7..10/17 × 行6..9/15 映射到 1280x720: */
#define BOX_HANDLE 1
#define BOX_X 526          /* 7/17*1280≈527 -> 对齐 526 */
#define BOX_Y 288          /* 6/15*720=288 */
#define BOX_W 226          /* (10/17-7/17)*1280≈226 */
#define BOX_H 144          /* (9/15-6/15)*720=144 */
static int g_box_ready = 0;

static int cam_osd_gidx(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c == '-') { return 10; }
    if (c == ':') { return 11; }
    return 12; /* space / unknown */
}

/* 把 ts 文本以 color 画进 canvas, 原点偏移 (offx,offy). ARGB1555: 白=0xFFFF 黑=0x8000 */
static void cam_osd_blit(unsigned char *base, int stride, const char *ts, int offx, int offy, unsigned short color)
{
    int i, row, col, sy, sx;
    for (i = 0; i < OSD_NCH && ts[i]; i++) {
        const unsigned char *gl = g_osd_font[cam_osd_gidx(ts[i])];
        int ox = offx + i * OSD_CW;
        for (row = 0; row < 16; row++) {
            unsigned char bits = gl[row];
            if (bits == 0) { continue; }
            for (col = 0; col < 8; col++) {
                if (!(bits & (0x80 >> col))) { continue; }
                for (sy = 0; sy < OSD_SCALE; sy++) {
                    int py = offy + row * OSD_SCALE + sy;
                    unsigned short *p = (unsigned short *)(base + py * stride) + (ox + col * OSD_SCALE);
                    for (sx = 0; sx < OSD_SCALE; sx++) { p[sx] = color; }
                }
            }
        }
    }
}

/* 画中心对焦框(空心矩形, 1px黑外圈+2px白边, 内部透明不挡画面) */
static void cam_focusbox_draw(unsigned char *base, int stride, int w, int h)
{
    int r, c;
    for (r = 0; r < h; r++) {
        unsigned short *p = (unsigned short *)(base + r * stride);
        for (c = 0; c < w; c++) {
            int d = r;
            if (h - 1 - r < d) { d = h - 1 - r; }
            if (c < d) { d = c; }
            if (w - 1 - c < d) { d = w - 1 - c; }
            p[c] = (d == 0) ? 0x8000 : (d <= 2 ? 0xFFFF : 0x0000);
        }
    }
}

/* create overlay region 0 (OSD text, chn0+chn2 左上) + region 1 (对焦框, chn2 中心) */
static void cam_osd_init(void)
{
    ot_rgn_attr ra;
    ot_rgn_chn_attr ca;
    ot_mpp_chn chn;
    (td_void)memset_s(&ra, sizeof(ra), 0, sizeof(ra));
    ra.type = OT_RGN_OVERLAY;
    ra.attr.overlay.pixel_format = OT_PIXEL_FORMAT_ARGB_1555;
    ra.attr.overlay.bg_color = 0x0000;     /* 透明 */
    ra.attr.overlay.size.width = OSD_W;
    ra.attr.overlay.size.height = OSD_H;
    ra.attr.overlay.canvas_num = 2;
    if (ss_mpi_rgn_create(OSD_HANDLE, &ra) != TD_SUCCESS) { printf("[osd] rgn_create fail\n"); return; }
    (td_void)memset_s(&ca, sizeof(ca), 0, sizeof(ca));
    ca.is_show = TD_TRUE;
    ca.type = OT_RGN_OVERLAY;
    ca.attr.overlay_chn.point.x = 16;
    ca.attr.overlay_chn.point.y = 16;
    ca.attr.overlay_chn.fg_alpha = 255;
    ca.attr.overlay_chn.bg_alpha = 0;
    ca.attr.overlay_chn.layer = 0;
    chn.mod_id = OT_ID_VENC; chn.dev_id = 0;
    chn.chn_id = 0; (td_void)ss_mpi_rgn_attach_to_chn(OSD_HANDLE, &chn, &ca);   /* main 4K H265 */
    chn.chn_id = 2; (td_void)ss_mpi_rgn_attach_to_chn(OSD_HANDLE, &chn, &ca);   /* preview 720p MJPEG */
    g_osd_ready = 1;
    printf("[osd] timestamp overlay up (chn0+chn2)\n");

    /* region 1: 中心对焦框, 只贴预览通道 chn2(720p, 网页/DSI 屏看的那路) */
    (td_void)memset_s(&ra, sizeof(ra), 0, sizeof(ra));
    ra.type = OT_RGN_OVERLAY;
    ra.attr.overlay.pixel_format = OT_PIXEL_FORMAT_ARGB_1555;
    ra.attr.overlay.bg_color = 0x0000;
    ra.attr.overlay.size.width = BOX_W;
    ra.attr.overlay.size.height = BOX_H;
    ra.attr.overlay.canvas_num = 2;
    if (ss_mpi_rgn_create(BOX_HANDLE, &ra) == TD_SUCCESS) {
        (td_void)memset_s(&ca, sizeof(ca), 0, sizeof(ca));
        ca.is_show = TD_TRUE;
        ca.type = OT_RGN_OVERLAY;
        ca.attr.overlay_chn.point.x = BOX_X;
        ca.attr.overlay_chn.point.y = BOX_Y;
        ca.attr.overlay_chn.fg_alpha = 255;
        ca.attr.overlay_chn.bg_alpha = 0;
        ca.attr.overlay_chn.layer = 1;
        chn.chn_id = 2; (td_void)ss_mpi_rgn_attach_to_chn(BOX_HANDLE, &chn, &ca);
        g_box_ready = 1;
    }
}

/* 画对焦条(黑底+白边框+按 frac 白填充), 让焦点值一眼可见峰化 */
static void cam_osd_bar(unsigned char *base, int stride, int x, int y, int w, int h, double frac)
{
    int r, c, fillw;
    if (frac < 0) { frac = 0; }
    if (frac > 1) { frac = 1; }
    fillw = (int)(frac * (w - 4));
    for (r = 0; r < h; r++) {
        unsigned short *p = (unsigned short *)(base + (y + r) * stride) + x;
        for (c = 0; c < w; c++) {
            int border = (r < 2 || r >= h - 2 || c < 2 || c >= w - 2);
            int fill = (c >= 2 && c < 2 + fillw && r >= 2 && r < h - 2);
            p[c] = (border || fill) ? 0xFFFF : 0x8000;   /* 边框/填充=白, 空槽=黑(半透明底可读) */
        }
    }
}

/* 刷 OSD: 第一行时间戳(g_osd_en), 第二行焦点值+对焦条(g_osd_focus_en). 黑影白字, 任意背景可读 */
static void cam_osd_update(void)
{
    ot_rgn_canvas_info ci;
    unsigned char *base;
    int stride;
    char buf[32];
    if (!g_osd_ready) { return; }
    if (ss_mpi_rgn_get_canvas_info(OSD_HANDLE, &ci) != TD_SUCCESS) { return; }
    base = (unsigned char *)ci.virt_addr;
    stride = (int)ci.stride;
    (td_void)memset_s(base, (size_t)stride * ci.size.height, 0, (size_t)stride * ci.size.height);
    if (g_osd_en) {
        time_t t = time(TD_NULL);
        struct tm lt;
        localtime_r(&t, &lt);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
        cam_osd_blit(base, stride, buf, 2, 2, 0x8000);
        cam_osd_blit(base, stride, buf, 0, 0, 0xFFFF);
    }
    if (g_osd_focus_en) {
        long long fv = cam_focus_value();
        double frac;
        if (fv < 0) { fv = 0; }
        if ((unsigned long long)fv > g_focus_peak) { g_focus_peak = (unsigned long long)fv; }
        snprintf(buf, sizeof(buf), "%lld", fv);
        cam_osd_blit(base, stride, buf, 2, OSD_L2_Y + 2, 0x8000);
        cam_osd_blit(base, stride, buf, 0, OSD_L2_Y, 0xFFFF);
        frac = (g_focus_peak > 0) ? (double)fv / (double)g_focus_peak : 0.0;
        cam_osd_bar(base, stride, 0, OSD_BAR_Y, OSD_W, OSD_BAR_H, frac);
    }
    (td_void)ss_mpi_rgn_update_canvas(OSD_HANDLE);

    /* 中心对焦框: 只在"点对焦 + 叠加开启"时显示; 否则清空 */
    if (g_box_ready && ss_mpi_rgn_get_canvas_info(BOX_HANDLE, &ci) == TD_SUCCESS) {
        base = (unsigned char *)ci.virt_addr;
        stride = (int)ci.stride;
        if (g_osd_focus_en && g_focus_spot) {
            cam_focusbox_draw(base, stride, BOX_W, BOX_H);
        } else {
            (td_void)memset_s(base, (size_t)stride * ci.size.height, 0, (size_t)stride * ci.size.height);
        }
        (td_void)ss_mpi_rgn_update_canvas(BOX_HANDLE);
    }
}

static void *cam_osd_thread(void *arg)
{
    (td_void)arg;
    while (1) {
        cam_osd_update();
        if (g_osd_focus_en) { usleep(250000); }   /* 对焦时 4Hz 刷, 便于看峰值 */
        else { sleep(1); }
    }
    return TD_NULL;
}

static int cam_hex(int c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    c |= 0x20;
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    return 0;
}

/* url-decode src[0..slen) into dst */
static void cam_url_decode(const char *src, int slen, char *dst, int cap)
{
    int i = 0, o = 0;
    for (i = 0; i < slen && o < cap - 1; i++) {
        char c = src[i];
        if (c == '+') { dst[o++] = ' '; }
        else if (c == '%' && i + 2 < slen) { dst[o++] = (char)((cam_hex(src[i + 1]) << 4) | cam_hex(src[i + 2])); i += 2; }
        else { dst[o++] = c; }
    }
    dst[o] = '\0';
}

/* extract url query param 'key' from request line into out (url-decoded) */
static int cam_qparam(const char *req, const char *key, char *out, int cap)
{
    char pat[40]; const char *p, *e;
    snprintf(pat, sizeof(pat), "%s=", key);
    p = strstr(req, pat);
    if (p == TD_NULL) { out[0] = '\0'; return 0; }
    p += strlen(pat);
    e = p;
    while (*e && *e != '&' && *e != ' ' && *e != '\r' && *e != '\n') { e++; }
    cam_url_decode(p, (int)(e - p), out, cap);
    return 1;
}

static int cam_check_cookie(const char *req)
{
    const char *c = strstr(req, "camtk="); int i, ok = 0;
    if (c == TD_NULL) { return 0; }
    c += 6;
    pthread_mutex_lock(&g_tok_lock);
    for (i = 0; i < CAM_TOK_N; i++) {
        int n = (int)strlen(g_tok[i]);
        if (n > 0 && !strncmp(c, g_tok[i], (size_t)n)) { ok = 1; break; }
    }
    pthread_mutex_unlock(&g_tok_lock);
    return ok;
}

static void cam_new_token(char *out, int cap)
{
    static const char set[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    static unsigned int seed = 0;
    struct timeval tv; int i;
    gettimeofday(&tv, TD_NULL);
    if (seed == 0) { seed = (unsigned int)(tv.tv_sec ^ tv.tv_usec); }
    for (i = 0; i < 16 && i < cap - 1; i++) { seed = seed * 1103515245u + 12345u; out[i] = set[(seed >> 16) % 62]; }
    out[i] = '\0';
    pthread_mutex_lock(&g_tok_lock);
    snprintf(g_tok[g_tok_slot % CAM_TOK_N], sizeof(g_tok[0]), "%s", out);
    g_tok_slot++;
    pthread_mutex_unlock(&g_tok_lock);
}

static void cam_http_body(int fd, const char *ctype, const char *body, int blen)
{
    char h[256];
    int n = snprintf(h, sizeof(h),
        "HTTP/1.0 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n",
        ctype, blen);
    if (write(fd, h, n) > 0 && blen > 0) { (void)write(fd, body, blen); }
}

static void cam_http_text(int fd, const char *body)
{
    cam_http_body(fd, "text/plain; charset=utf-8", body, (int)strlen(body));
}

static void cam_http_401(int fd)
{
    const char *b = "unauthorized";
    char h[128];
    int n = snprintf(h, sizeof(h), "HTTP/1.0 401 Unauthorized\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
        (int)strlen(b), b);
    (void)write(fd, h, n);
}

static long cam_read_long(const char *path)
{
    FILE *f = fopen(path, "r"); long v = -1;
    if (f == TD_NULL) { return -1; }
    if (fscanf(f, "%ld", &v) != 1) { v = -1; }
    fclose(f);
    return v;
}

static void cam_get_ip(char *out, int cap)
{
    static const char *ifs[3] = { "eth0", "end0", "eth1" };
    int s, i; struct ifreq ifr;
    out[0] = '\0';
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { return; }
    for (i = 0; i < 3; i++) {
        (td_void)memset_s(&ifr, sizeof(ifr), 0, sizeof(ifr));
        snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifs[i]);
        if (ioctl(s, SIOCGIFADDR, &ifr) == 0) {
            struct sockaddr_in *sa = (struct sockaddr_in *)&ifr.ifr_addr;
            snprintf(out, cap, "%s", inet_ntoa(sa->sin_addr));
            break;
        }
    }
    close(s);
}

/* 1 Hz sampler -> fps + kbit/s per stream */
static void *cam_stats_thread(void *arg)
{
    unsigned long pf[3] = {0, 0, 0}, pb[3] = {0, 0, 0};
    ot_unused(arg);
    while (g_sample_venc_exit == TD_FALSE) {
        int i;
        usleep(1000000);
        for (i = 0; i < 3; i++) {
            unsigned long f = g_enc_frames[i], b = g_enc_bytes[i];
            g_enc_fps[i]  = (double)(f - pf[i]);
            g_enc_kbps[i] = (double)(b - pb[i]) * 8.0 / 1000.0;
            pf[i] = f; pb[i] = b;
        }
    }
    return TD_NULL;
}

/* ISP/MMZ memory used (MB) parsed from /proc/umap/vb pool config; total = 600MB reserved */
static int cam_mmz_used_mb(void)
{
    FILE *p = popen("awk '/^size/{for(i=2;i<=NF;i++)s[i]=$i} /^count/{for(i=2;i<=NF;i++)t+=s[i]*$i} END{print int(t/1048576)}' /proc/umap/vb 2>/dev/null", "r");
    int mb = -1;
    if (p != TD_NULL) { if (fscanf(p, "%d", &mb) != 1) { mb = -1; } pclose(p); }
    return mb;
}

/* GPIO groups present on this board (gpiochip base/8): read all-8-bits via PL061 data@0x3FC over /dev/mem */
static const int g_gpio_groups[] = { 0, 2, 3, 4, 5, 6, 7, 8, 13, 14 };
#define GPIO_GRP_N ((int)(sizeof(g_gpio_groups) / sizeof(g_gpio_groups[0])))
#define GPIO_BASE 0x11090000u

static void cam_http_gpio(int fd)
{
    char body[512]; int n = 0, i;
    int mfd = open("/dev/mem", O_RDWR | O_SYNC);
    n += snprintf(body + n, sizeof(body) - n, "{");
    for (i = 0; i < GPIO_GRP_N; i++) {
        int g = g_gpio_groups[i], val = -1;
        if (mfd >= 0) {
            void *m = mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, GPIO_BASE + (unsigned)g * 0x1000u);
            if (m != MAP_FAILED) { val = (int)(*(volatile unsigned int *)((char *)m + 0x3FC) & 0xff); munmap(m, 0x1000); }
        }
        n += snprintf(body + n, sizeof(body) - n, "%s\"%d\":%d", i ? "," : "", g, val);
    }
    n += snprintf(body + n, sizeof(body) - n, "}");
    if (mfd >= 0) { close(mfd); }
    cam_http_body(fd, "application/json", body, n);
}

/* /gpioset?g=<grp>&p=<pin0-7>&v=<0|1> : drive one gpio as output (helps find IR-CUT/LED) */
static void cam_http_gpioset(int fd, const char *req)
{
    char gs[8], ps[8], vs[8]; int g = -1, p = -1, v = -1;
    if (cam_qparam(req, "g", gs, sizeof(gs))) { g = atoi(gs); }
    if (cam_qparam(req, "p", ps, sizeof(ps))) { p = atoi(ps); }
    if (cam_qparam(req, "v", vs, sizeof(vs))) { v = atoi(vs); }
    if (g < 0 || p < 0 || p > 7 || v < 0) { cam_http_text(fd, "bad args"); return; }
    int mfd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mfd >= 0) {
        void *m = mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, GPIO_BASE + (unsigned)g * 0x1000u);
        if (m != MAP_FAILED) {
            *(volatile unsigned int *)((char *)m + 0x400) |= (1u << p);        /* DIR = output */
            *(volatile unsigned int *)((char *)m + (((1u << p)) << 2)) = v ? (1u << p) : 0; /* masked data write */
            munmap(m, 0x1000);
        }
        close(mfd);
    }
    cam_http_text(fd, "ok");
}

/* web terminal: run a shell command (auth-gated), 10s timeout, cap 16KB output */
static void cam_http_exec(int fd, const char *req)
{
    char cmd[512] = "";
    cam_qparam(req, "cmd", cmd, sizeof(cmd));
    if (cmd[0] == '\0') { cam_http_text(fd, ""); return; }
    char full[600];
    snprintf(full, sizeof(full), "timeout 10 %s 2>&1", cmd);
    FILE *p = popen(full, "r");
    if (p == TD_NULL) { cam_http_text(fd, "exec failed"); return; }
    char *out = (char *)malloc(16384);
    int n = 0;
    if (out != TD_NULL) {
        size_t r;
        while (n < 16383 && (r = fread(out + n, 1, (size_t)(16383 - n), p)) > 0) { n += (int)r; }
        out[n] = '\0';
    }
    pclose(p);
    if (out == TD_NULL) { cam_http_text(fd, "oom"); return; }
    cam_http_body(fd, "text/plain; charset=utf-8", out, n);
    free(out);
}

static void cam_http_info(int fd)
{
    char body[1024], load[64] = "0.00 0.00 0.00", ip[32] = "", kern[96] = "", temps[16] = "null";
    long memtotal = 0, memavail = 0, tempm, freq;
    double uptime = 0;
    struct utsname un;
    FILE *f;

    f = fopen("/proc/meminfo", "r");
    if (f != TD_NULL) {
        char k[32], u[16]; long v;
        while (fscanf(f, "%31s %ld %15s", k, &v, u) == 3) {
            if (!strcmp(k, "MemTotal:")) { memtotal = v; }
            else if (!strcmp(k, "MemAvailable:")) { memavail = v; }
        }
        fclose(f);
    }
    f = fopen("/proc/uptime", "r");
    if (f != TD_NULL) { if (fscanf(f, "%lf", &uptime) != 1) { uptime = 0; } fclose(f); }
    f = fopen("/proc/loadavg", "r");
    if (f != TD_NULL) { char a[16], b[16], c[16]; if (fscanf(f, "%15s %15s %15s", a, b, c) == 3) { snprintf(load, sizeof(load), "%s %s %s", a, b, c); } fclose(f); }
    tempm = cam_read_long("/sys/class/thermal/thermal_zone0/temp");
    if (tempm >= 0) { snprintf(temps, sizeof(temps), "%.1f", tempm / 1000.0); }
    freq = cam_read_long("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    cam_get_ip(ip, sizeof(ip));
    if (uname(&un) == 0) { snprintf(kern, sizeof(kern), "%s", un.release); }

    int mmzmb = cam_mmz_used_mb();
    char mmzs[32];
    if (mmzmb >= 0) { snprintf(mmzs, sizeof(mmzs), "%d / 600 MB", mmzmb); } else { snprintf(mmzs, sizeof(mmzs), "600 MB"); }
    int n = snprintf(body, sizeof(body),
        "{\"temp\":%s,\"cpumhz\":%ld,\"memtotal\":%ld,\"memavail\":%ld,\"uptime\":%.0f,\"load\":\"%s\","
        "\"f0\":%.1f,\"k0\":%.1f,\"f1\":%.1f,\"k1\":%.1f,\"f2\":%.1f,\"k2\":%.1f,"
        "\"ip\":\"%s\",\"mmz\":\"%s\",\"kern\":\"%s\"}",
        temps, (freq > 0) ? freq / 1000 : 0, memtotal, memavail, uptime, load,
        g_enc_fps[0], g_enc_kbps[0], g_enc_fps[1], g_enc_kbps[1], g_enc_fps[2], g_enc_kbps[2],
        ip, mmzs, kern);
    cam_http_body(fd, "application/json", body, n);
}

static void cam_http_gettime(int fd)
{
    time_t t = time(TD_NULL);
    struct tm tm; char b[64], j[128];
    localtime_r(&t, &tm);
    strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tm);
    int n = snprintf(j, sizeof(j), "{\"t\":\"%s\",\"epoch\":%ld}", b, (long)t);
    cam_http_body(fd, "application/json", j, n);
}

static void cam_http_settime(int fd, const char *req)
{
    char e[24];
    if (cam_qparam(req, "epoch", e, sizeof(e)) && e[0]) {
        struct timeval tv;
        tv.tv_sec = (time_t)strtoll(e, TD_NULL, 10);
        tv.tv_usec = 0;
        if (settimeofday(&tv, TD_NULL) == 0) { cam_http_text(fd, "\xe6\x97\xb6\xe9\x97\xb4\xe5\xb7\xb2\xe6\x9b\xb4\xe6\x96\xb0"); }
        else { cam_http_text(fd, "\xe8\xae\xbe\xe7\xbd\xae\xe5\xa4\xb1\xe8\xb4\xa5\xef\xbc\x88\xe9\x9c\x80 root\xef\xbc\x89"); }
    } else {
        cam_http_text(fd, "bad epoch");
    }
}

static void cam_http_getrtsp(int fd)
{
    char j[96];
    pthread_mutex_lock(&g_cfg_lock);
    int n = snprintf(j, sizeof(j), "{\"en\":%d,\"user\":\"%s\"}", g_rtsp_auth_en, g_cam_user);
    pthread_mutex_unlock(&g_cfg_lock);
    cam_http_body(fd, "application/json", j, n);
}

static void cam_http_setrtsp(int fd, const char *req)
{
    char u[48], p[48], en[8];
    if (cam_qparam(req, "user", u, sizeof(u)) && u[0]) { pthread_mutex_lock(&g_cfg_lock); snprintf(g_cam_user, sizeof(g_cam_user), "%s", u); pthread_mutex_unlock(&g_cfg_lock); }
    if (cam_qparam(req, "pass", p, sizeof(p)) && p[0]) { pthread_mutex_lock(&g_cfg_lock); snprintf(g_cam_pass, sizeof(g_cam_pass), "%s", p); pthread_mutex_unlock(&g_cfg_lock); }
    if (cam_qparam(req, "en", en, sizeof(en))) { g_rtsp_auth_en = atoi(en); }
    cam_cfg_save();
    cam_http_text(fd, "\xe5\xb7\xb2\xe4\xbf\x9d\xe5\xad\x98\xe5\xb9\xb6\xe7\x94\x9f\xe6\x95\x88");
}

static void cam_http_getnet(int fd)
{
    char cur[24] = "", j[224]; int n;
    FILE *p = popen("ifconfig eth0 2>/dev/null | sed -n 's/.*inet addr:\\([0-9.]*\\).*/\\1/p'", "r");
    if (p != TD_NULL) {
        if (fgets(cur, sizeof(cur), p) != TD_NULL) { char *nl = strpbrk(cur, "\r\n"); if (nl != TD_NULL) { *nl = '\0'; } }
        pclose(p);
    }
    pthread_mutex_lock(&g_cfg_lock);
    n = snprintf(j, sizeof(j), "{\"cur\":\"%s\",\"ip\":\"%s\",\"mask\":\"%s\",\"gw\":\"%s\"}", cur, g_net_ip, g_net_mask, g_net_gw);
    pthread_mutex_unlock(&g_cfg_lock);
    cam_http_body(fd, "application/json", j, n);
}

static void cam_http_setnet(int fd, const char *req)
{
    char ip[20] = "", mask[20] = "", gw[20] = "";
    cam_qparam(req, "ip", ip, sizeof(ip));
    cam_qparam(req, "mask", mask, sizeof(mask));
    cam_qparam(req, "gw", gw, sizeof(gw));
    if (ip[0] != '\0') {
        pthread_mutex_lock(&g_cfg_lock);
        snprintf(g_net_ip, sizeof(g_net_ip), "%s", ip);
        if (mask[0] != '\0') { snprintf(g_net_mask, sizeof(g_net_mask), "%s", mask); }
        snprintf(g_net_gw, sizeof(g_net_gw), "%s", gw);
        pthread_mutex_unlock(&g_cfg_lock);
        cam_cfg_save();
        /* reply BEFORE changing the IP, else this very connection dies mid-response */
        cam_http_text(fd, "\xe5\xb7\xb2\xe4\xbf\x9d\xe5\xad\x98\xef\xbc\x8c\xe6\xad\xa3\xe5\x88\x87\xe6\x8d\xa2\xe2\x80\xa6\xe8\xaf\xb7\xe7\x94\xa8\xe6\x96\xb0 IP \xe6\x89\x93\xe5\xbc\x80");
        cam_net_apply();
    } else {
        cam_http_text(fd, "bad ip");
    }
}

static void cam_http_getscr(int fd)
{
    char j[160]; int n;
    pthread_mutex_lock(&g_cfg_lock);
    n = snprintf(j, sizeof(j), "{\"en\":%d,\"bright\":%d,\"src\":%d,\"lr\":%d,\"run\":%d}",
                 g_scr_en, g_scr_bright, g_scr_src, g_scr_lr, g_scr_running);
    pthread_mutex_unlock(&g_cfg_lock);
    cam_http_body(fd, "application/json", j, n);
}

static void cam_http_setscr(int fd, const char *req)
{
    char v[16];
    int en = g_scr_en, bright = g_scr_bright, src = g_scr_src, lr = g_scr_lr;
    if (cam_qparam(req, "en", v, sizeof(v)))     { en = atoi(v) ? 1 : 0; }
    if (cam_qparam(req, "bright", v, sizeof(v))) { bright = atoi(v); if (bright < 0) { bright = 0; } if (bright > 255) { bright = 255; } }
    if (cam_qparam(req, "src", v, sizeof(v)))    { src = atoi(v); if (src < 0 || src >= OT_VPSS_MAX_PHYS_CHN_NUM) { src = 2; } }
    if (cam_qparam(req, "lr", v, sizeof(v)))     { lr = atoi(v) ? 1 : 0; }
    pthread_mutex_lock(&g_cfg_lock);
    g_scr_en = en; g_scr_bright = bright; g_scr_src = src; g_scr_lr = lr;
    pthread_mutex_unlock(&g_cfg_lock);
    cam_cfg_save();
    /* 立即生效: 开->关 / 关->开 / 已开则至少更新背光(src/lr 改动下次开屏生效) */
    if (en && !g_scr_running)      { cam_scr_start(); }
    else if (!en && g_scr_running) { cam_scr_stop(); }
    else if (en && g_scr_running)  { scr_set_bright(bright); }
    cam_http_text(fd, "\xe5\xb7\xb2\xe4\xbf\x9d\xe5\xad\x98");  /* 已保存 */
}

static void cam_http_focus(int fd)
{
    char j[64]; int n;
    long long fv = cam_focus_value();
    n = snprintf(j, sizeof(j), "{\"fv\":%lld,\"ok\":%d}", fv, g_af_ok);
    cam_http_body(fd, "application/json", j, n);
}

static void cam_http_getosd(int fd)
{
    char j[80]; int n;
    n = snprintf(j, sizeof(j), "{\"en\":%d,\"ready\":%d,\"focus\":%d,\"spot\":%d}",
                 g_osd_en, g_osd_ready, g_osd_focus_en, g_focus_spot);
    cam_http_body(fd, "application/json", j, n);
}

static void cam_http_setosd(int fd, const char *req)
{
    char v[16];
    int changed = 0;
    if (cam_qparam(req, "en", v, sizeof(v))) {
        pthread_mutex_lock(&g_cfg_lock);
        g_osd_en = atoi(v) ? 1 : 0;
        pthread_mutex_unlock(&g_cfg_lock);
        changed = 1;
    }
    if (cam_qparam(req, "focus", v, sizeof(v))) {
        int nf = atoi(v) ? 1 : 0;
        if (nf && !g_osd_focus_en) { g_focus_peak = 0; }   /* 每次开对焦叠加时重置峰值 */
        pthread_mutex_lock(&g_cfg_lock);
        g_osd_focus_en = nf;
        pthread_mutex_unlock(&g_cfg_lock);
        changed = 1;
    }
    if (cam_qparam(req, "spot", v, sizeof(v))) {
        pthread_mutex_lock(&g_cfg_lock);
        g_focus_spot = atoi(v) ? 1 : 0;
        pthread_mutex_unlock(&g_cfg_lock);
        g_focus_peak = 0;   /* 切换测量区域, 峰值重置 */
        changed = 1;
    }
    if (changed) { cam_cfg_save(); cam_osd_update(); }   /* 立即生效 */
    cam_http_text(fd, "\xe5\xb7\xb2\xe4\xbf\x9d\xe5\xad\x98");  /* 已保存 */
}

static void cam_http_login(int fd, const char *req)
{
    char u[48], p[48]; int ok;
    cam_qparam(req, "user", u, sizeof(u));
    cam_qparam(req, "pass", p, sizeof(p));
    pthread_mutex_lock(&g_cfg_lock);
    ok = (!strcmp(u, g_cam_user) && !strcmp(p, g_cam_pass));
    pthread_mutex_unlock(&g_cfg_lock);
    if (ok) {
        char tk[24], h[256];
        cam_new_token(tk, sizeof(tk));
        int n = snprintf(h, sizeof(h),
            "HTTP/1.0 200 OK\r\nSet-Cookie: camtk=%s; Path=/; Max-Age=86400\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok", tk);
        (void)write(fd, h, n);
    } else {
        cam_http_401(fd);
    }
}

static void cam_http_logout(int fd)
{
    const char *h = "HTTP/1.0 200 OK\r\nSet-Cookie: camtk=; Path=/; Max-Age=0\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
    (void)write(fd, h, strlen(h));
}

/* ---- IMX678 register peek over i2c-3 @0x1a (16-bit reg addr, 8-bit data) ---- */
static int imx678_i2c_read(unsigned int reg)
{
    struct i2c_rdwr_ioctl_data xfer;
    struct i2c_msg msgs[2];
    unsigned char wbuf[2], rbuf[1] = { 0 };
    int fd, ret;
    fd = open("/dev/i2c-3", O_RDWR);
    if (fd < 0) { return -1; }
    wbuf[0] = (unsigned char)((reg >> 8) & 0xff);
    wbuf[1] = (unsigned char)(reg & 0xff);
    msgs[0].addr = 0x1a; msgs[0].flags = 0;        msgs[0].len = 2; msgs[0].buf = wbuf;
    msgs[1].addr = 0x1a; msgs[1].flags = I2C_M_RD; msgs[1].len = 1; msgs[1].buf = rbuf;
    xfer.msgs = msgs; xfer.nmsgs = 2;
    ret = ioctl(fd, I2C_RDWR, &xfer);
    close(fd);
    return (ret < 0) ? -1 : (int)rbuf[0];
}

/* curated key IMX678 registers -> JSON map { "3028":10, ... } (names/desc live in the web UI) */
static void cam_http_regs(int fd)
{
    static const unsigned int list[] = {
        0x3000, 0x3002, 0x3014,
        0x3028, 0x3029, 0x302a,   /* VMAX */
        0x302c, 0x302d,           /* HMAX */
        0x3050, 0x3051, 0x3052,   /* SHR0 */
        0x3070, 0x3071            /* GAIN */
    };
    char body[512];
    int n = 0, i, v;
    n += snprintf(body + n, sizeof(body) - n, "{");
    for (i = 0; i < (int)(sizeof(list) / sizeof(list[0])); i++) {
        v = imx678_i2c_read(list[i]);
        n += snprintf(body + n, sizeof(body) - n, "%s\"%04x\":%d", (i ? "," : ""), list[i], v);
    }
    n += snprintf(body + n, sizeof(body) - n, "}");
    cam_http_body(fd, "application/json", body, n);
}

/* read one arbitrary register: /reg?a=0x3070 -> {"a":"3070","v":10} */
static void cam_http_reg(int fd, const char *req)
{
    char a[16]; unsigned int reg = 0; int v, n;
    char body[64];
    if (cam_qparam(req, "a", a, sizeof(a))) { reg = (unsigned int)strtol(a, TD_NULL, 0); }
    v = imx678_i2c_read(reg);
    n = snprintf(body, sizeof(body), "{\"a\":\"%04x\",\"v\":%d}", reg & 0xffff, v);
    cam_http_body(fd, "application/json", body, n);
}

/* reader thread for the 4K JPEG snapshot channel (chn3) */
/* reader thread for chn3: keep a recent 4K JPEG in g_snap4k, throttled to ~2fps so it stays light */
static void *web_reader4k_thread(void *arg)
{
    ot_venc_chn chn = SNAP4K_CHN;
    ot_venc_chn_status st;
    ot_venc_stream stream;
    td_u32 i;
    ot_unused(arg);
    while (g_sample_venc_exit == TD_FALSE) {
        usleep(500000);   /* ~2fps: minimal cpu/mem, snapshot at most ~0.5s stale */
        if (ss_mpi_venc_query_status(chn, &st) != TD_SUCCESS || st.cur_packs == 0) { continue; }
        (td_void)memset_s(&stream, sizeof(stream), 0, sizeof(stream));
        stream.pack = (ot_venc_pack *)malloc(sizeof(ot_venc_pack) * st.cur_packs);
        if (stream.pack == TD_NULL) { continue; }
        stream.pack_cnt = st.cur_packs;
        if (ss_mpi_venc_get_stream(chn, &stream, 100) != TD_SUCCESS) { free(stream.pack); continue; }
        pthread_mutex_lock(&g_snap4k_lock);
        g_snap4k_len = 0;
        for (i = 0; i < stream.pack_cnt; i++) {
            td_u32 l = stream.pack[i].len - stream.pack[i].offset;
            if ((td_u32)g_snap4k_len + l <= sizeof(g_snap4k)) {
                (td_void)memcpy_s(g_snap4k + g_snap4k_len, sizeof(g_snap4k) - g_snap4k_len,
                    stream.pack[i].addr + stream.pack[i].offset, l);
                g_snap4k_len += (int)l;
            }
        }
        pthread_mutex_unlock(&g_snap4k_lock);
        ss_mpi_venc_release_stream(chn, &stream);
        free(stream.pack);
    }
    return TD_NULL;
}

/* serve newest 4K JPEG; as_download=1 forces a file download */
static void web_http_snap4k(int fd, int as_download)
{
    static unsigned char sbuf[4 * 1024 * 1024];
    char hdr[256];
    int len, hn;
    if (!g_snap4k_ok) {
        const char *e = "HTTP/1.0 503 Service Unavailable\r\nContent-Type: text/plain\r\nContent-Length: 18\r\nConnection: close\r\n\r\n4K channel offline";
        (void)write(fd, e, strlen(e));
        return;
    }
    pthread_mutex_lock(&g_snap4k_lock);
    len = g_snap4k_len;
    if (len > 0) { (td_void)memcpy_s(sbuf, sizeof(sbuf), g_snap4k, len); }
    pthread_mutex_unlock(&g_snap4k_lock);
    if (len <= 0) {
        const char *e = "HTTP/1.0 503 Service Unavailable\r\nContent-Type: text/plain\r\nContent-Length: 11\r\nConnection: close\r\n\r\nno 4K frame";
        (void)write(fd, e, strlen(e));
        return;
    }
    if (as_download) {
        hn = snprintf(hdr, sizeof(hdr),
            "HTTP/1.0 200 OK\r\nContent-Type: image/jpeg\r\nContent-Disposition: attachment; filename=\"imx678_4k.jpg\"\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", len);
    } else {
        hn = snprintf(hdr, sizeof(hdr),
            "HTTP/1.0 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", len);
    }
    if (write(fd, hdr, hn) > 0) { (void)write(fd, sbuf, len); }
}

/* reader thread: keep the newest MJPEG frame available */
static void *web_reader_thread(void *arg)
{
    ot_venc_chn chn = g_web_chn;
    ot_venc_chn_status st;
    ot_venc_stream stream;
    td_u32 i;
    ot_unused(arg);
    while (g_sample_venc_exit == TD_FALSE) {
        if (ss_mpi_venc_query_status(chn, &st) != TD_SUCCESS) { usleep(10000); continue; }
        if (st.cur_packs == 0) { usleep(5000); continue; }
        (td_void)memset_s(&stream, sizeof(stream), 0, sizeof(stream));
        stream.pack = (ot_venc_pack *)malloc(sizeof(ot_venc_pack) * st.cur_packs);
        if (stream.pack == TD_NULL) { usleep(5000); continue; }
        stream.pack_cnt = st.cur_packs;
        if (ss_mpi_venc_get_stream(chn, &stream, 100) != TD_SUCCESS) { free(stream.pack); usleep(5000); continue; }
        pthread_mutex_lock(&g_web_lock);
        g_web_jlen = 0;
        for (i = 0; i < stream.pack_cnt; i++) {
            td_u32 l = stream.pack[i].len - stream.pack[i].offset;
            if ((td_u32)g_web_jlen + l <= sizeof(g_web_jpeg)) {
                (td_void)memcpy_s(g_web_jpeg + g_web_jlen, sizeof(g_web_jpeg) - g_web_jlen,
                    stream.pack[i].addr + stream.pack[i].offset, l);
                g_web_jlen += (int)l;
            }
        }
        pthread_mutex_unlock(&g_web_lock);
        g_enc_frames[2]++; g_enc_bytes[2] += (unsigned long)g_web_jlen;
        ss_mpi_venc_release_stream(chn, &stream);
        free(stream.pack);
    }
    return TD_NULL;
}

/* continuous multipart MJPEG - smooth in-browser live view */
static void web_http_stream(int fd)
{
    const char *hdr =
        "HTTP/1.0 200 OK\r\nConnection: close\r\nCache-Control: no-cache\r\nPragma: no-cache\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frm\r\n\r\n";
    unsigned char *buf;
    char part[128];
    int len, n;
    if (write(fd, hdr, strlen(hdr)) <= 0) { return; }
    buf = (unsigned char *)malloc(sizeof(g_web_jpeg));
    if (buf == TD_NULL) { return; }
    while (g_sample_venc_exit == TD_FALSE) {
        pthread_mutex_lock(&g_web_lock);
        len = g_web_jlen;
        if (len > 0) { (td_void)memcpy_s(buf, sizeof(g_web_jpeg), g_web_jpeg, len); }
        pthread_mutex_unlock(&g_web_lock);
        if (len > 0) {
            n = snprintf(part, sizeof(part), "--frm\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n", len);
            if (write(fd, part, n) <= 0) { break; }
            if (write(fd, buf, len) <= 0) { break; }
            if (write(fd, "\r\n", 2) <= 0) { break; }
        }
        usleep(66000); /* ~15 fps */
    }
    free(buf);
}

static void web_http_snap(int fd)
{
    char hdr[160];
    int len, hn;
    static unsigned char sbuf[1024 * 1024];
    pthread_mutex_lock(&g_web_lock);
    len = g_web_jlen;
    if (len > 0) { (td_void)memcpy_s(sbuf, sizeof(sbuf), g_web_jpeg, len); }
    pthread_mutex_unlock(&g_web_lock);
    if (len <= 0) { return; }
    hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", len);
    if (write(fd, hdr, hn) > 0) { (void)write(fd, sbuf, len); }
}

/* apply brightness/contrast/saturation/hue via ISP CSC */
static void web_apply_csc(const char *q)
{
    ot_isp_csc_attr csc;
    char *p;
    int v;
    if (ot_mpi_isp_get_csc_attr(0, &csc) != TD_SUCCESS) { return; }
    csc.enable = TD_TRUE;
    if ((p = strstr(q, "luma=")) != TD_NULL) { v = atoi(p + 5); if (v >= 0 && v <= 100) { csc.luma = (td_u8)v; g_isp_luma = v; } }
    if ((p = strstr(q, "contr=")) != TD_NULL) { v = atoi(p + 6); if (v >= 0 && v <= 100) { csc.contr = (td_u8)v; g_isp_contr = v; } }
    if ((p = strstr(q, "satu=")) != TD_NULL) { v = atoi(p + 5); if (v >= 0 && v <= 100) { csc.satu = (td_u8)v; g_isp_satu = v; } }
    if ((p = strstr(q, "hue=")) != TD_NULL) { v = atoi(p + 4); if (v >= 0 && v <= 100) { csc.hue = (td_u8)v; g_isp_hue = v; } }
    ot_mpi_isp_set_csc_attr(0, &csc);
}

/* apply saved ISP CSC (startup, after cfg load) */
static void cam_isp_apply(void)
{
    ot_isp_csc_attr csc;
    if (ot_mpi_isp_get_csc_attr(0, &csc) != TD_SUCCESS) { return; }
    csc.enable = TD_TRUE;
    csc.luma = (td_u8)g_isp_luma; csc.contr = (td_u8)g_isp_contr;
    csc.satu = (td_u8)g_isp_satu; csc.hue = (td_u8)g_isp_hue;
    ot_mpi_isp_set_csc_attr(0, &csc);
}

/* save newest JPEG to SD (fallback /root) */
static void web_save_jpeg(char *out, int cap)
{
    char path[128];
    int len;
    FILE *f;
    snprintf(path, sizeof(path), "/mnt/sd/imx678_%03d.jpg", g_web_save_cnt);
    f = fopen(path, "wb");
    if (f == TD_NULL) {
        snprintf(path, sizeof(path), "/root/imx678_%03d.jpg", g_web_save_cnt);
        f = fopen(path, "wb");
    }
    if (f == TD_NULL) { snprintf(out, cap, "save failed"); return; }
    pthread_mutex_lock(&g_web_lock);
    len = g_web_jlen;
    if (len > 0) { fwrite(g_web_jpeg, 1, len, f); }
    pthread_mutex_unlock(&g_web_lock);
    fclose(f);
    g_web_save_cnt++;
    snprintf(out, cap, "saved %s", path);
}

/* drain any unread request bytes before close so the kernel sends FIN, not RST
 * (RST mid-response is what made the old page fail with "connection reset") */
static void web_close(int fd)
{
    char tmp[512];
    struct timeval tv;
    tv.tv_sec = 0; tv.tv_usec = 200000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    shutdown(fd, SHUT_WR);
    while (read(fd, tmp, sizeof(tmp)) > 0) { /* discard */ }
    close(fd);
}

static void *web_client_thread(void *arg)
{
    int fd = (int)(intptr_t)arg;
    char req[1024];
    struct timeval tv;
    int rn;
    tv.tv_sec = 5; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    rn = read(fd, req, sizeof(req) - 1);
    if (rn > 0) {
        req[rn] = '\0';
        int authed = cam_check_cookie(req);
        if (strstr(req, "GET /login") != TD_NULL) {
            cam_http_login(fd, req);
        } else if (strstr(req, "GET /logout") != TD_NULL) {
            cam_http_logout(fd);
        } else if (strstr(req, "GET /stream") != TD_NULL || strstr(req, "GET /snap") != TD_NULL ||
                   strstr(req, "GET /set") != TD_NULL || strstr(req, "GET /save") != TD_NULL ||
                   strstr(req, "GET /info") != TD_NULL || strstr(req, "GET /gettime") != TD_NULL ||
                   strstr(req, "GET /settime") != TD_NULL || strstr(req, "GET /getrtsp") != TD_NULL ||
                   strstr(req, "GET /setrtsp") != TD_NULL ||
                   strstr(req, "GET /reg") != TD_NULL || strstr(req, "GET /down4k") != TD_NULL ||
                   strstr(req, "GET /getnet") != TD_NULL || strstr(req, "GET /setnet") != TD_NULL ||
                   strstr(req, "GET /gpio") != TD_NULL || strstr(req, "GET /imgsave") != TD_NULL ||
                   strstr(req, "GET /getscr") != TD_NULL || strstr(req, "GET /setscr") != TD_NULL ||
                   strstr(req, "GET /focus") != TD_NULL || strstr(req, "GET /getosd") != TD_NULL ||
                   strstr(req, "GET /setosd") != TD_NULL ||
                   strstr(req, "GET /exec") != TD_NULL) {
            if (!authed) {
                cam_http_401(fd);
            } else if (strstr(req, "GET /snap4k") != TD_NULL) {
                web_http_snap4k(fd, 0);
            } else if (strstr(req, "GET /down4k") != TD_NULL) {
                web_http_snap4k(fd, 1);
            } else if (strstr(req, "GET /regs") != TD_NULL) {
                cam_http_regs(fd);
            } else if (strstr(req, "GET /reg") != TD_NULL) {
                cam_http_reg(fd, req);
            } else if (strstr(req, "GET /stream") != TD_NULL) {
                web_http_stream(fd);
            } else if (strstr(req, "GET /snap") != TD_NULL) {
                web_http_snap(fd);
            } else if (strstr(req, "GET /info") != TD_NULL) {
                cam_http_info(fd);
            } else if (strstr(req, "GET /gettime") != TD_NULL) {
                cam_http_gettime(fd);
            } else if (strstr(req, "GET /settime") != TD_NULL) {
                cam_http_settime(fd, req);
            } else if (strstr(req, "GET /getrtsp") != TD_NULL) {
                cam_http_getrtsp(fd);
            } else if (strstr(req, "GET /setrtsp") != TD_NULL) {
                cam_http_setrtsp(fd, req);
            } else if (strstr(req, "GET /getnet") != TD_NULL) {
                cam_http_getnet(fd);
            } else if (strstr(req, "GET /setnet") != TD_NULL) {
                cam_http_setnet(fd, req);
            } else if (strstr(req, "GET /gpioset") != TD_NULL) {
                cam_http_gpioset(fd, req);
            } else if (strstr(req, "GET /gpio") != TD_NULL) {
                cam_http_gpio(fd);
            } else if (strstr(req, "GET /exec") != TD_NULL) {
                cam_http_exec(fd, req);
            } else if (strstr(req, "GET /getscr") != TD_NULL) {
                cam_http_getscr(fd);
            } else if (strstr(req, "GET /setscr") != TD_NULL) {
                cam_http_setscr(fd, req);
            } else if (strstr(req, "GET /focus") != TD_NULL) {
                cam_http_focus(fd);
            } else if (strstr(req, "GET /getosd") != TD_NULL) {
                cam_http_getosd(fd);
            } else if (strstr(req, "GET /setosd") != TD_NULL) {
                cam_http_setosd(fd, req);
            } else if (strstr(req, "GET /imgsave") != TD_NULL) {
                cam_cfg_save();
                cam_http_text(fd, "\xe5\xb7\xb2\xe4\xbf\x9d\xe5\xad\x98");
            } else if (strstr(req, "GET /set") != TD_NULL) {
                char *qm = strchr(req, '?');
                const char *ok = "HTTP/1.0 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
                if (qm != TD_NULL) { web_apply_csc(qm); }
                write(fd, ok, strlen(ok));
            } else if (strstr(req, "GET /save") != TD_NULL) {
                char msg[160], resp[256];
                int n;
                web_save_jpeg(msg, sizeof(msg));
                n = snprintf(resp, sizeof(resp),
                    "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
                    (int)strlen(msg), msg);
                write(fd, resp, n);
            }
        } else {
            char hh[192];
            int hn = snprintf(hh, sizeof(hh),
                "HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                (int)strlen(g_web_html));
            write(fd, hh, hn);
            write(fd, g_web_html, strlen(g_web_html));
        }
    }
    web_close(fd);
    return TD_NULL;
}

/* sample_venc_web (unified) defined after RTSP section */

/* ================= IMX678 RTSP server (mode 14) =================
 * main = rtsp://<ip>/main  -> H.265 4K (venc chn0 <- vpss chn0)
 * sub  = rtsp://<ip>/sub   -> H.264 1080p (venc chn1 <- vpss chn1)
 * Minimal RTSP (OPTIONS/DESCRIBE/SETUP/PLAY/TEARDOWN) + RTP-over-UDP.
 * ================================================================ */
#include <pthread.h>
#include <stdint.h>
#include <errno.h>

#define RTSP_PORT 554
#define RTSP_MAX_SESS 8
#define RTP_MTU 1400
#define RTP_PT 96

typedef struct {
    volatile int in_use;
    volatile int playing;
    int stream_id;                 /* 0 = main (H265), 1 = sub (H264) */
    struct sockaddr_in rtp_addr;   /* client ip + client rtp port */
    unsigned short seq;
    unsigned int ssrc;
} rtsp_sess_t;

static rtsp_sess_t g_rtsp_sess[RTSP_MAX_SESS];
static pthread_mutex_t g_rtsp_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_rtp_fd = -1;                      /* shared UDP socket for sending RTP */
static ot_venc_chn g_rtsp_chn[2] = {0, 1};     /* venc chn for stream 0/1 */
static int g_rtsp_codec[2] = {1, 0};           /* 1 = H265, 0 = H264 */
static unsigned int g_rtsp_ts[2] = {0, 0};

/* captured parameter sets (base64) for SDP fmtp */
static char g_sprop_vps[2][300];
static char g_sprop_sps[2][300];
static char g_sprop_pps[2][300];

static const char g_b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void rtsp_b64enc(const unsigned char *in, int n, char *out)
{
    int i, o = 0;
    for (i = 0; i < n; i += 3) {
        int r = n - i;
        unsigned int v = (unsigned int)in[i] << 16;
        if (r > 1) { v |= (unsigned int)in[i + 1] << 8; }
        if (r > 2) { v |= (unsigned int)in[i + 2]; }
        out[o++] = g_b64[(v >> 18) & 63];
        out[o++] = g_b64[(v >> 12) & 63];
        out[o++] = (r > 1) ? g_b64[(v >> 6) & 63] : '=';
        out[o++] = (r > 2) ? g_b64[v & 63] : '=';
    }
    out[o] = '\0';
}

/* stash VPS/SPS/PPS when we see them so DESCRIBE can advertise sprop */
static void rtsp_capture_param(int sid, const unsigned char *nal, int len)
{
    if (len < 2 || len > 200) { return; }
    if (g_rtsp_codec[sid]) {                     /* H265 */
        int t = (nal[0] >> 1) & 0x3f;
        if (t == 32) { rtsp_b64enc(nal, len, g_sprop_vps[sid]); }
        else if (t == 33) { rtsp_b64enc(nal, len, g_sprop_sps[sid]); }
        else if (t == 34) { rtsp_b64enc(nal, len, g_sprop_pps[sid]); }
    } else {                                     /* H264 */
        int t = nal[0] & 0x1f;
        if (t == 7) { rtsp_b64enc(nal, len, g_sprop_sps[sid]); }
        else if (t == 8) { rtsp_b64enc(nal, len, g_sprop_pps[sid]); }
    }
}

/* send one RTP packet (payload already includes any FU headers) to all sessions of a stream */
static void rtsp_rtp_send(int sid, const unsigned char *payload, int plen, int marker, unsigned int ts)
{
    unsigned char buf[12 + RTP_MTU + 16];
    int i;
    if (plen <= 0 || plen > RTP_MTU + 8) { return; }
    pthread_mutex_lock(&g_rtsp_lock);
    for (i = 0; i < RTSP_MAX_SESS; i++) {
        rtsp_sess_t *s = &g_rtsp_sess[i];
        if (!s->in_use || !s->playing || s->stream_id != sid) { continue; }
        buf[0] = 0x80;
        buf[1] = (unsigned char)((marker ? 0x80 : 0) | RTP_PT);
        buf[2] = (unsigned char)((s->seq >> 8) & 0xff);
        buf[3] = (unsigned char)(s->seq & 0xff);
        s->seq++;
        buf[4] = (unsigned char)((ts >> 24) & 0xff);
        buf[5] = (unsigned char)((ts >> 16) & 0xff);
        buf[6] = (unsigned char)((ts >> 8) & 0xff);
        buf[7] = (unsigned char)(ts & 0xff);
        buf[8] = (unsigned char)((s->ssrc >> 24) & 0xff);
        buf[9] = (unsigned char)((s->ssrc >> 16) & 0xff);
        buf[10] = (unsigned char)((s->ssrc >> 8) & 0xff);
        buf[11] = (unsigned char)(s->ssrc & 0xff);
        memcpy(buf + 12, payload, plen);
        sendto(g_rtp_fd, buf, 12 + plen, 0, (struct sockaddr *)&s->rtp_addr, sizeof(s->rtp_addr));
    }
    pthread_mutex_unlock(&g_rtsp_lock);
}

/* packetize one NAL (no start code) into RTP: single or FU fragments */
static void rtsp_emit_nal(int sid, const unsigned char *nal, int len, int marker, unsigned int ts)
{
    unsigned char pkt[RTP_MTU + 8];
    if (len <= 0) { return; }
    rtsp_capture_param(sid, nal, len);
    if (len <= RTP_MTU) {
        rtsp_rtp_send(sid, nal, len, marker, ts);
        return;
    }
    if (g_rtsp_codec[sid]) {                     /* H265 FU (RFC 7798) */
        unsigned char b0 = nal[0], b1 = nal[1];
        int t = (b0 >> 1) & 0x3f;
        const unsigned char *p = nal + 2;
        int rem = len - 2, first = 1;
        while (rem > 0) {
            int chunk = (rem > (RTP_MTU - 3)) ? (RTP_MTU - 3) : rem;
            int last = (chunk == rem);
            pkt[0] = (unsigned char)((b0 & 0x81) | (49 << 1));
            pkt[1] = b1;
            pkt[2] = (unsigned char)((first ? 0x80 : 0) | (last ? 0x40 : 0) | t);
            memcpy(pkt + 3, p, chunk);
            rtsp_rtp_send(sid, pkt, chunk + 3, (last && marker) ? 1 : 0, ts);
            p += chunk; rem -= chunk; first = 0;
        }
    } else {                                     /* H264 FU-A (RFC 6184) */
        unsigned char nh = nal[0];
        int t = nh & 0x1f;
        const unsigned char *p = nal + 1;
        int rem = len - 1, first = 1;
        while (rem > 0) {
            int chunk = (rem > (RTP_MTU - 2)) ? (RTP_MTU - 2) : rem;
            int last = (chunk == rem);
            pkt[0] = (unsigned char)((nh & 0xe0) | 28);
            pkt[1] = (unsigned char)((first ? 0x80 : 0) | (last ? 0x40 : 0) | t);
            memcpy(pkt + 2, p, chunk);
            rtsp_rtp_send(sid, pkt, chunk + 2, (last && marker) ? 1 : 0, ts);
            p += chunk; rem -= chunk; first = 0;
        }
    }
}

/* split one venc pack (annex-b, may hold several NALs) and emit each */
static void rtsp_process_pack(int sid, const unsigned char *d, int dlen, int frame_end, unsigned int ts)
{
    int sc[64], cb[64], nc = 0, i = 0, k;
    if (dlen <= 0) { return; }
    while (i + 3 <= dlen) {
        if (d[i] == 0 && d[i + 1] == 0) {
            if (d[i + 2] == 1) { if (nc < 64) { sc[nc] = i; cb[nc] = i + 3; nc++; } i += 3; continue; }
            if (i + 3 < dlen && d[i + 2] == 0 && d[i + 3] == 1) { if (nc < 64) { sc[nc] = i; cb[nc] = i + 4; nc++; } i += 4; continue; }
        }
        i++;
    }
    if (nc == 0) { rtsp_emit_nal(sid, d, dlen, frame_end, ts); return; }
    for (k = 0; k < nc; k++) {
        int cs = cb[k];
        int ce = (k + 1 < nc) ? sc[k + 1] : dlen;
        int last_nal = (k == nc - 1) && frame_end;
        rtsp_emit_nal(sid, d + cs, ce - cs, last_nal, ts);
    }
}

/* per-stream: pull venc frames and RTP them out */
static void *rtsp_stream_thread(void *arg)
{
    int sid = (int)(intptr_t)arg;
    ot_venc_chn chn = g_rtsp_chn[sid];
    ot_venc_chn_status status;
    ot_venc_stream stream;
    unsigned int tick = 90000 / 25;              /* ~25 fps */

    while (g_sample_venc_exit == TD_FALSE) {
        td_u32 pi;
        if (ss_mpi_venc_query_status(chn, &status) != TD_SUCCESS) { usleep(20000); continue; }
        if (status.cur_packs == 0) { usleep(4000); continue; }
        (td_void)memset_s(&stream, sizeof(stream), 0, sizeof(stream));
        stream.pack = (ot_venc_pack *)malloc(sizeof(ot_venc_pack) * status.cur_packs);
        if (stream.pack == TD_NULL) { usleep(4000); continue; }
        stream.pack_cnt = status.cur_packs;
        if (ss_mpi_venc_get_stream(chn, &stream, 200) != TD_SUCCESS) { free(stream.pack); usleep(4000); continue; }
        g_rtsp_ts[sid] += tick;
        g_enc_frames[sid]++;
        for (pi = 0; pi < stream.pack_cnt; pi++) {
            ot_venc_pack *pk = &stream.pack[pi];
            g_enc_bytes[sid] += (unsigned long)(pk->len - pk->offset);
            rtsp_process_pack(sid, pk->addr + pk->offset, (int)(pk->len - pk->offset),
                (int)pk->is_frame_end, g_rtsp_ts[sid]);
        }
        ss_mpi_venc_release_stream(chn, &stream);
        free(stream.pack);
    }
    return TD_NULL;
}

static int rtsp_alloc_sess(void)
{
    int i;
    pthread_mutex_lock(&g_rtsp_lock);
    for (i = 0; i < RTSP_MAX_SESS; i++) {
        if (!g_rtsp_sess[i].in_use) {
            (td_void)memset_s(&g_rtsp_sess[i], sizeof(rtsp_sess_t), 0, sizeof(rtsp_sess_t));
            g_rtsp_sess[i].in_use = 1;
            g_rtsp_sess[i].ssrc = 0x1a2b3c00u + (unsigned int)i;
            pthread_mutex_unlock(&g_rtsp_lock);
            return i;
        }
    }
    pthread_mutex_unlock(&g_rtsp_lock);
    return -1;
}

static void rtsp_free_sess(int idx)
{
    if (idx < 0) { return; }
    pthread_mutex_lock(&g_rtsp_lock);
    g_rtsp_sess[idx].in_use = 0;
    g_rtsp_sess[idx].playing = 0;
    pthread_mutex_unlock(&g_rtsp_lock);
}

static void rtsp_build_sdp(int sid, char *sdp, int cap)
{
    char media[1200];
    int n;
    if (g_rtsp_codec[sid]) {
        n = snprintf(media, sizeof(media), "m=video 0 RTP/AVP 96\r\na=rtpmap:96 H265/90000\r\n");
        if (g_sprop_vps[sid][0] && g_sprop_sps[sid][0] && g_sprop_pps[sid][0]) {
            n += snprintf(media + n, sizeof(media) - n,
                "a=fmtp:96 sprop-vps=%s; sprop-sps=%s; sprop-pps=%s\r\n",
                g_sprop_vps[sid], g_sprop_sps[sid], g_sprop_pps[sid]);
        }
        snprintf(media + n, sizeof(media) - n, "a=control:track0\r\n");
    } else {
        n = snprintf(media, sizeof(media), "m=video 0 RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\n");
        if (g_sprop_sps[sid][0] && g_sprop_pps[sid][0]) {
            n += snprintf(media + n, sizeof(media) - n,
                "a=fmtp:96 packetization-mode=1; sprop-parameter-sets=%s,%s\r\n",
                g_sprop_sps[sid], g_sprop_pps[sid]);
        }
        snprintf(media + n, sizeof(media) - n, "a=control:track0\r\n");
    }
    snprintf(sdp, cap,
        "v=0\r\no=- 0 0 IN IP4 0.0.0.0\r\ns=IMX678\r\nc=IN IP4 0.0.0.0\r\nt=0 0\r\na=control:*\r\n%s",
        media);
}

static void *rtsp_client_thread(void *arg)
{
    int fd = (int)(intptr_t)arg;
    char req[2048], resp[3072];
    int sid = 0, sess_idx = -1;
    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    struct timeval tv;

    tv.tv_sec = 15; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    getpeername(fd, (struct sockaddr *)&peer, &plen);

    for (;;) {
        int rn = read(fd, req, sizeof(req) - 1);
        int cseq = 0;
        char *cs;
        if (rn <= 0) { break; }
        req[rn] = '\0';
        cs = strstr(req, "CSeq:");
        if (cs == TD_NULL) { cs = strstr(req, "Cseq:"); }
        if (cs != TD_NULL) { cseq = atoi(cs + 5); }
        if (strstr(req, "/sub") != TD_NULL) { sid = 1; }
        else if (strstr(req, "/main") != TD_NULL) { sid = 0; }

        if (g_rtsp_auth_en && (!strncmp(req, "DESCRIBE", 8) || !strncmp(req, "SETUP", 5) || !strncmp(req, "PLAY", 4))) {
            char up[80], b64[160], *a;
            pthread_mutex_lock(&g_cfg_lock);
            snprintf(up, sizeof(up), "%s:%s", g_cam_user, g_cam_pass);
            pthread_mutex_unlock(&g_cfg_lock);
            rtsp_b64enc((const unsigned char *)up, (int)strlen(up), b64);
            a = strstr(req, "Basic ");
            if (a == TD_NULL || strncmp(a + 6, b64, strlen(b64)) != 0) {
                snprintf(resp, sizeof(resp),
                    "RTSP/1.0 401 Unauthorized\r\nCSeq: %d\r\nWWW-Authenticate: Basic realm=\"IMX678\"\r\n\r\n", cseq);
                write(fd, resp, strlen(resp));
                continue;
            }
        }

        if (!strncmp(req, "OPTIONS", 7)) {
            snprintf(resp, sizeof(resp),
                "RTSP/1.0 200 OK\r\nCSeq: %d\r\nPublic: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n\r\n", cseq);
            write(fd, resp, strlen(resp));
        } else if (!strncmp(req, "DESCRIBE", 8)) {
            char sdp[2048];
            rtsp_build_sdp(sid, sdp, sizeof(sdp));
            snprintf(resp, sizeof(resp),
                "RTSP/1.0 200 OK\r\nCSeq: %d\r\nContent-Type: application/sdp\r\nContent-Length: %d\r\n\r\n%s",
                cseq, (int)strlen(sdp), sdp);
            write(fd, resp, strlen(resp));
        } else if (!strncmp(req, "SETUP", 5)) {
            int cport = 0;
            char *tp = strstr(req, "client_port=");
            if (tp != TD_NULL) { cport = atoi(tp + 12); }
            if (sess_idx < 0) { sess_idx = rtsp_alloc_sess(); }
            if (sess_idx >= 0 && cport > 0) {
                pthread_mutex_lock(&g_rtsp_lock);
                g_rtsp_sess[sess_idx].stream_id = sid;
                g_rtsp_sess[sess_idx].rtp_addr = peer;
                g_rtsp_sess[sess_idx].rtp_addr.sin_port = htons((unsigned short)cport);
                pthread_mutex_unlock(&g_rtsp_lock);
            }
            snprintf(resp, sizeof(resp),
                "RTSP/1.0 200 OK\r\nCSeq: %d\r\nTransport: RTP/AVP;unicast;client_port=%d-%d;server_port=40000-40001;ssrc=%08X\r\nSession: 12345678\r\n\r\n",
                cseq, cport, cport + 1, (sess_idx >= 0) ? g_rtsp_sess[sess_idx].ssrc : 0);
            write(fd, resp, strlen(resp));
        } else if (!strncmp(req, "PLAY", 4)) {
            if (sess_idx >= 0) {
                pthread_mutex_lock(&g_rtsp_lock);
                g_rtsp_sess[sess_idx].playing = 1;
                pthread_mutex_unlock(&g_rtsp_lock);
            }
            ss_mpi_venc_request_idr(g_rtsp_chn[sid], TD_TRUE);
            snprintf(resp, sizeof(resp),
                "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: 12345678\r\nRTP-Info: url=track0;seq=0;rtptime=0\r\n\r\n", cseq);
            write(fd, resp, strlen(resp));
        } else if (!strncmp(req, "TEARDOWN", 8)) {
            snprintf(resp, sizeof(resp), "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: 12345678\r\n\r\n", cseq);
            write(fd, resp, strlen(resp));
            break;
        } else {
            snprintf(resp, sizeof(resp), "RTSP/1.0 200 OK\r\nCSeq: %d\r\n\r\n", cseq);
            write(fd, resp, strlen(resp));
        }
    }
    rtsp_free_sess(sess_idx);
    close(fd);
    return TD_NULL;
}

/* start one venc chn: type/size, bind to vpss chn, begin receiving */
static td_s32 rtsp_start_chn(ot_vpss_grp vpss_grp, int vpss_chn, ot_venc_chn venc_chn,
    ot_payload_type type, ot_size *sz, td_u32 pic_enum)
{
    td_s32 ret;
    sample_comm_venc_chn_param chn_param = {0};
    ot_venc_start_param recv_param = {0};

    chn_param.frame_rate = 25;
    chn_param.stats_time = 2;
    chn_param.gop = 50;
    chn_param.venc_size.width = sz->width;
    chn_param.venc_size.height = sz->height;
    chn_param.size = pic_enum;
    chn_param.profile = 0;
    chn_param.is_rcn_ref_share_buf = TD_FALSE;
    chn_param.type = type;
    chn_param.rc_mode = SAMPLE_RC_CBR;
    if ((ret = sample_comm_venc_start(venc_chn, &chn_param)) != TD_SUCCESS) { return ret; }
    if ((ret = sample_comm_vpss_bind_venc(vpss_grp, vpss_chn, venc_chn)) != TD_SUCCESS) { return ret; }
    recv_param.recv_pic_num = -1;
    ss_mpi_venc_start_chn(venc_chn, &recv_param);
    return TD_SUCCESS;
}

static td_s32 sample_venc_rtsp(td_void)
{
    td_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg;
    ot_size enc_size[CHN_NUM_MAX], vi_size;
    const ot_vi_pipe vi_pipe = 0;
    const ot_vi_chn vi_chn = 0;
    const ot_vpss_grp vpss_grp = 0;
    sample_venc_vpss_chn_attr vpss_param;
    sample_venc_vb_attr vb_attr = {0};
    int listen_fd, opt = 1;
    struct sockaddr_in addr;
    struct timeval tv;
    pthread_t th_main, th_sub;

    signal(SIGPIPE, SIG_IGN);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    sample_comm_vi_get_size_by_sns_type(sns_type, &vi_size);
    ret = sample_venc_init_param(enc_size, CHN_NUM_MAX, &vi_size, &vpss_param);
    if (ret != TD_SUCCESS) { return ret; }
    get_vb_attr(&vi_size, &vpss_param, &vb_attr);
    if ((ret = sample_venc_sys_init(&vb_attr)) != TD_SUCCESS) { return ret; }
    if ((ret = sample_venc_vi_init(&vi_cfg)) != TD_SUCCESS) { goto EXIT_SYS; }
    if ((ret = sample_venc_vpss_init(vpss_grp, &vpss_param)) != TD_SUCCESS) { goto EXIT_VI; }
    if ((ret = sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0)) != TD_SUCCESS) { goto EXIT_VPSS; }

    /* main: H265 4K on vpss chn0 / venc chn0 */
    if ((ret = rtsp_start_chn(vpss_grp, 0, 0, OT_PT_H265, &enc_size[0], PIC_3840X2160)) != TD_SUCCESS) {
        printf("start main(H265 4K) failed %#x\n", ret); goto EXIT_UNBIND;
    }
    /* sub: H264 1080p on vpss chn1 / venc chn1 */
    if ((ret = rtsp_start_chn(vpss_grp, 1, 1, OT_PT_H264, &enc_size[1], PIC_1080P)) != TD_SUCCESS) {
        printf("start sub(H264 1080p) failed %#x\n", ret); goto EXIT_MAIN;
    }

    g_rtp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    (td_void)memset_s(&addr, sizeof(addr), 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(40000);
    if (bind(g_rtp_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { /* fall back to ephemeral */
        addr.sin_port = 0;
        bind(g_rtp_fd, (struct sockaddr *)&addr, sizeof(addr));
    }

    pthread_create(&th_main, TD_NULL, rtsp_stream_thread, (void *)(intptr_t)0);
    pthread_create(&th_sub, TD_NULL, rtsp_stream_thread, (void *)(intptr_t)1);
    ss_mpi_venc_request_idr(0, TD_TRUE);
    ss_mpi_venc_request_idr(1, TD_TRUE);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    (td_void)memset_s(&addr, sizeof(addr), 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(RTSP_PORT);
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { printf("bind 554 failed\n"); goto EXIT_RTP; }
    listen(listen_fd, 8);
    tv.tv_sec = 1; tv.tv_usec = 0;
    setsockopt(listen_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    printf("=== IMX678 RTSP: rtsp://<board-ip>/main (H265 4K)  rtsp://<board-ip>/sub (H264 1080p) ===\n");

    while (g_sample_venc_exit == TD_FALSE) {
        int cli_fd = accept(listen_fd, TD_NULL, TD_NULL);
        pthread_t ct;
        if (cli_fd < 0) { continue; }            /* timeout -> re-check exit flag */
        if (pthread_create(&ct, TD_NULL, rtsp_client_thread, (void *)(intptr_t)cli_fd) == 0) {
            pthread_detach(ct);
        } else {
            close(cli_fd);
        }
    }
    close(listen_fd);
EXIT_RTP:
    if (g_rtp_fd >= 0) { close(g_rtp_fd); g_rtp_fd = -1; }
    ss_mpi_venc_stop_chn(1);
    sample_comm_vpss_un_bind_venc(vpss_grp, 1, 1);
    sample_comm_venc_stop(1);
EXIT_MAIN:
    ss_mpi_venc_stop_chn(0);
    sample_comm_vpss_un_bind_venc(vpss_grp, 0, 0);
    sample_comm_venc_stop(0);
EXIT_UNBIND:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0);
EXIT_VPSS:
    sample_venc_vpss_deinit(vpss_grp, &vpss_param);
EXIT_VI:
    sample_venc_vi_deinit(&vi_cfg);
EXIT_SYS:
    sample_comm_sys_exit();
    return ret;
}

static void *rtsp_listen_thread(void *arg)
{
    int listen_fd, opt = 1;
    struct sockaddr_in addr;
    struct timeval tv;
    ot_unused(arg);
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    (td_void)memset_s(&addr, sizeof(addr), 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(RTSP_PORT);
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { printf("bind 554 failed\n"); close(listen_fd); return TD_NULL; }
    listen(listen_fd, 8);
    tv.tv_sec = 1; tv.tv_usec = 0;
    setsockopt(listen_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    while (g_sample_venc_exit == TD_FALSE) {
        pthread_t ct;
        int cli = accept(listen_fd, TD_NULL, TD_NULL);
        if (cli < 0) { continue; }
        if (pthread_create(&ct, TD_NULL, rtsp_client_thread, (void *)(intptr_t)cli) == 0) { pthread_detach(ct); }
        else { close(cli); }
    }
    close(listen_fd);
    return TD_NULL;
}

/* unified: VI -> VPSS(3 chn) -> H265 4K + H264 1080p (RTSP :554) + MJPEG 720p preview (web :8080) */
static td_s32 sample_venc_web(td_void)
{
    td_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg;
    ot_size enc_size[CHN_NUM_MAX], vi_size, pv_size;
    const ot_vi_pipe vi_pipe = 0;
    const ot_vi_chn vi_chn = 0;
    const ot_vpss_grp vpss_grp = 0;
    sample_venc_vpss_chn_attr vpss_param;
    sample_venc_vb_attr vb_attr = {0};
    sample_comm_venc_chn_param mjpeg_param = {0};
    ot_venc_start_param recv_param = {0};
    int listen_fd, opt = 1;
    struct sockaddr_in addr;
    struct timeval tv;
    pthread_t reader, stats, th_main, th_sub, rtsp_th;

    signal(SIGPIPE, SIG_IGN);
    cam_param_mount();
    cam_cfg_load();
    cam_net_apply();
    cam_start_sshd();
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    sample_comm_vi_get_size_by_sns_type(sns_type, &vi_size);
    ret = sample_venc_init_param(enc_size, CHN_NUM_MAX, &vi_size, &vpss_param);
    if (ret != TD_SUCCESS) { return ret; }

    /* add 3rd vpss channel (chn2) for the small MJPEG web preview (720p) */
    if (sample_comm_sys_get_pic_size(PIC_720P, &pv_size) != TD_SUCCESS) { pv_size.width = 1280; pv_size.height = 720; }
    vpss_param.output_size[2].width  = pv_size.width;
    vpss_param.output_size[2].height = pv_size.height;
    vpss_param.compress_mode[2] = OT_COMPRESS_MODE_NONE;
    vpss_param.enable[2] = TD_TRUE;
    vpss_param.fmu_mode[2] = OT_FMU_MODE_OFF;

    /* add 4th vpss channel (chn3) @ full 4K, feeds the JPEG snapshot encoder (/snap4k, /down4k) */
    vpss_param.output_size[3].width  = vi_size.width;
    vpss_param.output_size[3].height = vi_size.height;
    vpss_param.compress_mode[3] = OT_COMPRESS_MODE_NONE;
    vpss_param.enable[3] = TD_TRUE;
    vpss_param.fmu_mode[3] = OT_FMU_MODE_OFF;

    get_vb_attr(&vi_size, &vpss_param, &vb_attr);
    if ((ret = sample_venc_sys_init(&vb_attr)) != TD_SUCCESS) { return ret; }
    if ((ret = sample_venc_vi_init(&vi_cfg)) != TD_SUCCESS) { goto EXIT_SYS; }
    if ((ret = sample_venc_vpss_init(vpss_grp, &vpss_param)) != TD_SUCCESS) { goto EXIT_VI; }
    if ((ret = sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0)) != TD_SUCCESS) { goto EXIT_VPSS; }

    /* main: H265 4K (vpss chn0 -> venc chn0) */
    if ((ret = rtsp_start_chn(vpss_grp, 0, 0, OT_PT_H265, &enc_size[0], PIC_3840X2160)) != TD_SUCCESS) {
        printf("start main(H265 4K) failed %#x\n", ret); goto EXIT_UNBIND;
    }
    /* sub: H264 1080p (vpss chn1 -> venc chn1) */
    if ((ret = rtsp_start_chn(vpss_grp, 1, 1, OT_PT_H264, &enc_size[1], PIC_1080P)) != TD_SUCCESS) {
        printf("start sub(H264 1080p) failed %#x\n", ret); goto EXIT_MAIN;
    }
    /* preview: MJPEG 720p (vpss chn2 -> venc chn2) */
    mjpeg_param.frame_rate = 25;
    mjpeg_param.stats_time = 2;
    mjpeg_param.gop = 1;
    mjpeg_param.venc_size.width  = pv_size.width;
    mjpeg_param.venc_size.height = pv_size.height;
    mjpeg_param.size = PIC_720P;
    mjpeg_param.profile = 0;
    mjpeg_param.is_rcn_ref_share_buf = TD_FALSE;
    mjpeg_param.type = OT_PT_MJPEG;
    mjpeg_param.rc_mode = SAMPLE_RC_CBR;
    if ((ret = sample_comm_venc_start(2, &mjpeg_param)) != TD_SUCCESS) { printf("start preview(MJPEG) failed %#x\n", ret); goto EXIT_SUB; }
    if ((ret = sample_comm_vpss_bind_venc(vpss_grp, 2, 2)) != TD_SUCCESS) { goto EXIT_PVENC; }
    recv_param.recv_pic_num = -1;
    ss_mpi_venc_start_chn(2, &recv_param);
    g_web_chn = 2;

    /* snapshot4k: MJPEG 4K (vpss chn3 -> venc chn3), low fps. NON-FATAL: on failure just disable /snap4k */
    {
        sample_comm_venc_chn_param j4k = {0};
        j4k.frame_rate = 2;
        j4k.stats_time = 2;
        j4k.gop = 1;
        j4k.venc_size.width  = vi_size.width;
        j4k.venc_size.height = vi_size.height;
        j4k.size = PIC_3840X2160;
        j4k.profile = 0;
        j4k.is_rcn_ref_share_buf = TD_FALSE;
        j4k.type = OT_PT_MJPEG;
        j4k.rc_mode = SAMPLE_RC_CBR;
        if (sample_comm_venc_start(SNAP4K_CHN, &j4k) == TD_SUCCESS &&
            sample_comm_vpss_bind_venc(vpss_grp, 3, SNAP4K_CHN) == TD_SUCCESS) {
            ot_venc_start_param rp4 = {0};
            rp4.recv_pic_num = -1;
            ss_mpi_venc_start_chn(SNAP4K_CHN, &rp4);
            g_snap4k_ok = 1;
            printf("[ok] 4K snapshot chn3 up @2fps (/snap4k /down4k)\n");
        } else {
            printf("[warn] 4K snapshot chn3 init failed -> /snap4k disabled (preview/rtsp unaffected)\n");
        }
    }

    /* shared UDP socket for RTP */
    g_rtp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    (td_void)memset_s(&addr, sizeof(addr), 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(40000);
    if (bind(g_rtp_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { addr.sin_port = 0; bind(g_rtp_fd, (struct sockaddr *)&addr, sizeof(addr)); }

    pthread_create(&reader,  TD_NULL, web_reader_thread, TD_NULL);
    if (g_snap4k_ok) { pthread_t r4k; pthread_create(&r4k, TD_NULL, web_reader4k_thread, TD_NULL); }
    pthread_create(&stats,   TD_NULL, cam_stats_thread,  TD_NULL);
    cam_isp_apply();   /* re-apply saved ISP brightness/contrast/etc now that ISP is running */
    pthread_create(&th_main, TD_NULL, rtsp_stream_thread, (void *)(intptr_t)0);
    pthread_create(&th_sub,  TD_NULL, rtsp_stream_thread, (void *)(intptr_t)1);
    ss_mpi_venc_request_idr(0, TD_TRUE);
    ss_mpi_venc_request_idr(1, TD_TRUE);
    pthread_create(&rtsp_th, TD_NULL, rtsp_listen_thread, TD_NULL);

    /* 对焦助手: 启用 ISP AF 焦点统计(非致命). OSD: 建 region + 起每秒刷新线程. */
    cam_af_enable();
    cam_osd_init();
    { pthread_t osd_th; pthread_create(&osd_th, TD_NULL, cam_osd_thread, TD_NULL); }

    /* 若已保存"启用屏幕", 现在(通道都在产帧)开屏. 默认关闭 -> 通常跳过. 非致命. */
    if (g_scr_en) { cam_scr_start(); }

    /* web server :8080 in this thread */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    (td_void)memset_s(&addr, sizeof(addr), 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(8080);
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { printf("bind 8080 failed\n"); goto EXIT_ALL; }
    listen(listen_fd, 8);
    tv.tv_sec = 1; tv.tv_usec = 0;
    setsockopt(listen_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    printf("=== TianYuan IMX678 cam: web http://<ip>:8080/  rtsp://<ip>/main(H265 4K) /sub(H264 1080p) ===\n");

    while (g_sample_venc_exit == TD_FALSE) {
        pthread_t ct;
        int cli_fd = accept(listen_fd, TD_NULL, TD_NULL);
        if (cli_fd < 0) { continue; }
        if (pthread_create(&ct, TD_NULL, web_client_thread, (void *)(intptr_t)cli_fd) == 0) { pthread_detach(ct); }
        else { close(cli_fd); }
    }
    close(listen_fd);
EXIT_ALL:
    if (g_rtp_fd >= 0) { close(g_rtp_fd); g_rtp_fd = -1; }
    ss_mpi_venc_stop_chn(2);
    sample_comm_vpss_un_bind_venc(vpss_grp, 2, 2);
EXIT_PVENC:
    sample_comm_venc_stop(2);
EXIT_SUB:
    ss_mpi_venc_stop_chn(1);
    sample_comm_vpss_un_bind_venc(vpss_grp, 1, 1);
    sample_comm_venc_stop(1);
EXIT_MAIN:
    ss_mpi_venc_stop_chn(0);
    sample_comm_vpss_un_bind_venc(vpss_grp, 0, 0);
    sample_comm_venc_stop(0);
EXIT_UNBIND:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp, 0);
EXIT_VPSS:
    sample_venc_vpss_deinit(vpss_grp, &vpss_param);
EXIT_VI:
    sample_venc_vi_deinit(&vi_cfg);
EXIT_SYS:
    sample_comm_sys_exit();
    return ret;
}

static td_s32 sample_venc_choose_mode(td_u32 index)
{
    td_s32 ret;

    switch (index) {
        case 0: /* 0: mode 0 */
            ret = sample_venc_normal();
            break;

        case 1: /* 1: mode 1 */
            ret = sample_venc_qpmap();
            break;

        case 2: /* 2: mode 2 */
            ret = sample_venc_intra_refresh();
            break;

        case 3: /* 3: mode 3 */
            ret = sample_venc_roi_bg();
            break;

        case 4: /* 4: mode 4 */
            ret = sample_venc_debreath_effect();
            break;

        case 5: /* 5: mode 5 */
            ret = sample_venc_mjpeg_roi_set();
            break;

        case 6: /* 6: mode 6 */
            ret = sample_venc_mjpeg_roimap();
            break;

        case 7: /* 7: mode 7 */
            ret = sample_venc_mosaic_map();
            break;

        case 8: /* 8: mode 8 */
            ret = sample_venc_mjpeg_mosaic_map();
            break;

        case 9: /* 9: mode 9 */
            ret = sample_venc_watermark();
            break;

        case 10: /* 10: mode 10 */
            ret = sample_venc_pvc();
            break;

        case 11: /* 11: mode 11 */
            ret = sample_venc_fmu_one_grp();
            break;

        case 12: /* 12: mode 12 */
            ret = sample_venc_fmu_two_grp();
            break;

        case 13:
            ret = sample_venc_web();
            break;
        case 14:
            ret = sample_venc_rtsp();
            break;
        default:
            printf("the index is invalid!\n");
            return TD_FAILURE;
    }
    return ret;
}

static td_s32 sample_venc_msg_proc_vb_pool_share(td_s32 pid)
{
    td_u32 i;
    ot_vb_common_pools_id pools_id = {0};

    if (ss_mpi_vb_get_common_pool_id(&pools_id) != TD_SUCCESS) {
        sample_print("get common pool_id failed!\n");
        return TD_FAILURE;
    }
    for (i = 0; i < pools_id.pool_cnt; ++i) {
        if (ss_mpi_vb_pool_share(pools_id.pool[i], pid) != TD_SUCCESS) {
            sample_print("vb pool share failed!\n");
            return TD_FAILURE;
        }
    }

    return TD_SUCCESS;
}

static td_void sample_venc_msg_proc_vb_pool_unshare(td_s32 pid)
{
    td_u32 i;
    ot_vb_common_pools_id pools_id = {0};

    if (ss_mpi_vb_get_common_pool_id(&pools_id) == TD_SUCCESS) {
        for (i = 0; i < pools_id.pool_cnt; ++i) {
            ss_mpi_vb_pool_unshare(pools_id.pool[i], pid);
        }
    }
}

static td_s32 sample_venc_ipc_msg_proc(const sample_ipc_msg_req_buf *msg_req_buf,
    td_bool *is_need_fb, sample_ipc_msg_res_buf *msg_res_buf)
{
    td_s32 ret;

    if (msg_req_buf == TD_NULL || is_need_fb == TD_NULL) {
        return TD_FAILURE;
    }

    /* need feedback default */
    *is_need_fb = TD_TRUE;

    switch ((sample_msg_type)msg_req_buf->msg_type) {
        case SAMPLE_MSG_TYPE_VB_POOL_SHARE_REQ: {
            if (msg_res_buf == TD_NULL) {
                return TD_FAILURE;
            }
            ret = sample_venc_msg_proc_vb_pool_share(msg_req_buf->msg_data.pid);
            msg_res_buf->msg_type = SAMPLE_MSG_TYPE_VB_POOL_SHARE_RES;
            msg_res_buf->msg_data.is_req_success = (ret == TD_SUCCESS) ? TD_TRUE : TD_FALSE;
            break;
        }
        case SAMPLE_MSG_TYPE_VB_POOL_UNSHARE_REQ: {
            if (msg_res_buf == TD_NULL) {
                return TD_FAILURE;
            }
            sample_venc_msg_proc_vb_pool_unshare(msg_req_buf->msg_data.pid);
            msg_res_buf->msg_type = SAMPLE_MSG_TYPE_VB_POOL_UNSHARE_RES;
            msg_res_buf->msg_data.is_req_success = TD_TRUE;
            break;
        }
        default: {
            printf("unsupported msg type(%ld)!\n", msg_req_buf->msg_type);
            return TD_FAILURE;
        }
    }
    return TD_SUCCESS;
}

/* *****************************************************************************
 * function    : main()
 * description : video venc sample
 * **************************************************************************** */
#ifdef __LITEOS__
td_s32 app_main(td_s32 argc, td_char *argv[])
#else
td_s32 main(td_s32 argc, td_char *argv[])
#endif
{
    td_s32 ret;
    td_u32 index;
    char *end_ptr;

    setvbuf(stdout, TD_NULL, _IONBF, 0); setvbuf(stderr, TD_NULL, _IONBF, 0);
    if (argc != 0x2 && argc != 0x3) { /* 2:arg num */
        sample_venc_usage(argv[0]);
        return TD_FAILURE;
    }

    if (!strncmp(argv[1], "-h", 2)) { /* 2:arg num */
        sample_venc_usage(argv[0]);
        return TD_FAILURE;
    }

    if (argv[1][0] < '0' || argv[1][0] > '9') {
        sample_venc_usage(argv[0]);
        return TD_FAILURE;
    }

    if (argc == 0x3) {
        td_s32 save_heif = strtoul(argv[0x2], &end_ptr, 10); /* 10: numberbase */
        if (end_ptr == argv[0x2] || *end_ptr !='\0' || (save_heif != 0 && save_heif != 1)) {
            sample_venc_usage(argv[0]);
            return TD_FAILURE;
        }
        sample_comm_venc_set_save_heif((save_heif == 1) ? TD_TRUE : TD_FALSE);
    }

    index = strtoul(argv[1], &end_ptr, 10); /* 10: numberbase */
    if (end_ptr == argv[1] || *end_ptr !='\0') {
        sample_venc_usage(argv[0]);
        return TD_FAILURE;
    }
#ifndef __LITEOS__
    sample_sys_signal(sample_venc_handle_sig);
#endif

    if (sample_ipc_server_init(sample_venc_ipc_msg_proc) != TD_SUCCESS) {
        printf("sample_ipc_server_init failed!!!\n");
    }

    ret = sample_venc_choose_mode(index);
    if (ret == TD_SUCCESS) {
        printf("program exit normally!\n");
    } else {
        printf("program exit abnormally!\n");
    }

    sample_ipc_server_deinit();

#ifdef __LITEOS__
    return ret;
#else
    exit(ret);
#endif
}
