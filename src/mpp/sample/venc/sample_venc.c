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
#include "ot_mpi_ae.h"        /* 手动曝光: get/set_exposure_attr, query_exposure_info */

/* newest JPEG frame, shared: one reader thread fills, many HTTP clients read */
static unsigned char g_web_jpeg[1024 * 1024];
static volatile int g_web_jlen = 0;
static pthread_mutex_t g_web_lock = PTHREAD_MUTEX_INITIALIZER;
static ot_venc_chn g_web_chn = 0;
static int g_web_save_cnt = 0;
/* 4K JPEG snapshot channel (chn3): continuous low-fps MJPEG, newest frame kept */
/* 4K MJPEG 单帧缓冲。**8MB 不是随手写的**: 原来是 4MB, 而细节多的 4K 画面(开灯的房间)
 * 一帧就能超过 4MB —— 于是只有"包0=JPEG头(678字节)"塞得进去, "包1=图像数据"被丢掉,
 * 结果是一张结构完整却没有像素的 JPEG, 看上去全黑, 还时好时坏(取决于场景复杂度)。
 * 查了整整一轮才定位, 所以这里既加大也加了溢出告警, 见 web_reader4k_thread。 */
#define SNAP4K_CAP (8 * 1024 * 1024)
static unsigned char g_snap4k[SNAP4K_CAP];
static volatile int g_snap4k_len = 0;
static volatile int g_snap4k_drop = 0;   /* 因装不下而整帧丢弃的次数(网页 /info 可看) */
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
"  <button data-t=\"ai\">AI 检测</button>"
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
"  <section class=\"tab\" id=\"ai\">"
"    <div class=\"card\"><h4>🤖 AI 检测（NPU）</h4>"
"      <div style=\"text-align:center;margin-bottom:12px\"><img id=\"v3\" style=\"max-width:100%;border-radius:10px;transform:rotate(180deg);background:#000;border:1px solid var(--line)\" alt=\"live\"></div>"
"      <div class=\"pvmini\">"
"        <div class=\"pm\"><span>目标数</span><b id=\"ai_n\">--</b></div>"
"        <div class=\"pm\"><span>推理耗时</span><b id=\"ai_ms\">--</b></div>"
"        <div class=\"pm\"><span>NPU</span><b id=\"ai_rdy\">--</b></div>"
"        <div class=\"pm\"><span>模型</span><b id=\"ai_kind\">--</b></div>"
"      </div>"
"      <table class=\"regt\" style=\"margin-top:12px\"><thead><tr><th>#</th><th>类别</th><th>置信度</th><th>位置 x,y 尺寸</th></tr></thead>"
"      <tbody id=\"aibody\"><tr><td colspan=\"4\" class=\"d\">等待检测…</td></tr></tbody></table>"
"    </div>"
"    <div class=\"card\"><h4>模型</h4>"
"      <div class=\"fld\"><label>当前模型（切换约需 2 秒，检测会短暂中断）</label>"
"        <select id=\"npu_model\"><option value=\"\">读取中…</option></select></div>"
"      <div class=\"btnrow\"><button class=\"btn g\" onclick=\"switchModel()\">切换模型</button><button class=\"btn gray\" onclick=\"loadModels()\">刷新列表</button></div>"
"      <div class=\"msg\" id=\"mdlmsg\"></div>"
"      <small style=\"color:var(--mut)\">模型放 <code>/opt/model</code>（固化在 flash 里，重启还在）或 <code>/tmp/model</code>（临时试用，掉电即失）。.om 由 PC 端 ATC 工具从 ONNX/Caffe 转换生成，板子上转不了。</small>"
"    </div>"
"    <div class=\"card\"><h4>检测设置</h4>"
"      <div class=\"sw\"><input type=\"checkbox\" id=\"npu_en\"><label for=\"npu_en\">启用 NPU 目标检测</label></div>"
"      <div class=\"sw\"><input type=\"checkbox\" id=\"npu_draw\"><label for=\"npu_draw\">在画面上画框+类别（预览 / DSI 屏 / MJPEG 都可见）</label></div>"
"      <div class=\"fld\"><label>推理间隔（每 N 帧推一次，越大越省 CPU）</label>"
"        <select id=\"npu_iv\"><option value=\"1\">1 · 最快最耗</option><option value=\"2\">2</option><option value=\"3\">3 · 推荐</option><option value=\"5\">5</option><option value=\"10\">10 · 省电</option></select></div>"
"      <div class=\"row\"><label>置信度门槛</label><input type=range min=10 max=90 step=5 value=35 id=npu_sc><span class=val id=npu_scv>35%</span></div>"
"      <div class=\"row\"><label>重叠框合并</label><input type=range min=20 max=90 step=5 value=45 id=npu_nms><span class=val id=npu_nmsv>45%</span></div>"
"      <div class=\"fld\"><label>只检测这些类别（挡掉牙刷马桶之类的误报）</label>"
"        <select id=\"npu_preset\" onchange=\"presetCls()\">"
"          <option value=\"\">全部 80 类</option>"
"          <option value=\"0\">只看人</option>"
"          <option value=\"0,1,2,3,5,7\">人 + 车（自行车/摩托/公交/卡车）</option>"
"          <option value=\"0,1,2,3,5,7,14,15,16,17\">人 + 车 + 动物（鸟/猫/狗/马）</option>"
"          <option value=\"0,15,16,24,26,39,41,63,67\">人 + 常见室内物品</option>"
"        </select></div>"
"      <div class=\"fld\"><label>自定义类别号（逗号分隔，留空=全部）</label><input type=\"text\" id=\"npu_cls\" placeholder=\"如 0,2,15,16\"></div>"
"      <div class=\"btnrow\"><button class=\"btn g\" onclick=\"saveNpu()\">保存并生效</button></div>"
"      <div class=\"msg\" id=\"aimsg\"></div>"
"      <small style=\"color:var(--mut)\">输入 640×640 取自 VPSS 扩展通道（等比缩放 + 上下灰边），不占用 4 个编码通道。框画在 720p 预览通道；4K 主码流(RTSP)暂不画框。<br>"
"      <b>COCO 模型</b>（yolov8/yolov7.om，片上 NMS）：类别白名单和上面两个滑块都有效。<br>"
"      <b>人脸模型</b>（YuNet，裸输出、板上解码）：只出 face 一类（类别白名单自动忽略），并给出 5 个关键点，双眼画黄点；开「对焦区跟主体」后对焦窗会自动缩到双眼上＝眼控对焦。人脸模型置信度建议调到 70% 以上。</small>"
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
"    <div class=\"card\"><h4>📷 曝光（相机手动挡）</h4>"
"      <div class=\"pvmini\"><div class=\"pm\"><span>快门</span><b id=\"e_sh\">--</b></div>"
"        <div class=\"pm\"><span>增益</span><b id=\"e_ga\">--</b></div>"
"        <div class=\"pm\"><span>ISO</span><b id=\"e_iso\">--</b></div>"
"        <div class=\"pm\"><span>帧率</span><b id=\"e_fps\">--</b></div>"
"        <div class=\"pm\"><span>平均亮度</span><b id=\"e_lum\">--</b></div></div>"
"      <canvas id=\"histc\" width=\"256\" height=\"72\" style=\"width:100%;height:72px;margin-top:10px;background:#0d1117;border:1px solid var(--line);border-radius:8px\"></canvas>"
"      <div class=\"fld\" style=\"margin-top:10px\"><label>曝光挡位</label><select id=\"e_man\">"
"        <option value=\"0\">P · 自动</option><option value=\"1\">S · 快门优先（增益仍自动）</option>"
"        <option value=\"2\">M · 全手动（快门+增益锁死）</option></select></div>"
"      <div class=\"fld\"><label>快门</label><select id=\"e_us\">"
"        <option value=\"125\">1/8000</option><option value=\"250\">1/4000</option><option value=\"500\">1/2000</option>"
"        <option value=\"1000\">1/1000</option><option value=\"2000\">1/500</option><option value=\"4000\">1/250</option>"
"        <option value=\"8000\">1/125</option><option value=\"16667\">1/60</option><option value=\"20000\">1/50</option>"
"        <option value=\"33333\">1/30</option><option value=\"66667\">1/15</option><option value=\"200000\">1/5</option>"
"        <option value=\"1000000\">1s</option></select></div>"
"      <div class=\"row\"><label>增益</label><input type=range min=100 max=3200 step=100 value=100 id=e_gain><span class=val id=e_gainv>1.0x</span></div>"
"      <div class=\"row\"><label>曝光补偿（自动挡）</label><input type=range min=0 max=255 step=4 value=56 id=e_comp><span class=val id=e_compv>56</span></div>"
"      <div class=\"fld\"><label>自动挡最长快门（防暗处拖影）</label><select id=\"e_max\">"
"        <option value=\"0\">不限制</option><option value=\"8000\">1/125</option><option value=\"16667\">1/60</option>"
"        <option value=\"33333\">1/30</option><option value=\"66667\">1/15</option></select></div>"
"      <div class=\"btnrow\"><button class=\"btn g\" onclick=\"saveExp()\">应用并保存</button></div>"
"      <div class=\"msg\" id=\"expmsg\"></div>"
"    </div>"
"    <div class=\"card\"><h4>🌡️ 白平衡</h4>"
"      <div class=\"pvmini\"><div class=\"pm\"><span>当前色温</span><b id=\"w_ct\">--</b></div>"
"        <div class=\"pm\"><span>R 增益</span><b id=\"w_r\">--</b></div>"
"        <div class=\"pm\"><span>B 增益</span><b id=\"w_b\">--</b></div>"
"        <div class=\"pm\"><span>饱和度</span><b id=\"w_sat\">--</b></div></div>"
"      <div class=\"fld\" style=\"margin-top:10px\"><label>白平衡模式</label><select id=\"w_man\">"
"        <option value=\"0\">自动 AWB</option><option value=\"1\">锁色温（推荐）</option>"
"        <option value=\"2\">手动 R/B 增益</option></select></div>"
"      <div class=\"fld\"><label>色温预设</label><select id=\"w_pre\">"
"        <option value=\"2800\">2800K 白炽灯</option><option value=\"4000\">4000K 荧光灯</option>"
"        <option value=\"5000\">5000K 中性</option><option value=\"5500\">5500K 日光</option>"
"        <option value=\"6500\">6500K 阴天</option><option value=\"7500\">7500K 阴影</option></select></div>"
"      <div class=\"row\"><label>色温微调</label><input type=range min=2000 max=10000 step=100 value=5500 id=w_temp><span class=val id=w_tempv>5500K</span></div>"
"      <div class=\"row\"><label>洋红↔绿</label><input type=range min=-64 max=64 step=1 value=0 id=w_shift><span class=val id=w_shiftv>0</span></div>"
"      <div class=\"row\"><label>R 增益（手动挡）</label><input type=range min=64 max=1024 step=4 value=256 id=w_rg><span class=val id=w_rgv>1.00x</span></div>"
"      <div class=\"row\"><label>B 增益（手动挡）</label><input type=range min=64 max=1024 step=4 value=256 id=w_bg><span class=val id=w_bgv>1.00x</span></div>"
"      <div class=\"btnrow\"><button class=\"btn g\" onclick=\"saveWb()\">应用并保存</button>"
"        <button class=\"btn gray\" onclick=\"wbFromAuto()\">取当前 AWB 值</button></div>"
"      <div class=\"msg\" id=\"wbmsg\"></div>"
"      <div class=\"hint\">混合光源下 AWB 会飘，拍一组照片前把它锁死。「取当前 AWB 值」= 让自动挡先算好，再把结果固定下来。</div>"
"    </div>"
"    <div class=\"card\"><h4>🔍 放大对焦</h4>"
"      <div class=\"fld\"><label>放大倍数</label><select id=\"z_z\">"
"        <option value=\"1\">1x（关）</option><option value=\"2\">2x</option>"
"        <option value=\"4\">4x</option><option value=\"8\">8x</option></select></div>"
"      <div class=\"row\"><label>中心 X</label><input type=range min=0 max=1000 step=10 value=500 id=z_cx><span class=val id=z_cxv>50%</span></div>"
"      <div class=\"row\"><label>中心 Y</label><input type=range min=0 max=1000 step=10 value=500 id=z_cy><span class=val id=z_cyv>50%</span></div>"
"      <div class=\"msg\" id=\"zmsg\"></div>"
"      <div class=\"hint\">只放大预览和屏幕，<b>4K 主码流和 AI 检测不受影响</b>。手动镜头合焦就靠它 —— 配合上面的峰值描边更准。不存配置，重启即复位。</div>"
"    </div>"
"    <div class=\"card\"><h4>🎯 对焦助手</h4>"
"      <div class=\"stat full\"><div class=\"k\">焦点值（越大越清晰）</div><div class=\"n\" id=\"fv_now\">--</div><div class=\"bar\"><i id=\"fv_bar\" style=\"width:0\"></i></div></div>"
"      <div class=\"row\" style=\"margin-top:8px\"><label>峰值</label><span class=\"val\" id=\"fv_peak\">--</span></div>"
"      <div class=\"btnrow\"><button class=\"btn g\" id=\"fvbtn\" onclick=\"fvToggle()\">开始对焦</button><button class=\"btn gray\" onclick=\"fvReset()\">清零峰值</button></div>"
"      <small style=\"color:var(--mut)\">拧动镜头，把焦点值/峰值拧到最大即最清晰。取 ISP 画面中心区的高频对比度统计。</small>"
"    </div>"
"    <div class=\"card\"><h4>⛰️ 峰值对焦描边 / 过曝斑马纹</h4>"
"      <div class=\"sw\"><input type=\"checkbox\" id=\"pk_en\"><label for=\"pk_en\">峰值对焦描边（合焦的边缘描高亮，哪块描得最满就是合焦）</label></div>"
"      <div class=\"row\"><label>灵敏度</label><input type=range min=5 max=100 step=5 value=40 id=pk_th><span class=val id=pk_thv>40</span></div>"
"      <div class=\"fld\"><label>描边颜色</label>"
"        <select id=\"pk_color\"><option value=\"0\">红</option><option value=\"1\">黄</option><option value=\"2\">青</option><option value=\"3\">白</option></select></div>"
"      <div class=\"sw\" style=\"margin-top:10px\"><input type=\"checkbox\" id=\"zb_en\"><label for=\"zb_en\">过曝斑马纹（亮度超门槛的地方打白色斜纹）</label></div>"
"      <div class=\"row\"><label>过曝门槛</label><input type=range min=180 max=255 step=5 value=235 id=zb_th><span class=val id=zb_thv>235</span></div>"
"      <div class=\"btnrow\"><button class=\"btn g\" onclick=\"savePeak()\">保存并生效</button></div>"
"      <div class=\"msg\" id=\"pkmsg\"></div>"
"      <small style=\"color:var(--mut)\">在 NPU 那一路 640×360 的取景帧上算，画到 720p 预览叠加层（和时间戳/检测框同一层）；4K 主码流不画。约 150ms 更新一次，计算耗时见 <code>/peak</code> 的 ms 字段。</small>"
"    </div>"
"    <div class=\"card\"><h4>🕒 OSD 时间水印</h4>"
"      <div class=\"sw\"><input type=\"checkbox\" id=\"osd_en\"><label for=\"osd_en\">在画面左上角叠加时间戳</label></div>"
"      <div class=\"sw\"><input type=\"checkbox\" id=\"osd_evf\"><label for=\"osd_evf\">取景界面：直方图＋拍摄参数叠加到画面（网页/屏幕/抓拍都能看到，像相机取景器）</label></div>"
"      <div class=\"sw\"><input type=\"checkbox\" id=\"osd_focus\"><label for=\"osd_focus\">叠加焦点值+对焦条（对焦时用，看着屏幕拧镜头）</label></div>"
"      <div class=\"sw\"><input type=\"checkbox\" id=\"focus_spot\" checked><label for=\"focus_spot\">点对焦（只测画面中心，并画框）——取消则全画幅平均</label></div>"
"      <div class=\"sw\"><input type=\"checkbox\" id=\"focus_follow\"><label for=\"focus_follow\">对焦区跟着 AI 检测的主体走（黄框＝正在测它；人脸模型会自动缩到双眼上，青框＝眼控对焦）</label></div>"
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
"  setInterval(function(){if($('time').classList.contains('on'))pollTime();if($('ai').classList.contains('on'))pollNpu();"
"    if($('img').classList.contains('on'))pollExp()},1000);"
"}"
"/* tabs */"
"document.querySelectorAll('#nav button').forEach(function(b){b.onclick=function(){"
"  document.querySelectorAll('#nav button').forEach(function(x){x.classList.remove('on')});b.classList.add('on');"
"  document.querySelectorAll('.tab').forEach(function(x){x.classList.remove('on')});$(b.dataset.t).classList.add('on');"
"  if(b.dataset.t=='sys'){pollInfo();loadNet();}if(b.dataset.t=='time')pollTime();if(b.dataset.t=='rtsp')loadRtsp();if(b.dataset.t=='reg')pollReg();if(b.dataset.t=='gpio')pollGpio();if(b.dataset.t=='scr')loadScr();"
"  if(b.dataset.t=='img'){$('v2').src='/stream?'+Date.now();loadOsd();loadExp();pollExp();loadPeak();loadWb();loadZoom();}else{if($('v2'))$('v2').removeAttribute('src');fvStop();}"
"  if(b.dataset.t=='ai'){$('v3').src='/stream?'+Date.now();loadNpu();loadModels();pollNpu();}else{if($('v3'))$('v3').removeAttribute('src');}"
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
"function loadOsd(){fetch('/getosd').then(function(r){return r.json()}).then(function(j){$('osd_en').checked=!!j.en;if($('osd_evf'))$('osd_evf').checked=!!j.evf;if($('osd_focus'))$('osd_focus').checked=!!j.focus;if($('focus_spot'))$('focus_spot').checked=!!j.spot;if($('focus_follow'))$('focus_follow').checked=!!j.follow})['catch'](function(){})}"
"/* 峰值描边 + 斑马纹 */"
"function loadPeak(){fetch('/peak').then(function(r){return r.json()}).then(function(j){"
"  $('pk_en').checked=!!j.peak;$('zb_en').checked=!!j.zebra;"
"  $('pk_th').value=j.th;$('pk_thv').innerText=j.th;$('pk_color').value=j.color;"
"  $('zb_th').value=j.zth;$('zb_thv').innerText=j.zth})['catch'](function(){})}"
"$('pk_th').oninput=function(){$('pk_thv').innerText=$('pk_th').value};"
"$('zb_th').oninput=function(){$('zb_thv').innerText=$('zb_th').value};"
"function savePeak(){var u='/setpeak?peak='+($('pk_en').checked?1:0)+'&th='+$('pk_th').value+'&color='+$('pk_color').value+'&zebra='+($('zb_en').checked?1:0)+'&zth='+$('zb_th').value;"
"  msg('pkmsg','正在应用…');fetch(u).then(function(r){return r.text()}).then(function(t){msg('pkmsg',t);loadPeak()})['catch'](function(){msg('pkmsg','失败')})}"
"function saveOsd(){fetch('/setosd?en='+($('osd_en').checked?1:0)+'&evf='+($('osd_evf').checked?1:0)+'&focus='+($('osd_focus').checked?1:0)+'&spot='+($('focus_spot').checked?1:0)+'&follow='+($('focus_follow').checked?1:0)).then(function(r){return r.text()}).then(function(t){msg('osdmsg',t)})['catch'](function(){msg('osdmsg','失败')})}"
"/* exposure + histogram */"
"function shTxt(us){if(us==null||us<0)return'--';if(us>=1000000)return(us/1000000).toFixed(1)+'s';return'1/'+Math.round(1000000/us)}"
"function loadExp(){fetch('/exp').then(function(r){return r.json()}).then(function(j){"
"  $('e_man').value=String(j.manual);$('e_gain').value=j.gain;$('e_gainv').innerText=(j.gain/100).toFixed(1)+'x';"
"  $('e_comp').value=j.comp;$('e_compv').innerText=j.comp;"
"  var s=$('e_us');var best=s.options[0].value,bd=1e18;for(var i=0;i<s.options.length;i++){var d=Math.abs(s.options[i].value-j.us);if(d<bd){bd=d;best=s.options[i].value}}s.value=best;"
"  $('e_max').value=String(j.maxus);expNow(j)})['catch'](function(){})}"
"function expNow(j){$('e_sh').innerText=shTxt(j.cur_us);$('e_ga').innerText=(j.cur_gain>=0?(j.cur_gain/100).toFixed(1)+'x':'--');"
"  $('e_iso').innerText=(j.iso>=0?j.iso:'--');$('e_fps').innerText=(j.fps>=0?(j.fps/100).toFixed(1):'--');$('e_lum').innerText=(j.lum>=0?j.lum:'--')}"
"function pollExp(){fetch('/exp').then(function(r){return r.json()}).then(expNow)['catch'](function(){});"
"  fetch('/wb').then(function(r){return r.json()}).then(wbNow)['catch'](function(){});"
"  fetch('/hist').then(function(r){return r.json()}).then(drawHist)['catch'](function(){})}"
"function drawHist(j){var c=$('histc');if(!c||!c.getContext)return;var g=c.getContext('2d'),b=j.bins||[];"
"  g.clearRect(0,0,c.width,c.height);if(!b.length)return;var mx=1;for(var i=0;i<b.length;i++){if(b[i]>mx)mx=b[i]}"
"  var w=c.width/b.length;g.fillStyle='#3a7bd5';for(i=0;i<b.length;i++){var h=Math.round(b[i]/mx*(c.height-2));g.fillRect(i*w,c.height-h,Math.max(1,w-1),h)}"
"  g.fillStyle='rgba(226,80,74,.85)';var hi=Math.round(b.length*0.97);for(i=hi;i<b.length;i++){var h2=Math.round(b[i]/mx*(c.height-2));g.fillRect(i*w,c.height-h2,Math.max(1,w-1),h2)}}"
"$('e_gain').oninput=function(){$('e_gainv').innerText=($('e_gain').value/100).toFixed(1)+'x'};"
"$('e_comp').oninput=function(){$('e_compv').innerText=$('e_comp').value};"
"function saveExp(){var u='/setexp?manual='+$('e_man').value+'&us='+$('e_us').value+'&gain='+$('e_gain').value+'&comp='+$('e_comp').value+'&maxus='+$('e_max').value;"
"  msg('expmsg','正在应用…');fetch(u).then(function(r){return r.text()}).then(function(t){msg('expmsg',t);setTimeout(loadExp,600)})['catch'](function(){msg('expmsg','失败')})}"
"/* white balance */"
"var WBONE=256;"
"function gtxt(g){return (g/WBONE).toFixed(2)+'x'}"
"function loadWb(){fetch('/wb').then(function(r){return r.json()}).then(function(j){"
"  WBONE=j.one||256;$('w_man').value=String(j.manual);"
"  $('w_temp').value=j.temp;$('w_tempv').innerText=j.temp+'K';$('w_pre').value=String(j.temp);"
"  $('w_shift').value=j.shift;$('w_shiftv').innerText=j.shift;"
"  $('w_rg').value=j.rgain;$('w_rgv').innerText=gtxt(j.rgain);"
"  $('w_bg').value=j.bgain;$('w_bgv').innerText=gtxt(j.bgain);wbNow(j)})['catch'](function(){})}"
"function wbNow(j){$('w_ct').innerText=(j.cur_temp>0?j.cur_temp+'K':'--');"
"  $('w_r').innerText=(j.cur_r>=0?gtxt(j.cur_r):'--');$('w_b').innerText=(j.cur_b>=0?gtxt(j.cur_b):'--');"
"  $('w_sat').innerText=(j.sat>=0?j.sat:'--')}"
"$('w_temp').oninput=function(){$('w_tempv').innerText=$('w_temp').value+'K'};"
"$('w_shift').oninput=function(){$('w_shiftv').innerText=$('w_shift').value};"
"$('w_rg').oninput=function(){$('w_rgv').innerText=gtxt($('w_rg').value)};"
"$('w_bg').oninput=function(){$('w_bgv').innerText=gtxt($('w_bg').value)};"
"$('w_pre').onchange=function(){$('w_temp').value=$('w_pre').value;$('w_tempv').innerText=$('w_pre').value+'K'};"
"function saveWb(){var u='/setwb?manual='+$('w_man').value+'&temp='+$('w_temp').value+'&shift='+$('w_shift').value"
"  +'&rgain='+$('w_rg').value+'&bgain='+$('w_bg').value;"
"  msg('wbmsg','正在应用…');fetch(u).then(function(r){return r.text()}).then(function(t){msg('wbmsg',t);setTimeout(loadWb,600)})['catch'](function(){msg('wbmsg','失败')})}"
/* 先读 ISP 自动算出来的增益, 填进手动挡再切过去 —— 等于"锁住现在这个白平衡" */
"function wbFromAuto(){fetch('/wb').then(function(r){return r.json()}).then(function(j){"
"  if(j.cur_r>0){$('w_rg').value=Math.min(1024,j.cur_r);$('w_rgv').innerText=gtxt(j.cur_r)}"
"  if(j.cur_b>0){$('w_bg').value=Math.min(1024,j.cur_b);$('w_bgv').innerText=gtxt(j.cur_b)}"
"  if(j.cur_temp>0){$('w_temp').value=Math.max(2000,Math.min(10000,j.cur_temp));$('w_tempv').innerText=$('w_temp').value+'K'}"
"  $('w_man').value='2';msg('wbmsg','已取当前 AWB 值，点「应用并保存」锁定')})['catch'](function(){msg('wbmsg','读取失败')})}"
"/* punch-in zoom */"
"function loadZoom(){fetch('/zoom').then(function(r){return r.json()}).then(function(j){"
"  $('z_z').value=String(j.z);$('z_cx').value=j.cx;$('z_cxv').innerText=Math.round(j.cx/10)+'%';"
"  $('z_cy').value=j.cy;$('z_cyv').innerText=Math.round(j.cy/10)+'%'})['catch'](function(){})}"
"function applyZoom(){var u='/setzoom?z='+$('z_z').value+'&cx='+$('z_cx').value+'&cy='+$('z_cy').value;"
"  msg('zmsg','…');fetch(u).then(function(r){return r.text()}).then(function(t){msg('zmsg',t)})['catch'](function(){msg('zmsg','失败')})}"
"$('z_z').onchange=applyZoom;"
"$('z_cx').oninput=function(){$('z_cxv').innerText=Math.round($('z_cx').value/10)+'%'};"
"$('z_cy').oninput=function(){$('z_cyv').innerText=Math.round($('z_cy').value/10)+'%'};"
"$('z_cx').onchange=applyZoom;$('z_cy').onchange=applyZoom;"
"/* ai detect */"
"function loadNpu(){fetch('/npu').then(function(r){return r.json()}).then(function(j){"
"  $('npu_en').checked=!!j.en;$('npu_draw').checked=!!j.draw;$('npu_iv').value=j.interval;"
"  $('npu_sc').value=j.score;$('npu_scv').innerText=j.score+'%';$('npu_cls').value=j.cls||'';"
"  $('npu_nms').value=j.nms;$('npu_nmsv').innerText=j.nms+'%';"
"  $('npu_preset').value=(j.cls||'')})['catch'](function(){})}"
"$('npu_sc').oninput=function(){$('npu_scv').innerText=$('npu_sc').value+'%'};"
"$('npu_nms').oninput=function(){$('npu_nmsv').innerText=$('npu_nms').value+'%'};"
"function presetCls(){$('npu_cls').value=$('npu_preset').value}"
"function loadModels(){fetch('/models').then(function(r){return r.json()}).then(function(j){"
"  var h='',L=j.list||[];for(var i=0;i<L.length;i++){h+='<option value=\"'+L[i].name+'\">'+L[i].name+' · '+L[i].kb+' KB · '+L[i].dir+'</option>'}"
"  $('npu_model').innerHTML=h||'<option value=\"\">没找到 .om 模型</option>';$('npu_model').value=j.cur"
"})['catch'](function(){msg('mdlmsg','读取模型列表失败')})}"
"function switchModel(){var m=$('npu_model').value;if(!m)return;msg('mdlmsg','正在切换到 '+m+'…');"
"  fetch('/setnpu?model='+encodeURIComponent(m)).then(function(r){return r.text()}).then(function(t){"
"    setTimeout(function(){loadNpu();loadModels();pollNpu();msg('mdlmsg','已切换到 '+m)},2500)})['catch'](function(){msg('mdlmsg','切换失败')})}"
"function saveNpu(){var u='/setnpu?en='+($('npu_en').checked?1:0)+'&draw='+($('npu_draw').checked?1:0)+'&interval='+$('npu_iv').value+'&score='+$('npu_sc').value+'&nms='+$('npu_nms').value+'&cls='+encodeURIComponent($('npu_cls').value);"
"  msg('aimsg','正在应用…');fetch(u).then(function(r){return r.text()}).then(function(t){msg('aimsg',t);loadNpu()})['catch'](function(){msg('aimsg','失败')})}"
"function pollNpu(){fetch('/npu').then(function(r){return r.json()}).then(function(j){"
"  $('ai_n').innerText=j.n;$('ai_ms').innerText=(j.ms>0?j.ms+' ms':'--');"
"  $('ai_rdy').innerText=j.ready?(j.en?'运行中':'已暂停'):(j.en?'启动中…':'关闭');"
"  $('ai_kind').innerText=(j.kind=='face'?'人脸':'COCO');"
"  var h='',d=j.det||[];for(var i=0;i<d.length;i++){var p=d[i].kp||[];"
"    var ex=(p.length>=2)?(' · 眼 '+p[0][0]+','+p[0][1]+' / '+p[1][0]+','+p[1][1]):'';"
"    h+='<tr><td class=a>'+(i+1)+'</td><td>'+d[i].n+'</td>'"
"    +'<td class=v>'+(d[i].s>=0?d[i].s+'%':'--')+'</td><td class=d>'+d[i].x+','+d[i].y+' · '+d[i].w+'×'+d[i].h+ex+'</td></tr>'}"
"  $('aibody').innerHTML=h||'<tr><td colspan=\"4\" class=\"d\">未检测到目标</td></tr>'})['catch'](function(){})}"
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
static int  g_focus_follow = 0;    /* 1=对焦测量区跟着 NPU 检测到的主体走(眼控/脸控对焦的基础) */
/* 取景界面: 直方图 + 拍摄参数直接画进画面(不是网页) —— 网页/MJPEG/DSI屏/抓拍全都自带 */
static int  g_evf_en = 1;
/* 手动曝光 (ISP AE). 相机就得能手动. gain 用"倍数×100"存(100=1.0x), 对上 ISP 的 22.10 定点 */
static int  g_exp_manual = 0;      /* 曝光挡位: 0=自动P  1=快门优先S(快门手动/增益自动)  2=全手动M */
static int  g_exp_us = 20000;      /* 手动快门, 微秒 (20000us = 1/50s) */
static int  g_exp_gain = 100;      /* 手动增益 ×100 (100=1.0x, 1600=16x) */
static int  g_exp_comp = 56;       /* 自动模式下的曝光补偿 0..255 (ISP 默认 56) */
static int  g_exp_maxus = 0;       /* 自动模式下最长快门(微秒), 0=不限制. 防止暗处糊成一片 */
static unsigned long long g_focus_peak = 0;  /* 对焦条峰值(缩放用) */
/* 手动白平衡 (ISP AWB). 相机不该只有"自动白平衡" —— 混合光源下 AWB 会飘, 拍序列必须锁死.
 * ⚠ 增益是 4.8 定点: WB_GAIN_ONE(0x100) = 1.0x, 上限 0xFFF ≈ 16x.
 *   (读回用的 ot_isp_wb_info 标的是 8.8, 但小数位同样是 8 位 —— 两边刻度一致, 只是量程不同) */
#define WB_GAIN_ONE 0x100
static int  g_wb_manual = 0;       /* 0=自动AWB  1=按色温锁(推荐)  2=手动 R/B 增益 */
static int  g_wb_temp = 5500;      /* 色温 K (模式1), SDK 用 cal_gain_by_temp 换算成增益 */
static int  g_wb_shift = 0;        /* 色温微调(洋红↔绿), 直接透传给 cal_gain_by_temp 的 shift */
static int  g_wb_rgain = WB_GAIN_ONE;  /* 模式2: R 增益 4.8 定点 */
static int  g_wb_bgain = WB_GAIN_ONE;  /* 模式2: B 增益 4.8 定点 */
/* 放大对焦(punch-in): 手动镜头合焦最靠谱的办法就是放大看.
 * 用 VPSS 通道 crop 实现 —— 只裁 chn2(预览/屏幕), 主码流 chn0 和送 NPU 的扩展通道都不受影响,
 * 所以放大查焦的同时 4K 录制和 AI 检测照常。**故意不存 param**: 这是临时取景动作,
 * 重启后还停在放大状态只会让人莫名其妙。 */
static int  g_zoom = 1;            /* 放大倍数: 1=关 2/4/8 */
static int  g_zoom_cx = 500;       /* 放大中心 X, 千分比 (可平移查看边角) */
static int  g_zoom_cy = 500;       /* 放大中心 Y, 千分比 */
/* NPU 目标检测 (YOLOv8, svp_acl). g_npu_en=配置开关; g_npu_interval=每N帧推一次 */
static int  g_npu_en = 1;          /* 默认开(本机就叫 AI camera); web「AI检测」页可关 */
static int  g_npu_interval = 3;    /* 每 3 帧推理一次, 省 4×A55 负载 */
static int  g_npu_draw = 1;        /* 1=把框+类别画进画面(region overlay, 预览/屏幕/MJPEG 都可见) */
static int  g_npu_score = 35;      /* 置信度门槛 %: 低于此值的框丢掉(模型自带 NMS 阈值 0.15 太松) */
static char g_npu_cls[128] = "";   /* 类别白名单, 逗号分隔的 COCO 类别号; 空=全部要 */
static int  g_npu_nms = 45;        /* NMS 交并比阈值 %: 越小合并越狠. 官方样例给 90 = 几乎不合并,
                                    * 同一个人会出好几个重叠框, 所以默认收到 45 */
static char g_npu_model[64] = "yolov8.om";   /* 当前模型文件名, 在 /tmp/model 或 /opt/model 里找 */
static int  g_npu_ready = 0;       /* 运行时: NPU 初始化成功 */
static int  g_npu_last_num = 0;    /* 最近一帧检测框数 (供 web 读取) */
static int  g_npu_ms = 0;          /* 最近一次推理耗时 ms */
/* 峰值对焦描边 + 过曝斑马纹 —— 手动镜头的两个基本取景辅助, 都在扩展通道那帧上算,
 * 画到 chn2 那张全幅叠加层上(和时间戳/检测框同一个 region, 单一写者=OSD 线程). */
static int  g_peak_en = 0;         /* 峰值对焦: 合焦的边缘描高亮 */
static int  g_peak_th = 40;        /* 灵敏度 0..100, 越大越只描最锐的边 */
static int  g_peak_color = 0;      /* 0=红 1=黄 2=青 3=白 */
static int  g_zebra_en = 0;        /* 过曝斑马纹 */
static int  g_zebra_th = 235;      /* 亮度门槛 0..255, 超过就打斜纹 */
static int  g_vis_ms = 0;          /* 最近一次描边/斑马纹计算耗时 ms (性能观察) */
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
               "scr_en=%d\nscr_bright=%d\nscr_src=%d\nscr_lr=%d\nosd_en=%d\nosd_focus=%d\nfocus_spot=%d\n"
               "npu_en=%d\nnpu_interval=%d\nnpu_draw=%d\nnpu_score=%d\nnpu_cls=%s\nnpu_nms=%d\n"
               "npu_model=%s\nfocus_follow=%d\n"
               "exp_manual=%d\nexp_us=%d\nexp_gain=%d\nexp_comp=%d\nexp_maxus=%d\n"
               "peak_en=%d\npeak_th=%d\npeak_color=%d\nzebra_en=%d\nzebra_th=%d\n"
               "wb_manual=%d\nwb_temp=%d\nwb_shift=%d\nwb_rgain=%d\nwb_bgain=%d\nevf_en=%d\n",
        g_cam_user, g_cam_pass, g_rtsp_auth_en, g_net_ip, g_net_mask, g_net_gw,
        g_isp_luma, g_isp_contr, g_isp_satu, g_isp_hue,
        g_scr_en, g_scr_bright, g_scr_src, g_scr_lr, g_osd_en, g_osd_focus_en, g_focus_spot,
        g_npu_en, g_npu_interval, g_npu_draw, g_npu_score, g_npu_cls, g_npu_nms, g_npu_model,
        g_focus_follow, g_exp_manual, g_exp_us, g_exp_gain, g_exp_comp, g_exp_maxus,
        g_peak_en, g_peak_th, g_peak_color, g_zebra_en, g_zebra_th,
        g_wb_manual, g_wb_temp, g_wb_shift, g_wb_rgain, g_wb_bgain, g_evf_en);
        /* 注意: g_zoom* 是**故意不存**的 —— 放大对焦是临时取景动作, 见其定义处的说明 */
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
        else if (!strncmp(line, "npu_en=", 7)) { g_npu_en = atoi(line + 7) ? 1 : 0; }
        else if (!strncmp(line, "npu_interval=", 13)) { g_npu_interval = atoi(line + 13); if (g_npu_interval < 1) { g_npu_interval = 1; } }
        else if (!strncmp(line, "npu_draw=", 9)) { g_npu_draw = atoi(line + 9) ? 1 : 0; }
        else if (!strncmp(line, "npu_score=", 10)) { g_npu_score = atoi(line + 10); if (g_npu_score < 1) { g_npu_score = 1; } if (g_npu_score > 99) { g_npu_score = 99; } }
        else if (!strncmp(line, "npu_cls=", 8)) { snprintf(g_npu_cls, sizeof(g_npu_cls), "%s", line + 8); }
        else if (!strncmp(line, "npu_nms=", 8)) { g_npu_nms = atoi(line + 8); if (g_npu_nms < 5) { g_npu_nms = 5; } if (g_npu_nms > 95) { g_npu_nms = 95; } }
        else if (!strncmp(line, "npu_model=", 10)) { if (line[10] != '\0') { snprintf(g_npu_model, sizeof(g_npu_model), "%s", line + 10); } }
        else if (!strncmp(line, "focus_follow=", 13)) { g_focus_follow = atoi(line + 13) ? 1 : 0; }
        /* 曝光挡是 0/1/2 三挡, 不能按布尔存 —— 以前写成 ?1:0, 重启后 M 挡会退回 S 挡 */
        else if (!strncmp(line, "exp_manual=", 11)) {
            g_exp_manual = atoi(line + 11);
            if (g_exp_manual < 0 || g_exp_manual > 2) { g_exp_manual = 0; }
        }
        else if (!strncmp(line, "exp_us=", 7)) { g_exp_us = atoi(line + 7); }
        else if (!strncmp(line, "exp_gain=", 9)) { g_exp_gain = atoi(line + 9); }
        else if (!strncmp(line, "exp_comp=", 9)) { g_exp_comp = atoi(line + 9); }
        else if (!strncmp(line, "exp_maxus=", 10)) { g_exp_maxus = atoi(line + 10); }
        else if (!strncmp(line, "peak_en=", 8)) { g_peak_en = atoi(line + 8) ? 1 : 0; }
        else if (!strncmp(line, "peak_th=", 8)) { g_peak_th = atoi(line + 8); if (g_peak_th < 1) { g_peak_th = 1; } if (g_peak_th > 100) { g_peak_th = 100; } }
        else if (!strncmp(line, "peak_color=", 11)) { g_peak_color = atoi(line + 11) & 3; }
        else if (!strncmp(line, "zebra_en=", 9)) { g_zebra_en = atoi(line + 9) ? 1 : 0; }
        else if (!strncmp(line, "zebra_th=", 9)) { g_zebra_th = atoi(line + 9); if (g_zebra_th < 100) { g_zebra_th = 100; } if (g_zebra_th > 255) { g_zebra_th = 255; } }
        /* 白平衡也是三挡(自动/色温/手动增益), 同样别按布尔存 */
        else if (!strncmp(line, "wb_manual=", 10)) {
            g_wb_manual = atoi(line + 10);
            if (g_wb_manual < 0 || g_wb_manual > 2) { g_wb_manual = 0; }
        }
        else if (!strncmp(line, "wb_temp=", 8)) { g_wb_temp = atoi(line + 8); if (g_wb_temp < 2000) { g_wb_temp = 2000; } if (g_wb_temp > 15000) { g_wb_temp = 15000; } }
        else if (!strncmp(line, "wb_shift=", 9)) { g_wb_shift = atoi(line + 9); }
        else if (!strncmp(line, "wb_rgain=", 9)) { g_wb_rgain = atoi(line + 9); if (g_wb_rgain < 1) { g_wb_rgain = 1; } if (g_wb_rgain > 0xFFF) { g_wb_rgain = 0xFFF; } }
        else if (!strncmp(line, "wb_bgain=", 9)) { g_wb_bgain = atoi(line + 9); if (g_wb_bgain < 1) { g_wb_bgain = 1; } if (g_wb_bgain > 0xFFF) { g_wb_bgain = 0xFFF; } }
        else if (!strncmp(line, "evf_en=", 7)) { g_evf_en = atoi(line + 7) ? 1 : 0; }
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
/* ===== 手动曝光 (ISP AE) =====
 * 相机就得能定快门定增益. ISP 的 ot_isp_exposure_attr:
 *   op_type = AUTO/MANUAL 总开关; manual_attr 里每一项还能各自 AUTO/MANUAL
 *   exp_time 单位微秒; a_gain 是 22.10 定点 (0x400 = 1.0x)
 * 自动模式下我们只改两样有用的: 曝光补偿 compensation 和最长快门(暗处防糊).
 * 这几个函数是"控制层", 网页/串口/按键都调它, 不各自去碰 ISP. */
#define EXP_GAIN_ONE 1024u                 /* 22.10 定点的 1.0x */

static int cam_exp_apply(void)
{
    ot_isp_exposure_attr ea;
    if (ot_mpi_isp_get_exposure_attr(0, &ea) != TD_SUCCESS) { return -1; }
    if (g_exp_manual) {
        /* S 挡只锁快门, 增益仍由 AE 补(所以画面亮度不变, ISO 会飙);
         * M 挡把模拟/数字增益全锁死, 定多少就是多少 —— 拍照要的是这个 */
        int full = (g_exp_manual >= 2);
        ea.op_type = OT_OP_MODE_MANUAL;
        ea.manual_attr.exp_time_op_type = OT_OP_MODE_MANUAL;
        ea.manual_attr.a_gain_op_type   = OT_OP_MODE_MANUAL;
        ea.manual_attr.d_gain_op_type    = full ? OT_OP_MODE_MANUAL : OT_OP_MODE_AUTO;
        ea.manual_attr.ispd_gain_op_type = full ? OT_OP_MODE_MANUAL : OT_OP_MODE_AUTO;
        ea.manual_attr.exp_time = (td_u32)((g_exp_us > 0) ? g_exp_us : 1);
        ea.manual_attr.a_gain = (td_u32)((unsigned long long)g_exp_gain * EXP_GAIN_ONE / 100u);
        if (ea.manual_attr.a_gain < EXP_GAIN_ONE) { ea.manual_attr.a_gain = EXP_GAIN_ONE; }
        if (full) {
            ea.manual_attr.d_gain = EXP_GAIN_ONE;       /* 数字增益 1.0x, 不偷偷提亮 */
            ea.manual_attr.isp_d_gain = EXP_GAIN_ONE;
        }
    } else {
        ea.op_type = OT_OP_MODE_AUTO;
        ea.auto_attr.compensation = (td_u8)((g_exp_comp < 0) ? 0 : ((g_exp_comp > 255) ? 255 : g_exp_comp));
        if (g_exp_maxus > 0) { ea.auto_attr.exp_time_range.max = (td_u32)g_exp_maxus; }
    }
    if (ot_mpi_isp_set_exposure_attr(0, &ea) != TD_SUCCESS) { return -1; }
    return 0;
}

/* 读回 ISP 实际生效的曝光(自动模式下就是 AE 算出来的), 网页显示"当前快门/增益/ISO/帧率/平均亮度" */
typedef struct { int us, gain_x100, iso, fps, lum; } cam_exp_now;
static void cam_exp_read(cam_exp_now *n)
{
    ot_isp_exp_info info;
    n->us = -1; n->gain_x100 = -1; n->iso = -1; n->fps = -1; n->lum = -1;
    if (ot_mpi_isp_query_exposure_info(0, &info) != TD_SUCCESS) { return; }
    n->us = (int)info.exp_time;
    n->gain_x100 = (int)((unsigned long long)info.a_gain * 100u / EXP_GAIN_ONE);
    n->iso = (int)info.iso;
    n->fps = (int)info.fps;
    n->lum = (int)info.ave_lum;
}

/* 手动白平衡. 三挡:
 *   0 自动 —— ISP 的 AWB 自己跑
 *   1 色温 —— 锁成某个开尔文值。**这是最像相机的一挡**: 用 SDK 的 cal_gain_by_temp
 *             按色温反算出 R/Gr/Gb/B 增益, 不用自己拧 RGB
 *   2 手动 —— 直接给 R/B 增益(4.8 定点), 给死磕的人留的
 * 模式 1 要拿一份当前 wb_attr 传进 cal_gain_by_temp(它要读里面的标定数据), 所以先 get 再算。*/
static int cam_wb_apply(void)
{
    ot_isp_wb_attr wb;
    if (ot_mpi_isp_get_wb_attr(0, &wb) != TD_SUCCESS) { return -1; }
    if (g_wb_manual) {
        wb.op_type = OT_OP_MODE_MANUAL;
        if (g_wb_manual == 1) {
            td_u16 gain[4] = { WB_GAIN_ONE, WB_GAIN_ONE, WB_GAIN_ONE, WB_GAIN_ONE };
            td_u16 t = (td_u16)((g_wb_temp < 2000) ? 2000 : ((g_wb_temp > 15000) ? 15000 : g_wb_temp));
            if (ot_mpi_isp_cal_gain_by_temp(0, &wb, t, (td_s16)g_wb_shift, gain) != TD_SUCCESS) {
                return -1;
            }
            wb.manual_attr.r_gain  = gain[0];
            wb.manual_attr.gr_gain = gain[1];
            wb.manual_attr.gb_gain = gain[2];
            wb.manual_attr.b_gain  = gain[3];
        } else {
            int r = g_wb_rgain, b = g_wb_bgain;
            if (r < 1) { r = 1; } if (r > 0xFFF) { r = 0xFFF; }
            if (b < 1) { b = 1; } if (b > 0xFFF) { b = 0xFFF; }
            wb.manual_attr.r_gain  = (td_u16)r;
            wb.manual_attr.gr_gain = WB_GAIN_ONE;    /* 绿通道作基准, 不动 */
            wb.manual_attr.gb_gain = WB_GAIN_ONE;
            wb.manual_attr.b_gain  = (td_u16)b;
        }
    } else {
        wb.op_type = OT_OP_MODE_AUTO;
    }
    if (ot_mpi_isp_set_wb_attr(0, &wb) != TD_SUCCESS) { return -1; }
    return 0;
}

/* 读回 ISP 实际生效的白平衡 —— 自动挡下就是 AWB 算出来的, 网页可以看着它挑色温 */
typedef struct { int r, gr, gb, b, temp, sat; } cam_wb_now;
static void cam_wb_read(cam_wb_now *n)
{
    ot_isp_wb_info info;
    n->r = n->gr = n->gb = n->b = n->temp = n->sat = -1;
    if (ot_mpi_isp_query_wb_info(0, &info) != TD_SUCCESS) { return; }
    n->r = (int)info.r_gain;   n->gr = (int)info.gr_gain;
    n->gb = (int)info.gb_gain; n->b  = (int)info.b_gain;
    n->temp = (int)info.color_temp;
    n->sat  = (int)info.saturation;
}

/* 放大对焦: 在 VPSS 通道级 crop 一块再让缩放器铺满输出 = 光学等效的"放大查焦".
 * 裁的是通道**输入**(= VPSS group 输出, 3840x2160), 所以 zoom=4 时裁 960x540 铺满 1280x720。
 * 只作用于 g_scr_src 那条预览通道; 主码流/扩展通道不碰。 */
#define ZOOM_SRC_W 3840          /* VPSS group 输出 = 传感器全画幅, crop 坐标以此为准 */
#define ZOOM_SRC_H 2160
static int cam_zoom_apply(void)
{
    ot_vpss_crop_info crop;
    int z = g_zoom, cx = g_zoom_cx, cy = g_zoom_cy;
    int w, h, x, y;
    memset(&crop, 0, sizeof(crop));
    /* ⚠ 关闭时也得填一个**合法的**矩形: MPP 连 enable=FALSE 的调用也会校验 crop_rect,
     * 传全零(width/height=0)会被判非法直接拒绝 —— 于是放大就再也关不掉了。 */
    crop.crop_mode = OT_COORD_ABS;
    crop.crop_rect.x = 0;
    crop.crop_rect.y = 0;
    crop.crop_rect.width  = ZOOM_SRC_W;
    crop.crop_rect.height = ZOOM_SRC_H;
    if (z <= 1) {
        crop.enable = TD_FALSE;
        return (ss_mpi_vpss_set_chn_crop(0, g_web_chn, &crop) == TD_SUCCESS) ? 0 : -1;
    }
    if (z > 16) { z = 16; }
    if (cx < 0) { cx = 0; } if (cx > 1000) { cx = 1000; }
    if (cy < 0) { cy = 0; } if (cy > 1000) { cy = 1000; }
    w = (ZOOM_SRC_W / z) & ~1;          /* 偶数对齐, VPSS 要求 */
    h = (ZOOM_SRC_H / z) & ~1;
    x = (int)((long)(ZOOM_SRC_W - w) * cx / 1000) & ~1;
    y = (int)((long)(ZOOM_SRC_H - h) * cy / 1000) & ~1;
    crop.enable = TD_TRUE;
    crop.crop_mode = OT_COORD_ABS;
    crop.crop_rect.x = x;
    crop.crop_rect.y = y;
    crop.crop_rect.width  = (td_u32)w;
    crop.crop_rect.height = (td_u32)h;
    return (ss_mpi_vpss_set_chn_crop(0, g_web_chn, &crop) == TD_SUCCESS) ? 0 : -1;
}

/* 亮度直方图: ISP 硬件给 1024 bin, 归到 64 格返回 (画面/网页都用得上) */
#define HIST_BINS 64
static int cam_hist_get(unsigned int out[HIST_BINS])
{
    static ot_isp_ae_stats st;      /* 结构挺大(几十KB), 放静态别爆线程栈 */
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    int i, k;
    unsigned long long total = 0;
    pthread_mutex_lock(&lock);
    if (ot_mpi_isp_get_ae_stats(0, &st) != TD_SUCCESS) { pthread_mutex_unlock(&lock); return -1; }
    for (i = 0; i < HIST_BINS; i++) { out[i] = 0; }
    for (i = 0; i < OT_ISP_HIST_NUM; i++) {
        k = i * HIST_BINS / OT_ISP_HIST_NUM;
        out[k] += st.be_hist1024_value[i];
        total += st.be_hist1024_value[i];
    }
    pthread_mutex_unlock(&lock);
    return (total > 0) ? 0 : -1;
}

/* 对焦测量区(AF 统计的 15x17 网格里的一块矩形). 三种来源:
 *   跟主体(g_focus_follow 且有检测框) > 中心块(g_focus_spot) > 全画幅
 * 输出的 4 个格子号也给画面用, 这样"框在哪就量哪"看得见. 返回 1=跟着主体 */
static int cam_focus_zone(int *pr0, int *pr1, int *pc0, int *pc1);

/* 焦点值 = 所选区分区 (h1+h2+v1+v2) 之和; 越大越锐. 失败 -1 */
static long long cam_focus_value(void)
{
    ot_isp_af_stats af;
    int r, c, r0, r1, c0, c1;
    unsigned long long s = 0;
    if (!g_af_ok) { return -1; }
    if (ot_mpi_isp_get_focus_stats(0, &af) != TD_SUCCESS) { return -1; }
    (td_void)cam_focus_zone(&r0, &r1, &c0, &c1);
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

/* chn2(720p 预览)的合成图层: 时间戳/对焦条/对焦框/检测框全画在这一个全幅 region 里.
 * MPP 的 overlay region 贴到同一个 venc 通道时不能互相重叠 —— 一个全幅 region 会把
 * 别的 region 顶掉(实测: 加了全幅检测框图层后时间戳就没了), 所以 chn2 只用一层.
 * 实现在后面的 NPU 模块(那边才有字体和检测框数据), 这里先声明. */
static void cam_chn2_rgn_init(void);
static void cam_chn2_draw(void);

/* create overlay region 0 (OSD text, 只贴 chn0 主4K) + chn2 的全幅合成层 */
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
    g_osd_ready = 1;
    printf("[osd] timestamp overlay up (chn0)\n");

    cam_chn2_rgn_init();   /* 预览通道的全幅合成层(时间戳+对焦+检测框都画这里) */
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

    cam_chn2_draw();   /* 预览通道那一层(时间戳+对焦条+对焦框+检测框) */
}

static void *cam_osd_thread(void *arg)
{
    (td_void)arg;
    while (1) {
        cam_osd_update();
        /* 对焦时看峰值、开检测时框要跟手 -> 5Hz; 平时 1Hz 够刷秒 */
        if (g_osd_focus_en || g_peak_en || g_zebra_en ||
            (g_npu_en && g_npu_draw && g_npu_ready)) { usleep(200000); }
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
/* 取查询参数。注意必须校验匹配点前面是 '?' 或 '&' —— 裸 strstr("th=") 会一头撞进
 * "zth=150" 里把斑马纹门槛当成描边灵敏度; 同理 "us=" 会命中 "maxus="。
 * 这类参数名互为后缀的组合本固件里就有两对(th/zth, us/maxus), 网页只是靠"短的排前面"
 * 侥幸躲过, 直接调接口就会中招。 */
static int cam_qparam(const char *req, const char *key, char *out, int cap)
{
    char pat[40]; const char *p, *e; size_t plen;
    snprintf(pat, sizeof(pat), "%s=", key);
    plen = strlen(pat);
    p = req;
    for (;;) {
        p = strstr(p, pat);
        if (p == TD_NULL) { out[0] = '\0'; return 0; }
        if (p != req && (p[-1] == '?' || p[-1] == '&')) { break; }  /* 是完整参数名 */
        p += plen;                                                 /* 半路命中, 继续找 */
    }
    p += plen;
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
    char j[110]; int n;
    n = snprintf(j, sizeof(j), "{\"en\":%d,\"ready\":%d,\"focus\":%d,\"spot\":%d,\"follow\":%d,\"evf\":%d}",
                 g_osd_en, g_osd_ready, g_osd_focus_en, g_focus_spot, g_focus_follow, g_evf_en);
    cam_http_body(fd, "application/json", j, n);
}

/* GET /exp -> 曝光设置 + 当前实际值(快门/增益/ISO/帧率/平均亮度) */
static void cam_http_exp(int fd)
{
    char j[256]; int n;
    cam_exp_now now;
    cam_exp_read(&now);
    n = snprintf(j, sizeof(j),
        "{\"manual\":%d,\"us\":%d,\"gain\":%d,\"comp\":%d,\"maxus\":%d,"
        "\"cur_us\":%d,\"cur_gain\":%d,\"iso\":%d,\"fps\":%d,\"lum\":%d}",
        g_exp_manual, g_exp_us, g_exp_gain, g_exp_comp, g_exp_maxus,
        now.us, now.gain_x100, now.iso, now.fps, now.lum);
    cam_http_body(fd, "application/json", j, n);
}

/* GET /setexp?manual=&us=&gain=&comp=&maxus= : 立即生效 + 存 param */
static void cam_http_setexp(int fd, const char *req)
{
    char v[24];
    int changed = 0, rc;
    if (cam_qparam(req, "manual", v, sizeof(v))) {
        int m = atoi(v);
        g_exp_manual = (m < 0) ? 0 : ((m > 2) ? 2 : m);   /* 0=P 1=S 2=M */
        changed = 1;
    }
    if (cam_qparam(req, "us", v, sizeof(v))) {
        int t = atoi(v);
        if (t < 10) { t = 10; }              /* 1/100000s 以下没意义 */
        if (t > 1000000) { t = 1000000; }    /* 1 秒上限(帧率会自动降) */
        g_exp_us = t; changed = 1;
    }
    if (cam_qparam(req, "gain", v, sizeof(v))) {
        int g = atoi(v);
        if (g < 100) { g = 100; }            /* 1.0x 起 */
        if (g > 6400) { g = 6400; }          /* 64x 顶 */
        g_exp_gain = g; changed = 1;
    }
    if (cam_qparam(req, "comp", v, sizeof(v))) { g_exp_comp = atoi(v); changed = 1; }
    if (cam_qparam(req, "maxus", v, sizeof(v))) { g_exp_maxus = atoi(v); changed = 1; }
    if (!changed) { cam_http_text(fd, "no change"); return; }
    rc = cam_exp_apply();
    cam_cfg_save();
    cam_http_text(fd, (rc == 0) ? "\xe5\xb7\xb2\xe5\xba\x94\xe7\x94\xa8"      /* 已应用 */
                                : "ISP \xe6\x8b\x92\xe7\xbb\x9d");            /* ISP 拒绝 */
}

/* GET /wb -> 白平衡设置 + ISP 当前实际增益/色温 (自动挡时可以照着它挑色温) */
static void cam_http_wb(int fd)
{
    char j[256]; int n;
    cam_wb_now now;
    cam_wb_read(&now);
    n = snprintf(j, sizeof(j),
        "{\"manual\":%d,\"temp\":%d,\"shift\":%d,\"rgain\":%d,\"bgain\":%d,"
        "\"cur_r\":%d,\"cur_gr\":%d,\"cur_gb\":%d,\"cur_b\":%d,\"cur_temp\":%d,\"sat\":%d,\"one\":%d}",
        g_wb_manual, g_wb_temp, g_wb_shift, g_wb_rgain, g_wb_bgain,
        now.r, now.gr, now.gb, now.b, now.temp, now.sat, WB_GAIN_ONE);
    cam_http_body(fd, "application/json", j, n);
}

/* GET /setwb?manual=&temp=&shift=&rgain=&bgain= : 立即生效 + 存 param */
static void cam_http_setwb(int fd, const char *req)
{
    char v[24];
    int changed = 0, rc;
    if (cam_qparam(req, "manual", v, sizeof(v))) {
        int m = atoi(v);
        g_wb_manual = (m < 0) ? 0 : ((m > 2) ? 2 : m);   /* 0=自动 1=色温 2=手动增益 */
        changed = 1;
    }
    if (cam_qparam(req, "temp", v, sizeof(v))) {
        int t = atoi(v);
        if (t < 2000) { t = 2000; }          /* 烛光以下 */
        if (t > 15000) { t = 15000; }        /* 深阴影以上 */
        g_wb_temp = t; changed = 1;
    }
    if (cam_qparam(req, "shift", v, sizeof(v))) { g_wb_shift = atoi(v); changed = 1; }
    if (cam_qparam(req, "rgain", v, sizeof(v))) {
        int g = atoi(v);
        if (g < 1) { g = 1; } if (g > 0xFFF) { g = 0xFFF; }
        g_wb_rgain = g; changed = 1;
    }
    if (cam_qparam(req, "bgain", v, sizeof(v))) {
        int g = atoi(v);
        if (g < 1) { g = 1; } if (g > 0xFFF) { g = 0xFFF; }
        g_wb_bgain = g; changed = 1;
    }
    if (!changed) { cam_http_text(fd, "no change"); return; }
    rc = cam_wb_apply();
    cam_cfg_save();
    cam_http_text(fd, (rc == 0) ? "\xe5\xb7\xb2\xe5\xba\x94\xe7\x94\xa8"      /* 已应用 */
                                : "ISP \xe6\x8b\x92\xe7\xbb\x9d");            /* ISP 拒绝 */
}

/* GET /zoom -> 放大对焦状态 (不存 param, 所以只是运行时值) */
static void cam_http_zoom(int fd)
{
    char j[96]; int n;
    n = snprintf(j, sizeof(j), "{\"z\":%d,\"cx\":%d,\"cy\":%d,\"srcw\":%d,\"srch\":%d}",
                 g_zoom, g_zoom_cx, g_zoom_cy, ZOOM_SRC_W, ZOOM_SRC_H);
    cam_http_body(fd, "application/json", j, n);
}

/* GET /setzoom?z=&cx=&cy= : 只影响预览通道, 主码流和 AI 检测照常 */
static void cam_http_setzoom(int fd, const char *req)
{
    char v[24];
    int changed = 0, rc;
    if (cam_qparam(req, "z", v, sizeof(v))) {
        int z = atoi(v);
        if (z < 1) { z = 1; } if (z > 16) { z = 16; }
        g_zoom = z; changed = 1;
    }
    if (cam_qparam(req, "cx", v, sizeof(v))) {
        int c = atoi(v); if (c < 0) { c = 0; } if (c > 1000) { c = 1000; }
        g_zoom_cx = c; changed = 1;
    }
    if (cam_qparam(req, "cy", v, sizeof(v))) {
        int c = atoi(v); if (c < 0) { c = 0; } if (c > 1000) { c = 1000; }
        g_zoom_cy = c; changed = 1;
    }
    if (!changed) { cam_http_text(fd, "no change"); return; }
    rc = cam_zoom_apply();
    cam_http_text(fd, (rc == 0) ? "\xe5\xb7\xb2\xe5\xba\x94\xe7\x94\xa8"      /* 已应用 */
                                : "VPSS \xe6\x8b\x92\xe7\xbb\x9d");           /* VPSS 拒绝 */
}

/* GET /hist -> 64 格亮度直方图 (ISP 硬件 1024 bin 归并) */
static void cam_http_hist(int fd)
{
    unsigned int b[HIST_BINS];
    char j[720];
    int n = 0, i;
    if (cam_hist_get(b) != 0) { cam_http_body(fd, "application/json", "{\"bins\":[]}", 11); return; }
    n += snprintf(j + n, sizeof(j) - n, "{\"bins\":[");
    for (i = 0; i < HIST_BINS && n < (int)sizeof(j) - 16; i++) {
        n += snprintf(j + n, sizeof(j) - n, "%s%u", (i > 0) ? "," : "", b[i]);
    }
    n += snprintf(j + n, sizeof(j) - n, "]}");
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
    if (cam_qparam(req, "evf", v, sizeof(v))) {            /* 直方图+拍摄参数叠加 */
        pthread_mutex_lock(&g_cfg_lock);
        g_evf_en = atoi(v) ? 1 : 0;
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
    if (cam_qparam(req, "follow", v, sizeof(v))) {     /* 对焦区跟检测主体走 */
        pthread_mutex_lock(&g_cfg_lock);
        g_focus_follow = atoi(v) ? 1 : 0;
        pthread_mutex_unlock(&g_cfg_lock);
        g_focus_peak = 0;
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
        /* **先算总长, 要么整帧收下要么整帧丢掉**。以前是逐包"塞得下就塞", 塞不下的悄悄跳过 ——
         * 4K JPEG 的头和图像数据是分开的两个包, 于是超长时只剩下头, 变成一张全黑的合法 JPEG。
         * 宁可保留上一张好图, 也不要留一张骗人的黑图。 */
        {
            td_u32 total = 0;
            for (i = 0; i < stream.pack_cnt; i++) {
                total += stream.pack[i].len - stream.pack[i].offset;
            }
            if (total > sizeof(g_snap4k)) {
                if ((g_snap4k_drop++ % 20) == 0) {
                    printf("[snap4k] 一帧 %u 字节装不下 %u 的缓冲, 整帧丢弃(已丢 %d 次) —— "
                           "画面太复杂就会这样, 需要调大 SNAP4K_CAP\n",
                           total, (unsigned)sizeof(g_snap4k), g_snap4k_drop);
                }
            } else {
                pthread_mutex_lock(&g_snap4k_lock);
                g_snap4k_len = 0;
                for (i = 0; i < stream.pack_cnt; i++) {
                    td_u32 l = stream.pack[i].len - stream.pack[i].offset;
                    (td_void)memcpy_s(g_snap4k + g_snap4k_len, sizeof(g_snap4k) - g_snap4k_len,
                        stream.pack[i].addr + stream.pack[i].offset, l);
                    g_snap4k_len += (int)l;
                }
                pthread_mutex_unlock(&g_snap4k_lock);
            }
        }
        ss_mpi_venc_release_stream(chn, &stream);
        free(stream.pack);
    }
    return TD_NULL;
}

/* serve newest 4K JPEG; as_download=1 forces a file download */
static void web_http_snap4k(int fd, int as_download)
{
    static unsigned char sbuf[SNAP4K_CAP];   /* 必须和 g_snap4k 一样大, 否则这里再截断一次 */
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

/* NPU 检测的接口定义在文件末尾的 NPU 模块里 (那边才有 g_npu_box), 这里先声明.
 * 取景辅助(描边/斑马纹)同理 —— 它要用那边的扩展通道生命周期函数 */
static void cam_http_npu(int fd);
static void cam_http_setnpu(int fd, const char *req);
static void cam_http_models(int fd);
static void cam_http_peak(int fd);
static void cam_http_setpeak(int fd, const char *req);

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
                   strstr(req, "GET /npu") != TD_NULL || strstr(req, "GET /setnpu") != TD_NULL ||
                   strstr(req, "GET /peak") != TD_NULL || strstr(req, "GET /setpeak") != TD_NULL ||
                   strstr(req, "GET /models") != TD_NULL || strstr(req, "GET /hist") != TD_NULL ||
                   strstr(req, "GET /exp") != TD_NULL || strstr(req, "GET /setexp") != TD_NULL ||
                   strstr(req, "GET /wb") != TD_NULL || strstr(req, "GET /setwb") != TD_NULL ||
                   strstr(req, "GET /zoom") != TD_NULL || strstr(req, "GET /setzoom") != TD_NULL ||
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
            } else if (strstr(req, "GET /models") != TD_NULL) {
                cam_http_models(fd);
            } else if (strstr(req, "GET /setexp") != TD_NULL) {   /* 必须排在 /exp 和 /set 前 */
                cam_http_setexp(fd, req);
            } else if (strstr(req, "GET /exp") != TD_NULL) {
                cam_http_exp(fd);
            } else if (strstr(req, "GET /hist") != TD_NULL) {
                cam_http_hist(fd);
            } else if (strstr(req, "GET /setpeak") != TD_NULL) {  /* 必须排在 /peak 和 /set 前 */
                cam_http_setpeak(fd, req);
            } else if (strstr(req, "GET /peak") != TD_NULL) {
                cam_http_peak(fd);
            } else if (strstr(req, "GET /setnpu") != TD_NULL) {   /* 必须排在 /npu 和 /set 前 */
                cam_http_setnpu(fd, req);
            } else if (strstr(req, "GET /npu") != TD_NULL) {
                cam_http_npu(fd);
            } else if (strstr(req, "GET /setwb") != TD_NULL) {    /* 必须排在 /wb 和 /set 前 */
                cam_http_setwb(fd, req);
            } else if (strstr(req, "GET /wb") != TD_NULL) {
                cam_http_wb(fd);
            } else if (strstr(req, "GET /setzoom") != TD_NULL) {  /* 必须排在 /zoom 和 /set 前 */
                cam_http_setzoom(fd, req);
            } else if (strstr(req, "GET /zoom") != TD_NULL) {
                cam_http_zoom(fd);
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

/* ===================== NPU 目标检测 (YOLOv8 via svp_acl) =====================
 * 复用板载 NPU (ot_svp_npu.ko 已加载). 通过 VPSS 扩展通道(chn4, 绑 phys chn0)取
 * 模型输入尺寸(640x640)的 YUV420SP 帧, 零拷贝喂给 NPU 推理, 解析内建 NMS 输出的
 * ROI 框. Phase1: 只打印检测框到日志(不画框). 不动现有 4 个物理 venc 通道.
 * common 胶水: sample_common_svp_npu.c + sample_common_svp_npu_model.c (已复制进本目录). */
#include <sys/prctl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>            /* YuNet 解码要 expf/sqrtf (musl 的 libm 在 libc 里, 不用加 -lm) */
#include "ss_mpi_vpss.h"
#include "ss_mpi_vb.h"
#include "ss_mpi_sys_mem.h"
#include "svp_acl.h"
#include "svp_acl_rt.h"
#include "sample_common_svp.h"
#include "sample_common_svp_npu.h"
#include "sample_common_svp_npu_model.h"

#define NPU_EXT_CHN     4                     /* VPSS 扩展通道 (phys=4 之外, ext 4..7) */
#define NPU_DEV_ID      0

#define NPU_MODEL_DIRS 2
static const char * const g_npu_model_dirs[NPU_MODEL_DIRS] = { "/tmp/model", "/opt/model" };

/* 模型路径: /tmp/model 优先(开发期临时试模型), 回退 /opt/model(固化在 flash 里的). */
static const char *cam_npu_model_path(void)
{
    static char path[160];
    int i;
    for (i = 0; i < NPU_MODEL_DIRS; i++) {
        (td_void)snprintf(path, sizeof(path), "%s/%s", g_npu_model_dirs[i], g_npu_model);
        if (access(path, R_OK) == 0) { return path; }
    }
    /* 配置里的模型不在了(比如它原来在掉电即失的 /tmp/model): 挑一个存在的 .om 顶上,
     * 免得重启后检测静默失效 */
    for (i = 0; i < NPU_MODEL_DIRS; i++) {
        DIR *dp = opendir(g_npu_model_dirs[i]);
        struct dirent *de;
        if (dp == TD_NULL) { continue; }
        while ((de = readdir(dp)) != TD_NULL) {
            size_t ln = strlen(de->d_name);
            if (ln < 4 || strcmp(de->d_name + ln - 3, ".om") != 0) { continue; }
            (td_void)snprintf(path, sizeof(path), "%s/%s", g_npu_model_dirs[i], de->d_name);
            printf("[npu] 配置的 %s 找不到, 改用 %s\n", g_npu_model, path);
            (td_void)snprintf(g_npu_model, sizeof(g_npu_model), "%s", de->d_name);
            (td_void)closedir(dp);
            return path;
        }
        (td_void)closedir(dp);
    }
    (td_void)snprintf(path, sizeof(path), "%s/%s", g_npu_model_dirs[NPU_MODEL_DIRS - 1], g_npu_model);
    return path;      /* 一个都没有时报 /opt 路径便于日志定位 */
}

/* ---------- Phase2: 检测结果存储 + 画到画面 (region overlay) ---------- */
#define NPU_MAX_DET 16
#define NPU_KPS 5             /* 关键点上限 (YuNet 给 5 个: 右眼/左眼/鼻尖/右嘴角/左嘴角) */
typedef struct {
    int x0, y0, x1, y1;   /* 千分比坐标 (0..1000), 与分辨率无关, 任意通道/网页都能换算 */
    int cls;              /* COCO 类别号, -1=未知(人脸模型也用 -1) */
    int score;            /* 置信度 0..100, -1=模型没给 */
    int nkp;              /* 关键点个数, 0=该模型不给 */
    int kx[NPU_KPS], ky[NPU_KPS];   /* 关键点, 同样是千分比 */
} cam_npu_box;
/* 模型类别: 厂商 .om 自带 RPN 检测输出层(X/X_ 一对, NMS 在片上做);
 * YuNet 这类自己转的裸输出模型要在 A55 上解码 + 自己做 NMS. */
#define NPU_KIND_RPN    0
#define NPU_KIND_YUNET  1
static int g_npu_kind = NPU_KIND_RPN;
static cam_npu_box g_npu_box[NPU_MAX_DET];
static int g_npu_box_n = 0;
static int g_npu_layout_logged = 0;
static pthread_mutex_t g_npu_lock = PTHREAD_MUTEX_INITIALIZER;   /* 保护 g_npu_box / g_npu_box_n */
/* 目标只在部分帧上被检出(实测约 1/5 帧), 直接跟着清会让框狂闪 ——
 * 检出后保持 NPU_HOLD_MS 毫秒再清, 画面才稳. */
#define NPU_HOLD_MS 600
static unsigned long long g_npu_hit_ms = 0;

/* COCO 80 类 (yolov8.om 的类别顺序) */
static const char * const g_coco[80] = {
    "person","bicycle","car","motorcycle","airplane","bus","train","truck","boat","traffic light",
    "fire hydrant","stop sign","parking meter","bench","bird","cat","dog","horse","sheep","cow",
    "elephant","bear","zebra","giraffe","backpack","umbrella","handbag","tie","suitcase","frisbee",
    "skis","snowboard","sports ball","kite","baseball bat","baseball glove","skateboard","surfboard",
    "tennis racket","bottle","wine glass","cup","fork","knife","spoon","bowl","banana","apple",
    "sandwich","orange","broccoli","carrot","hot dog","pizza","donut","cake","chair","couch",
    "potted plant","bed","dining table","toilet","tv","laptop","mouse","remote","keyboard","cell phone",
    "microwave","oven","toaster","sink","refrigerator","book","clock","vase","scissors","teddy bear",
    "hair drier","toothbrush"
};
static const char *cam_coco_name(int cls)
{
    if (cls >= 0 && cls < (int)(sizeof(g_coco) / sizeof(g_coco[0]))) { return g_coco[cls]; }
    return "object";
}
/* 目标名: 人脸模型没有 COCO 类别, 统一叫 face */
static const char *cam_det_name(int cls)
{
    if (g_npu_kind == NPU_KIND_YUNET) { return "face"; }
    return cam_coco_name(cls);
}
/* 每类一个颜色 (ARGB1555, 最高位=不透明) */
static const unsigned short g_det_color[8] = {
    0x83E0, /* 绿 */ 0xFFE0, /* 黄 */ 0x83FF, /* 青 */ 0xFC1F, /* 品红 */
    0xFE80, /* 橙 */ 0x801F, /* 蓝 */ 0xFFFF, /* 白 */ 0xFC00  /* 红 */
};

/* 8x16 1-bit font, ASCII 32..126 (DejaVuSansMono-Bold 14px) — 检测标签用.
 * 与时间戳那张 13 字形表分开: 时间戳已固化验证过, 不动它. */
static const unsigned char g_txt_font[95][16] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /*   */
  {0x00,0x00,0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00}, /* ! */
  {0x00,0x00,0x00,0x26,0x26,0x26,0x26,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* " */
  {0x00,0x00,0x00,0x1b,0x1a,0x12,0x7f,0x36,0x24,0xff,0x6c,0x6c,0x48,0x00,0x00,0x00}, /* # */
  {0x00,0x00,0x08,0x08,0x3e,0x6a,0x68,0x7c,0x1e,0x0e,0x0a,0x6e,0x3c,0x08,0x08,0x00}, /* $ */
  {0x00,0x00,0x00,0x70,0xd8,0xd8,0x73,0x0c,0x30,0x47,0x09,0x09,0x07,0x00,0x00,0x00}, /* % */
  {0x00,0x00,0x00,0x3c,0x30,0x30,0x30,0x78,0x79,0xcd,0xcf,0x67,0x3f,0x00,0x00,0x00}, /* & */
  {0x00,0x00,0x00,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ' */
  {0x00,0x00,0x0c,0x0c,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x0c,0x0c,0x00,0x00}, /* ( */
  {0x00,0x00,0x30,0x18,0x18,0x18,0x0c,0x0c,0x0c,0x0c,0x18,0x18,0x18,0x30,0x00,0x00}, /* ) */
  {0x00,0x00,0x00,0x08,0x4a,0x3c,0x3c,0x4a,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* * */
  {0x00,0x00,0x00,0x00,0x18,0x18,0x18,0xff,0xff,0x18,0x18,0x18,0x00,0x00,0x00,0x00}, /* + */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x10,0x00,0x00}, /* , */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3c,0x3c,0x00,0x00,0x00,0x00,0x00,0x00}, /* - */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x00,0x00,0x00}, /* . */
  {0x00,0x00,0x00,0x02,0x06,0x06,0x04,0x0c,0x08,0x18,0x10,0x30,0x20,0x60,0x60,0x00}, /* / */
  {0x00,0x00,0x00,0x3c,0x76,0x66,0x67,0x6f,0x6f,0x67,0x66,0x76,0x3c,0x00,0x00,0x00}, /* 0 */
  {0x00,0x00,0x00,0x3c,0x7c,0x1c,0x1c,0x1c,0x1c,0x1c,0x1c,0x1c,0x7f,0x00,0x00,0x00}, /* 1 */
  {0x00,0x00,0x00,0x3c,0x46,0x06,0x06,0x0e,0x0c,0x18,0x30,0x70,0x7e,0x00,0x00,0x00}, /* 2 */
  {0x00,0x00,0x00,0x3c,0x46,0x06,0x06,0x1c,0x06,0x06,0x06,0x46,0x3c,0x00,0x00,0x00}, /* 3 */
  {0x00,0x00,0x00,0x0e,0x1e,0x1e,0x36,0x66,0x66,0x7f,0x06,0x06,0x06,0x00,0x00,0x00}, /* 4 */
  {0x00,0x00,0x00,0x7e,0x60,0x60,0x7c,0x46,0x06,0x07,0x06,0x46,0x3c,0x00,0x00,0x00}, /* 5 */
  {0x00,0x00,0x00,0x1c,0x32,0x60,0x7c,0x76,0x63,0x63,0x63,0x76,0x3c,0x00,0x00,0x00}, /* 6 */
  {0x00,0x00,0x00,0x7e,0x06,0x06,0x0e,0x0c,0x1c,0x18,0x18,0x38,0x30,0x00,0x00,0x00}, /* 7 */
  {0x00,0x00,0x00,0x3c,0x66,0x66,0x66,0x3c,0x66,0x63,0x63,0x66,0x3c,0x00,0x00,0x00}, /* 8 */
  {0x00,0x00,0x00,0x3c,0x66,0x66,0x66,0x67,0x67,0x3e,0x06,0x46,0x3c,0x00,0x00,0x00}, /* 9 */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00,0x00,0x00}, /* : */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x10,0x00,0x00}, /* ; */
  {0x00,0x00,0x00,0x00,0x00,0x03,0x0f,0x7c,0x60,0x78,0x0f,0x03,0x00,0x00,0x00,0x00}, /* < */
  {0x00,0x00,0x00,0x00,0x00,0x7f,0x7f,0x00,0x00,0x7f,0x7f,0x00,0x00,0x00,0x00,0x00}, /* = */
  {0x00,0x00,0x00,0x00,0x00,0x40,0x78,0x1e,0x07,0x1e,0x78,0x40,0x00,0x00,0x00,0x00}, /* > */
  {0x00,0x00,0x00,0x3c,0x26,0x06,0x0e,0x1c,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00}, /* ? */
  {0x00,0x00,0x00,0x1e,0x62,0x43,0xcf,0xdb,0xd1,0xd1,0xdb,0xcf,0x40,0x33,0x1f,0x00}, /* @ */
  {0x00,0x00,0x00,0x1c,0x1c,0x3c,0x34,0x36,0x66,0x7e,0x63,0x63,0xc3,0x00,0x00,0x00}, /* A */
  {0x00,0x00,0x00,0x7e,0x66,0x67,0x66,0x7e,0x66,0x63,0x63,0x67,0x7e,0x00,0x00,0x00}, /* B */
  {0x00,0x00,0x00,0x1e,0x30,0x70,0x60,0x60,0x60,0x60,0x70,0x30,0x1e,0x00,0x00,0x00}, /* C */
  {0x00,0x00,0x00,0x7c,0x66,0x66,0x63,0x63,0x63,0x63,0x66,0x66,0x7c,0x00,0x00,0x00}, /* D */
  {0x00,0x00,0x00,0x7f,0x60,0x60,0x60,0x7e,0x60,0x60,0x60,0x60,0x7f,0x00,0x00,0x00}, /* E */
  {0x00,0x00,0x00,0x7f,0x60,0x60,0x60,0x7e,0x60,0x60,0x60,0x60,0x60,0x00,0x00,0x00}, /* F */
  {0x00,0x00,0x00,0x1e,0x30,0x60,0x60,0x60,0x67,0x63,0x63,0x33,0x1e,0x00,0x00,0x00}, /* G */
  {0x00,0x00,0x00,0x67,0x67,0x67,0x67,0x7f,0x67,0x67,0x67,0x67,0x67,0x00,0x00,0x00}, /* H */
  {0x00,0x00,0x00,0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x7e,0x00,0x00,0x00}, /* I */
  {0x00,0x00,0x00,0x3e,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x4e,0x3c,0x00,0x00,0x00}, /* J */
  {0x00,0x00,0x00,0x63,0x66,0x6c,0x78,0x78,0x7c,0x6e,0x66,0x67,0x63,0x00,0x00,0x00}, /* K */
  {0x00,0x00,0x00,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x7f,0x00,0x00,0x00}, /* L */
  {0x00,0x00,0x00,0x67,0x77,0x77,0x7f,0x5b,0x5b,0x43,0x43,0x43,0x43,0x00,0x00,0x00}, /* M */
  {0x00,0x00,0x00,0x63,0x73,0x73,0x73,0x7b,0x6b,0x6f,0x6f,0x67,0x67,0x00,0x00,0x00}, /* N */
  {0x00,0x00,0x00,0x3c,0x66,0x67,0x63,0x63,0x63,0x63,0x67,0x66,0x3c,0x00,0x00,0x00}, /* O */
  {0x00,0x00,0x00,0x7e,0x67,0x63,0x63,0x67,0x7e,0x60,0x60,0x60,0x60,0x00,0x00,0x00}, /* P */
  {0x00,0x00,0x00,0x3c,0x66,0x67,0x63,0x63,0x63,0x63,0x67,0x66,0x3e,0x06,0x02,0x00}, /* Q */
  {0x00,0x00,0x00,0x7c,0x66,0x66,0x67,0x66,0x7c,0x6e,0x66,0x67,0x63,0x00,0x00,0x00}, /* R */
  {0x00,0x00,0x00,0x3c,0x62,0x60,0x70,0x7c,0x1e,0x07,0x07,0x46,0x3c,0x00,0x00,0x00}, /* S */
  {0x00,0x00,0x00,0x7f,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00}, /* T */
  {0x00,0x00,0x00,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x66,0x3e,0x00,0x00,0x00}, /* U */
  {0x00,0x00,0x00,0xe3,0x63,0x67,0x66,0x66,0x36,0x36,0x3c,0x3c,0x1c,0x00,0x00,0x00}, /* V */
  {0x00,0x00,0x00,0xc1,0xc3,0xc3,0xdb,0xdb,0x5f,0x7f,0x77,0x76,0x66,0x00,0x00,0x00}, /* W */
  {0x00,0x00,0x00,0xe3,0x66,0x36,0x3c,0x1c,0x1c,0x3c,0x36,0x66,0xe3,0x00,0x00,0x00}, /* X */
  {0x00,0x00,0x00,0xe3,0x67,0x76,0x3e,0x3c,0x1c,0x18,0x18,0x18,0x18,0x00,0x00,0x00}, /* Y */
  {0x00,0x00,0x00,0x7f,0x07,0x0e,0x0e,0x1c,0x38,0x38,0x70,0x70,0x7f,0x00,0x00,0x00}, /* Z */
  {0x00,0x00,0x1c,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x1c,0x00,0x00}, /* [ */
  {0x00,0x00,0x00,0x60,0x60,0x20,0x30,0x10,0x18,0x08,0x0c,0x0c,0x06,0x06,0x02,0x00}, /* \ */
  {0x00,0x00,0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0x00,0x00}, /* ] */
  {0x00,0x00,0x00,0x18,0x3c,0x76,0x63,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ^ */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff}, /* _ */
  {0x00,0x30,0x10,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ` */
  {0x00,0x00,0x00,0x00,0x00,0x3e,0x46,0x07,0x3f,0x67,0x67,0x67,0x3f,0x00,0x00,0x00}, /* a */
  {0x00,0x00,0x60,0x60,0x60,0x7e,0x76,0x63,0x63,0x63,0x63,0x76,0x7e,0x00,0x00,0x00}, /* b */
  {0x00,0x00,0x00,0x00,0x00,0x1e,0x32,0x60,0x60,0x60,0x60,0x32,0x1e,0x00,0x00,0x00}, /* c */
  {0x00,0x00,0x06,0x06,0x06,0x3e,0x66,0x66,0x66,0x66,0x66,0x66,0x3e,0x00,0x00,0x00}, /* d */
  {0x00,0x00,0x00,0x00,0x00,0x3c,0x66,0x63,0x7f,0x60,0x60,0x71,0x3e,0x00,0x00,0x00}, /* e */
  {0x00,0x00,0x0e,0x18,0x18,0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00}, /* f */
  {0x00,0x00,0x00,0x00,0x00,0x3f,0x67,0x67,0x67,0x67,0x67,0x67,0x3f,0x06,0x06,0x3c}, /* g */
  {0x00,0x00,0x60,0x60,0x60,0x7e,0x76,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00}, /* h */
  {0x00,0x00,0x1c,0x1c,0x00,0x7c,0x1c,0x1c,0x1c,0x1c,0x1c,0x1c,0x7f,0x00,0x00,0x00}, /* i */
  {0x00,0x00,0x0c,0x0c,0x00,0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x1c,0x78}, /* j */
  {0x00,0x00,0x60,0x60,0x60,0x67,0x6e,0x7c,0x7c,0x7c,0x66,0x66,0x63,0x00,0x00,0x00}, /* k */
  {0x00,0x00,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x0f,0x00,0x00,0x00}, /* l */
  {0x00,0x00,0x00,0x00,0x00,0xfe,0xdb,0xdb,0xdb,0xdb,0xdb,0xdb,0xdb,0x00,0x00,0x00}, /* m */
  {0x00,0x00,0x00,0x00,0x00,0x7e,0x76,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00}, /* n */
  {0x00,0x00,0x00,0x00,0x00,0x3c,0x66,0x67,0x63,0x63,0x67,0x66,0x3c,0x00,0x00,0x00}, /* o */
  {0x00,0x00,0x00,0x00,0x00,0x7e,0x76,0x63,0x63,0x63,0x63,0x76,0x7e,0x60,0x60,0x60}, /* p */
  {0x00,0x00,0x00,0x00,0x00,0x3e,0x66,0x66,0x66,0x66,0x66,0x66,0x3e,0x06,0x06,0x06}, /* q */
  {0x00,0x00,0x00,0x00,0x00,0x3f,0x39,0x30,0x30,0x30,0x30,0x30,0x30,0x00,0x00,0x00}, /* r */
  {0x00,0x00,0x00,0x00,0x00,0x3c,0x62,0x70,0x7c,0x1e,0x06,0x46,0x3c,0x00,0x00,0x00}, /* s */
  {0x00,0x00,0x00,0x18,0x18,0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0x1e,0x00,0x00,0x00}, /* t */
  {0x00,0x00,0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3e,0x00,0x00,0x00}, /* u */
  {0x00,0x00,0x00,0x00,0x00,0x63,0x66,0x66,0x36,0x36,0x3c,0x3c,0x1c,0x00,0x00,0x00}, /* v */
  {0x00,0x00,0x00,0x00,0x00,0xc1,0xc3,0xdb,0x5b,0x7f,0x77,0x76,0x66,0x00,0x00,0x00}, /* w */
  {0x00,0x00,0x00,0x00,0x00,0x67,0x36,0x3c,0x1c,0x1c,0x3c,0x76,0x67,0x00,0x00,0x00}, /* x */
  {0x00,0x00,0x00,0x00,0x00,0x63,0x67,0x66,0x36,0x3e,0x3c,0x1c,0x1c,0x18,0x18,0x70}, /* y */
  {0x00,0x00,0x00,0x00,0x00,0x7e,0x06,0x0e,0x1c,0x38,0x38,0x70,0x7e,0x00,0x00,0x00}, /* z */
  {0x00,0x00,0x0e,0x18,0x18,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x18,0x0e,0x00}, /* { */
  {0x00,0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18}, /* | */
  {0x00,0x00,0x70,0x18,0x18,0x18,0x18,0x1c,0x0e,0x1c,0x18,0x18,0x18,0x18,0x78,0x00}, /* } */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x79,0x7f,0x0e,0x00,0x00,0x00,0x00,0x00,0x00}, /* ~ */
};

/* chn2(720p 预览 = 网页/MJPEG/DSI 屏那一路)的唯一叠加层, 整幅画布.
 * 同一 venc 通道上的 overlay region 不能重叠, 所以时间戳/对焦/检测框都合并画在这里. */
#define DET_HANDLE 2
#define DET_W      1280
#define DET_H      720
#define DET_OX     16              /* 时间戳在画面里的左上角偏移(和原来 region 位置一致) */
#define DET_OY     16
#define DET_TXT_SCALE 2                    /* 8x16 字体放大 2 倍 = 16x32, 720p 上够看 */
static int g_det_rgn_ready = 0;

/* 峰值对焦描边 / 过曝斑马纹的掩膜.
 * 在 VPSS 扩展通道那帧(640x360 的 Y 平面)上算, 1 bit/像素打包 —— 一张掩膜才 28.8KB,
 * 画到 1280x720 叠加层时正好 1 个 bit 铺 2x2 像素。
 * 写者 = 视觉线程(cam_vis_analyze), 读者 = OSD 线程(cam_vis_blit), 用 g_vis_lock 隔开;
 * 算的时候先写线程私有的暂存区, 只有整块 memcpy 那一下才持锁, 免得 OSD 等太久。 */
#define VIS_MASK_W      640
#define VIS_MASK_H      360
#define VIS_MASK_STRIDE (VIS_MASK_W / 8)
#define VIS_MASK_BYTES  (VIS_MASK_STRIDE * VIS_MASK_H)
static unsigned char g_peak_mask[VIS_MASK_BYTES];
static unsigned char g_zebra_mask[VIS_MASK_BYTES];
static int g_vis_mw = 0, g_vis_mh = 0;     /* 掩膜里实际有效的宽高(0=还没算过) */
static pthread_mutex_t g_vis_lock = PTHREAD_MUTEX_INITIALIZER;

static void cam_det_px(unsigned char *base, int stride, int cw, int ch, int x, int y, unsigned short c)
{
    if (x < 0 || y < 0 || x >= cw || y >= ch) { return; }   /* 越界丢弃, 框贴边也安全 */
    *((unsigned short *)(base + (long)y * stride) + x) = c;
}

/* 空心矩形: 从 (x0,y0)-(x1,y1) 往外长 th 像素粗 */
static void cam_det_rect(unsigned char *base, int stride, int cw, int ch,
    int x0, int y0, int x1, int y1, int th, unsigned short c)
{
    int i, t;
    for (t = 0; t < th; t++) {
        for (i = x0 - t; i <= x1 + t; i++) {
            cam_det_px(base, stride, cw, ch, i, y0 - t, c);
            cam_det_px(base, stride, cw, ch, i, y1 + t, c);
        }
        for (i = y0 - t; i <= y1 + t; i++) {
            cam_det_px(base, stride, cw, ch, x0 - t, i, c);
            cam_det_px(base, stride, cw, ch, x1 + t, i, c);
        }
    }
}

/* 写一行 ASCII 文本 (8x16 字体 × scale), 带画布裁剪 */
static void cam_txt_blit(unsigned char *base, int stride, int cw, int ch,
    int x, int y, const char *s, int scale, unsigned short color)
{
    int i, row, col, sy, sx;
    for (i = 0; s[i] != '\0'; i++) {
        int code = (unsigned char)s[i];
        const unsigned char *gl;
        if (code < 32 || code > 126) { code = 32; }
        gl = g_txt_font[code - 32];
        for (row = 0; row < 16; row++) {
            unsigned char bits = gl[row];
            if (bits == 0) { continue; }
            for (col = 0; col < 8; col++) {
                if (!(bits & (0x80 >> col))) { continue; }
                for (sy = 0; sy < scale; sy++) {
                    for (sx = 0; sx < scale; sx++) {
                        cam_det_px(base, stride, cw, ch,
                            x + (i * 8 + col) * scale + sx, y + row * scale + sy, color);
                    }
                }
            }
        }
    }
}

/* 建 chn2 合成层 (handle 2, 整幅 720p, 贴 venc chn2). 非致命: 失败就只是没叠加. */
static void cam_chn2_rgn_init(void)
{
    ot_rgn_attr ra;
    ot_rgn_chn_attr ca;
    ot_mpp_chn chn;
    (td_void)memset_s(&ra, sizeof(ra), 0, sizeof(ra));
    ra.type = OT_RGN_OVERLAY;
    ra.attr.overlay.pixel_format = OT_PIXEL_FORMAT_ARGB_1555;
    ra.attr.overlay.bg_color = 0x0000;          /* 透明 */
    ra.attr.overlay.size.width = DET_W;
    ra.attr.overlay.size.height = DET_H;
    ra.attr.overlay.canvas_num = 2;
    if (ss_mpi_rgn_create(DET_HANDLE, &ra) != TD_SUCCESS) { printf("[npu] det rgn_create fail\n"); return; }
    (td_void)memset_s(&ca, sizeof(ca), 0, sizeof(ca));
    ca.is_show = TD_TRUE;
    ca.type = OT_RGN_OVERLAY;
    ca.attr.overlay_chn.point.x = 0;
    ca.attr.overlay_chn.point.y = 0;
    ca.attr.overlay_chn.fg_alpha = 255;
    ca.attr.overlay_chn.bg_alpha = 0;
    ca.attr.overlay_chn.layer = 0;              /* chn2 上只有这一层, 不与别的 region 重叠 */
    chn.mod_id = OT_ID_VENC; chn.dev_id = 0; chn.chn_id = 2;
    if (ss_mpi_rgn_attach_to_chn(DET_HANDLE, &chn, &ca) != TD_SUCCESS) {
        printf("[osd] chn2 rgn attach fail\n");
        (td_void)ss_mpi_rgn_destroy(DET_HANDLE);
        return;
    }
    g_det_rgn_ready = 1;
    printf("[osd] chn2 overlay up (%dx%d: 时间戳+对焦+检测框)\n", DET_W, DET_H);
}

/* 把峰值/斑马纹掩膜铺到叠加层. 一个 bit -> sx*sy 个像素 (640x360 -> 1280x720 即 2x2).
 * 两者重叠时描边优先(描边是判断合焦的, 别被斑马纹盖住). */
static void cam_vis_blit(unsigned char *base, int stride)
{
    /* 描边颜色 ARGB1555: 红/黄/青/白 */
    static const unsigned short peak_col[4] = { 0xFC00, 0xFFE0, 0x83FF, 0xFFFF };
    unsigned short pc;
    int mw, mh, sx, sy, r, c, dx, dy;
    int peak = g_peak_en, zeb = g_zebra_en;
    if (!peak && !zeb) { return; }
    pc = peak_col[g_peak_color & 3];
    pthread_mutex_lock(&g_vis_lock);
    mw = g_vis_mw; mh = g_vis_mh;
    if (mw > 0 && mh > 0) {
        sx = DET_W / mw; sy = DET_H / mh;
        if (sx < 1) { sx = 1; }
        if (sy < 1) { sy = 1; }
        for (r = 0; r < mh; r++) {
            const unsigned char *pr = g_peak_mask + (size_t)r * VIS_MASK_STRIDE;
            const unsigned char *zr = g_zebra_mask + (size_t)r * VIS_MASK_STRIDE;
            for (c = 0; c < mw; c++) {
                unsigned char bit = (unsigned char)(0x80 >> (c & 7));
                int on_p = peak && (pr[c >> 3] & bit);
                int on_z = zeb && (zr[c >> 3] & bit);
                unsigned short col;
                int px, py;
                if (!on_p && !on_z) { continue; }
                col = on_p ? pc : 0xFFFF;          /* 斑马纹白色斜纹 */
                px = c * sx; py = r * sy;
                for (dy = 0; dy < sy; dy++) {
                    unsigned short *row = (unsigned short *)(base + (size_t)(py + dy) * stride);
                    for (dx = 0; dx < sx; dx++) { row[px + dx] = col; }
                }
            }
        }
    }
    pthread_mutex_unlock(&g_vis_lock);
}

/* 对焦测量区: 跟主体(检测框最大的那个) > 中心块 > 全画幅.
 * 返回 0=中心/全幅, 1=跟主体框, 2=跟眼睛(人脸模型给了关键点时自动升级为眼控对焦) */
static int cam_focus_zone(int *pr0, int *pr1, int *pc0, int *pc1)
{
    int r0 = FOCUS_SPOT_R0, r1 = FOCUS_SPOT_R1, c0 = FOCUS_SPOT_C0, c1 = FOCUS_SPOT_C1;
    int follow = 0;
    if (g_focus_follow) {
        cam_npu_box b = {0};
        int i, best = -1;
        long long area = 0;
        int bx0, by0, bx1, by1;
        pthread_mutex_lock(&g_npu_lock);
        for (i = 0; i < g_npu_box_n && i < NPU_MAX_DET; i++) {   /* 主体 = 面积最大的框 */
            long long a = (long long)(g_npu_box[i].x1 - g_npu_box[i].x0) *
                          (g_npu_box[i].y1 - g_npu_box[i].y0);
            if (a > area) { area = a; best = i; }
        }
        if (best >= 0) { b = g_npu_box[best]; }
        pthread_mutex_unlock(&g_npu_lock);
        if (best >= 0 && area > 0) {
            bx0 = b.x0; by0 = b.y0; bx1 = b.x1; by1 = b.y1;
            follow = 1;
            /* 眼控对焦: 有两只眼睛的关键点就只测眼睛那一小块 —— 人像该合焦的是眼睛,
             * 用整张脸去测, 焦点会被鼻尖/头发拉走. 窗口 = 双眼间距, 且不小于脸宽的 1/3
             * (AF 统计只有 17x15 格, 太小的窗口取不到格子). */
            if (b.nkp >= 2) {
                int ex = (b.kx[0] + b.kx[1]) / 2, ey = (b.ky[0] + b.ky[1]) / 2;
                int dx = (b.kx[0] > b.kx[1]) ? (b.kx[0] - b.kx[1]) : (b.kx[1] - b.kx[0]);
                int hw = dx, hh = dx / 2;
                int minw = (bx1 - bx0) / 3, minh = (by1 - by0) / 4;
                if (hw < minw) { hw = minw; }
                if (hh < minh) { hh = minh; }
                if (hw < 30) { hw = 30; }        /* 至少 3% 画幅 */
                if (hh < 30) { hh = 30; }
                bx0 = ex - hw; bx1 = ex + hw;
                by0 = ey - hh; by1 = ey + hh;
                if (bx0 < 0) { bx0 = 0; }
                if (by0 < 0) { by0 = 0; }
                if (bx1 > 1000) { bx1 = 1000; }
                if (by1 > 1000) { by1 = 1000; }
                follow = 2;
            }
            /* 千分比 -> AF 统计网格 (15 行 x 17 列) */
            c0 = bx0 * OT_ISP_AF_ZONE_COLUMN / 1000;
            c1 = bx1 * OT_ISP_AF_ZONE_COLUMN / 1000;
            r0 = by0 * OT_ISP_AF_ZONE_ROW / 1000;
            r1 = by1 * OT_ISP_AF_ZONE_ROW / 1000;
            if (c1 >= OT_ISP_AF_ZONE_COLUMN) { c1 = OT_ISP_AF_ZONE_COLUMN - 1; }
            if (r1 >= OT_ISP_AF_ZONE_ROW) { r1 = OT_ISP_AF_ZONE_ROW - 1; }
            if (c1 < c0) { c1 = c0; }
            if (r1 < r0) { r1 = r0; }
        }
    }
    if (!follow && !g_focus_spot) {
        r0 = 0; r1 = OT_ISP_AF_ZONE_ROW - 1; c0 = 0; c1 = OT_ISP_AF_ZONE_COLUMN - 1;
    }
    *pr0 = r0; *pr1 = r1; *pc0 = c0; *pc1 = c1;
    return follow;
}

/* 重画 chn2 叠加层: 时间戳 + 焦点值/对焦条 + 中心对焦框 + 检测框. 由 OSD 线程单独调用
 * (单一写者, 免得和推理线程抢 region 画布). 全清后重画, 关掉的项自然消失. */
/* ===================== 取景界面 (EVF 风格) =====================
 * 把直方图和拍摄参数**画进叠加层**而不是网页 —— 这样网页预览 / MJPEG 流 / DSI 屏 /
 * /snap 出来的图全都自带, 是同一份画面。"预览和屏幕一样"就是这么来的。
 * 全部画在 chn2 那张唯一的全幅 region 上(同一通道的 region 不能重叠, 这是硬约束),
 * 写者只有 OSD 线程一个。 */
#define EVF_HIST_W  256
#define EVF_HIST_H  80
#define EVF_PAD     16

/* 微秒 -> 相机习惯的快门写法 */
static void cam_shutter_txt(int us, char *out, int cap)
{
    if (us <= 0) { (td_void)snprintf(out, (size_t)cap, "--"); return; }
    if (us >= 1000000) { (td_void)snprintf(out, (size_t)cap, "%.1fs", us / 1000000.0); return; }
    (td_void)snprintf(out, (size_t)cap, "1/%d", (int)(1000000.0 / us + 0.5));
}

/* 实心矩形填充(直方图底板用) */
static void cam_evf_fill(unsigned char *base, int stride, int cw, int ch,
                         int x0, int y0, int x1, int y1, unsigned short c)
{
    int x, y;
    for (y = y0; y < y1; y++) {
        if (y < 0 || y >= ch) { continue; }
        for (x = x0; x < x1; x++) {
            if (x < 0 || x >= cw) { continue; }
            cam_det_px(base, stride, cw, ch, x, y, c);
        }
    }
}

static void cam_evf_draw(unsigned char *base, int stride)
{
    static unsigned int bins[HIST_BINS];
    cam_exp_now e;
    cam_wb_now w;
    char line[96], sh[16];
    int hx = DET_W - EVF_HIST_W - EVF_PAD;
    int hy = DET_H - EVF_HIST_H - EVF_PAD;
    int i;

    /* --- 直方图: 右下角, 黑底白柱, 最右 3% 段标红(过曝警示, 和网页那版一致) --- */
    if (cam_hist_get(bins) == 0) {
        unsigned int mx = 1;
        int hi = HIST_BINS * 97 / 100;
        for (i = 0; i < HIST_BINS; i++) { if (bins[i] > mx) { mx = bins[i]; } }
        cam_evf_fill(base, stride, DET_W, DET_H, hx - 2, hy - 2,
                     hx + EVF_HIST_W + 2, hy + EVF_HIST_H + 2, 0x8000);
        for (i = 0; i < HIST_BINS; i++) {
            int bw = EVF_HIST_W / HIST_BINS;
            int h = (int)((unsigned long long)bins[i] * (EVF_HIST_H - 2) / mx);
            unsigned short c = (i >= hi) ? 0xFC00 : 0xFFFF;   /* 高光段红 */
            if (h < 1 && bins[i] > 0) { h = 1; }
            cam_evf_fill(base, stride, DET_W, DET_H, hx + i * bw, hy + EVF_HIST_H - h,
                         hx + i * bw + bw - 1, hy + EVF_HIST_H, c);
        }
    }

    /* --- 拍摄参数: 左下角一行, 和相机取景器一个意思 --- */
    cam_exp_read(&e);
    cam_wb_read(&w);
    cam_shutter_txt(e.us, sh, sizeof(sh));
    (td_void)snprintf(line, sizeof(line), "%s %s ISO%d %s%dK",
        (g_exp_manual == 2) ? "M" : ((g_exp_manual == 1) ? "S" : "P"),
        sh, (e.iso >= 0) ? e.iso : 0,
        g_wb_manual ? "WB" : "AWB",
        (g_wb_manual == 1) ? g_wb_temp : ((w.temp > 0) ? w.temp : 0));
    {
        int ty = DET_H - EVF_PAD - 16 * DET_TXT_SCALE;
        cam_txt_blit(base, stride, DET_W, DET_H, EVF_PAD + 2, ty + 2, line, DET_TXT_SCALE, 0x8000);
        cam_txt_blit(base, stride, DET_W, DET_H, EVF_PAD, ty, line, DET_TXT_SCALE, 0xFFFF);
    }
}

static void cam_chn2_draw(void)
{
    ot_rgn_canvas_info ci;
    cam_npu_box b[NPU_MAX_DET];
    unsigned char *base;
    char buf[32];
    int stride, i, n;
    if (!g_det_rgn_ready) { return; }
    pthread_mutex_lock(&g_npu_lock);
    n = g_npu_box_n;
    (td_void)memcpy_s(b, sizeof(b), g_npu_box, sizeof(g_npu_box));
    pthread_mutex_unlock(&g_npu_lock);
    if (ss_mpi_rgn_get_canvas_info(DET_HANDLE, &ci) != TD_SUCCESS) { return; }
    base = (unsigned char *)ci.virt_addr;
    stride = (int)ci.stride;
    (td_void)memset_s(base, (size_t)stride * ci.size.height, 0, (size_t)stride * ci.size.height);

    cam_vis_blit(base, stride);      /* 先铺描边/斑马纹, 文字和框画在它上面 */

    if (g_evf_en) { cam_evf_draw(base, stride); }   /* 直方图 + 拍摄参数 */

    if (g_osd_en) {                                   /* 时间戳(和主码流那层同样式) */
        time_t t = time(TD_NULL);
        struct tm lt;
        localtime_r(&t, &lt);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
        cam_txt_blit(base, stride, DET_W, DET_H, DET_OX + 2, DET_OY + 2, buf, OSD_SCALE, 0x8000);
        cam_txt_blit(base, stride, DET_W, DET_H, DET_OX, DET_OY, buf, OSD_SCALE, 0xFFFF);
    }
    if (g_osd_focus_en) {                             /* 焦点值 + 对焦条 */
        long long fv = cam_focus_value();
        double frac;
        if (fv < 0) { fv = 0; }
        if ((unsigned long long)fv > g_focus_peak) { g_focus_peak = (unsigned long long)fv; }
        (td_void)snprintf(buf, sizeof(buf), "%lld", fv);
        cam_txt_blit(base, stride, DET_W, DET_H, DET_OX + 2, DET_OY + OSD_L2_Y + 2, buf, OSD_SCALE, 0x8000);
        cam_txt_blit(base, stride, DET_W, DET_H, DET_OX, DET_OY + OSD_L2_Y, buf, OSD_SCALE, 0xFFFF);
        frac = (g_focus_peak > 0) ? (double)fv / (double)g_focus_peak : 0.0;
        cam_osd_bar(base, stride, DET_OX, DET_OY + OSD_BAR_Y, OSD_W, OSD_BAR_H, frac);
        if (g_focus_spot || g_focus_follow) {          /* 把"正在测哪块"画出来(跟主体时会跟着动) */
            int fr0, fr1, fc0, fc1;
            int fol = cam_focus_zone(&fr0, &fr1, &fc0, &fc1);
            int x0 = fc0 * DET_W / OT_ISP_AF_ZONE_COLUMN, x1 = (fc1 + 1) * DET_W / OT_ISP_AF_ZONE_COLUMN;
            int y0 = fr0 * DET_H / OT_ISP_AF_ZONE_ROW,    y1 = (fr1 + 1) * DET_H / OT_ISP_AF_ZONE_ROW;
            /* 白=中心块, 黄=跟主体框, 青=眼控(测的是眼睛那一小块) */
            unsigned short fc = (fol == 2) ? 0x83FF : (fol ? 0xFFE0 : 0xFFFF);
            cam_det_rect(base, stride, DET_W, DET_H, x0, y0, x1, y1, 2, fc);
            cam_det_rect(base, stride, DET_W, DET_H, x0 - 2, y0 - 2, x1 + 2, y1 + 2, 1, 0x8000);
        }
    }
    if (g_npu_en && g_npu_draw) {
        /* 标签最高只能到这里, 免得压在左上角的时间戳/对焦读数上 */
        int tymin = g_osd_en ? (DET_OY + OSD_LH + 4) : 0;
        if (g_osd_focus_en) { tymin = DET_OY + OSD_BAR_Y + OSD_BAR_H + 4; }
        for (i = 0; i < n && i < NPU_MAX_DET; i++) {
            unsigned short c = (g_npu_kind == NPU_KIND_YUNET) ? 0x83FF     /* 人脸统一青色 */
                             : g_det_color[(b[i].cls < 0 ? 0 : b[i].cls) & 7];
            int x0 = b[i].x0 * DET_W / 1000, x1 = b[i].x1 * DET_W / 1000;
            int y0 = b[i].y0 * DET_H / 1000, y1 = b[i].y1 * DET_H / 1000;
            int ty = y0 - 16 * DET_TXT_SCALE - 4;
            char lab[40];
            int k;
            if (x1 <= x0 || y1 <= y0) { continue; }
            cam_det_rect(base, stride, DET_W, DET_H, x0, y0, x1, y1, 2, c);
            cam_det_rect(base, stride, DET_W, DET_H, x0 - 2, y0 - 2, x1 + 2, y1 + 2, 1, 0x8000);
            for (k = 0; k < b[i].nkp && k < NPU_KPS; k++) {   /* 关键点: 眼睛画大点, 其它小点 */
                int px = b[i].kx[k] * DET_W / 1000, py = b[i].ky[k] * DET_H / 1000;
                int rr = (k < 2) ? 3 : 1;
                unsigned short kc = (k < 2) ? 0xFFE0 : 0xFFFF;   /* 双眼=黄 */
                cam_det_rect(base, stride, DET_W, DET_H, px - rr, py - rr, px + rr, py + rr, 2, kc);
            }
            if (b[i].score >= 0) {
                (td_void)snprintf(lab, sizeof(lab), "%s %d%%", cam_det_name(b[i].cls), b[i].score);
            } else {
                (td_void)snprintf(lab, sizeof(lab), "%s", cam_det_name(b[i].cls));
            }
            /* 贴顶时标签画到框内; 再往下让开时间戳那条(否则叠在一起看不清) */
            if (ty < tymin) { ty = y0 + 2; }
            if (ty < tymin) { ty = tymin; }
            cam_txt_blit(base, stride, DET_W, DET_H, x0 + 2, ty + 2, lab, DET_TXT_SCALE, 0x8000);
            cam_txt_blit(base, stride, DET_W, DET_H, x0, ty, lab, DET_TXT_SCALE, c);
        }
    }
    (td_void)ss_mpi_rgn_update_canvas(DET_HANDLE);
}

/* 输出张量名: 默认 yolov8 的, 载入模型后自动探测覆盖(见 cam_npu_detect_names).
 * 各模型规律 = 计数张量 X + 坐标张量 X_ (yolov5/v8: output0, yolov7: output,
 * yolov3/4: detection_nms_2 …), 所以按"配对"找即可, 不用写死型号表. */
static char g_npu_num_name[64] = "output0";
static char g_npu_roi_name[64] = "output0_";
static char g_npu_rpn_name[] = "rpn_data";    /* 阈值输入张量名 (只有 RPN 检测层的模型才有) */
/* YuNet(自转的裸输出人脸模型): 三个尺度各 4 个输出 cls_N/obj_N/bbox_N/kps_N.
 * 解码 + NMS 全在 A55 上做, 见 cam_npu_parse_yunet. */
#define YUNET_SCALES 3
static const int g_yunet_stride[YUNET_SCALES] = { 8, 16, 32 };
typedef struct { size_t cls, obj, bbox, kps; int ok; } cam_yunet_out;
static cam_yunet_out g_yunet[YUNET_SCALES];
static volatile int g_npu_stop = 0;
static pthread_t g_npu_th;                    /* 推理线程, 换模型时要 join */
static int g_npu_th_run = 0;
static int g_acl_inited = 0;                  /* svp acl 只初始化一次(换模型不重来) */
static sample_svp_npu_task_info g_npu_task = {0};
static sample_svp_npu_detection_info g_npu_det = {0};
static ot_size g_npu_insize = {0};
static void *g_npu_vb_virt = TD_NULL;
static ot_vb_pool_info g_npu_vb_pool = {0};
/* letterbox: 4K 是 16:9, 模型输入是正方形(640x640). 直接缩过去会横向压扁 1.78 倍
 * (灯就被认成牙刷了). 所以扩展通道只出 640x360 等比画面, 拷进模型输入缓冲区正中间,
 * 上下留灰边; 框坐标再按内容区反算回去. 代价 = 每次推理 345KB memcpy (~1ms). */
static td_u8 *g_npu_in_virt = TD_NULL;    /* 模型自带的输入缓冲区(svp_acl_rt_malloc, NPU 可 DMA) */
static td_u32 g_npu_in_size = 0, g_npu_in_stride = 0;
static int g_npu_cw = 0, g_npu_ch = 0;    /* 内容区宽高 (640x360) */
static int g_npu_lb_y = 0;                /* 内容区在模型输入里的 y 偏移 (140) */

/* 载入模型后自动认出 ROI 输出张量名: 输出里成对出现 X / X_ (X_=坐标, X=计数).
 * 认不出就沿用默认(yolov8 的 output0/output0_)并打日志. */
/* YuNet 识别: 输出里出现 cls_8/obj_8/bbox_8/kps_8 这套名字就按裸输出人脸模型解码.
 * 三个尺度只要有一个齐全就算数(缺的那尺度跳过), 返回 1 = 是 YuNet. */
static int cam_npu_try_yunet(const char **names, size_t n)
{
    char nm[16];
    int s, got = 0;
    size_t i;
    (td_void)memset_s(g_yunet, sizeof(g_yunet), 0, sizeof(g_yunet));
    for (s = 0; s < YUNET_SCALES; s++) {
        int have = 0;
        for (i = 0; i < n; i++) {
            if (names[i] == TD_NULL) { continue; }
            (td_void)snprintf(nm, sizeof(nm), "cls_%d", g_yunet_stride[s]);
            if (!strcmp(names[i], nm)) { g_yunet[s].cls = i; have |= 1; }
            (td_void)snprintf(nm, sizeof(nm), "obj_%d", g_yunet_stride[s]);
            if (!strcmp(names[i], nm)) { g_yunet[s].obj = i; have |= 2; }
            (td_void)snprintf(nm, sizeof(nm), "bbox_%d", g_yunet_stride[s]);
            if (!strcmp(names[i], nm)) { g_yunet[s].bbox = i; have |= 4; }
            (td_void)snprintf(nm, sizeof(nm), "kps_%d", g_yunet_stride[s]);
            if (!strcmp(names[i], nm)) { g_yunet[s].kps = i; have |= 8; }
        }
        if ((have & 7) == 7) {                 /* cls+obj+bbox 必须齐, kps 可缺 */
            g_yunet[s].ok = (have & 8) ? 2 : 1;   /* 2 = 带关键点(能眼控对焦) */
            got++;
        }
    }
    return (got > 0);
}

static void cam_npu_detect_names(void)
{
    sample_svp_npu_model_info *mi = sample_common_svp_npu_get_model_info(0);
    const char *names[16];
    size_t n, i, k;
    if (mi == TD_NULL || mi->model_desc == TD_NULL) { return; }
    n = svp_acl_mdl_get_num_outputs(mi->model_desc);
    if (n > sizeof(names) / sizeof(names[0])) { n = sizeof(names) / sizeof(names[0]); }
    for (i = 0; i < n; i++) {
        names[i] = svp_acl_mdl_get_output_name_by_index(mi->model_desc, i);
        printf("[npu] 输出%zu: %s\n", i, (names[i] != TD_NULL) ? names[i] : "(null)");
    }
    if (cam_npu_try_yunet(names, n)) {
        g_npu_kind = NPU_KIND_YUNET;
        printf("[npu] 识别为 YuNet 人脸模型 (裸输出, 板上解码+NMS): 尺度 8=%d 16=%d 32=%d "
               "(2=带5关键点)\n", g_yunet[0].ok, g_yunet[1].ok, g_yunet[2].ok);
        return;
    }
    g_npu_kind = NPU_KIND_RPN;
    for (i = 0; i < n; i++) {
        size_t li;
        if (names[i] == TD_NULL) { continue; }
        li = strlen(names[i]);
        for (k = 0; k < n; k++) {                    /* 找 names[k] == names[i] + "_" */
            if (k == i || names[k] == TD_NULL) { continue; }
            if (strlen(names[k]) == li + 1 && names[k][li] == '_' && !strncmp(names[i], names[k], li)) {
                (td_void)snprintf(g_npu_num_name, sizeof(g_npu_num_name), "%s", names[i]);
                (td_void)snprintf(g_npu_roi_name, sizeof(g_npu_roi_name), "%s", names[k]);
                printf("[npu] 张量名: 计数=%s 坐标=%s (自动识别)\n", g_npu_num_name, g_npu_roi_name);
                return;
            }
        }
    }
    printf("[npu] 未识别出配对张量名, 沿用 %s/%s\n", g_npu_num_name, g_npu_roi_name);
}

/* ---- VPSS 扩展通道 (chn4) 的生命周期 ----
 * 这一路帧现在有三个消费者: NPU 检测、峰值对焦描边、过曝斑马纹。所以通道不再归 NPU 独有,
 * 谁先要谁开, 全都不要了才关。绑 phys chn0(4K主), TAIL, 等比缩到 w x h, YUV420SP, depth=1.
 * (不是缩到 640x640 —— 那样会压扁; 补边在 cam_npu_letterbox 里做) */
static int g_vis_chn_w = 0, g_vis_chn_h = 0;    /* 当前扩展通道输出尺寸, 0 = 没开 */

static int cam_vis_chn_start(int w, int h)
{
    ot_vpss_ext_chn_attr ext;
    td_s32 ret;
    if (w <= 0 || h <= 0) { return -1; }
    if (g_vis_chn_w == w && g_vis_chn_h == h) { return 0; }      /* 已经是这个尺寸, 幂等 */
    if (g_vis_chn_w != 0) {                                      /* 尺寸变了(换了输入不同的模型): 拆了重建 */
        printf("[vis] 扩展通道 %dx%d -> %dx%d, 重建\n", g_vis_chn_w, g_vis_chn_h, w, h);
        (td_void)ss_mpi_vpss_disable_chn(0, NPU_EXT_CHN);
        g_vis_chn_w = 0; g_vis_chn_h = 0;
    }
    (td_void)memset_s(&ext, sizeof(ext), 0, sizeof(ext));
    ext.bind_chn      = 0;
    ext.src_type      = OT_EXT_CHN_SRC_TYPE_TAIL;
    ext.width         = (td_u32)w;
    ext.height        = (td_u32)h;
    ext.depth         = 1;
    ext.video_format  = OT_VIDEO_FORMAT_LINEAR;
    ext.dynamic_range = OT_DYNAMIC_RANGE_SDR8;
    ext.pixel_format  = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    ext.compress_mode = OT_COMPRESS_MODE_NONE;
    ext.frame_rate.src_frame_rate = -1;
    ext.frame_rate.dst_frame_rate = -1;
    ret = ss_mpi_vpss_set_ext_chn_attr(0, NPU_EXT_CHN, &ext);
    if (ret != TD_SUCCESS) { printf("[vis] set_ext_chn_attr failed %#x\n", ret); return -1; }
    ret = ss_mpi_vpss_enable_chn(0, NPU_EXT_CHN);
    if (ret != TD_SUCCESS) { printf("[vis] enable ext chn failed %#x\n", ret); return -1; }
    g_vis_chn_w = w; g_vis_chn_h = h;
    return 0;
}

static void cam_vis_chn_stop(void)
{
    if (g_vis_chn_w != 0) {
        (td_void)ss_mpi_vpss_disable_chn(0, NPU_EXT_CHN);
        g_vis_chn_w = 0; g_vis_chn_h = 0;
    }
}

/* 初始化: acl init -> 载模型 -> 取输入分辨率 -> 建 dataset/task/work buf -> 设阈值.
 * 失败按序回滚, 返回 -1. 全程非致命(相机不受影响). */
static int cam_npu_init(void)
{
    td_s32 ret;
    sample_svp_npu_threshold th;
    const char *model_path = cam_npu_model_path();

    if (!g_acl_inited) {
        ret = sample_common_svp_npu_acl_init("", NPU_DEV_ID);
        if (ret != TD_SUCCESS) { printf("[npu] acl init failed %#x\n", ret); return -1; }
        g_acl_inited = 1;
    }

    ret = sample_common_svp_npu_load_model(model_path, 0, TD_FALSE);
    if (ret != TD_SUCCESS) { printf("[npu] load model %s failed %#x\n", model_path, ret); return -1; }
    printf("[npu] model = %s\n", model_path);
    cam_npu_detect_names();

    ret = sample_common_svp_npu_get_input_resolution(0, 0, &g_npu_insize);
    if (ret != TD_SUCCESS) {
        printf("[npu] get input resolution failed %#x\n", ret);
        (td_void)sample_common_svp_npu_unload_model(0);
        return -1;
    }
    printf("[npu] model input = %ux%u\n", g_npu_insize.width, g_npu_insize.height);

    g_npu_task.cfg.max_batch_num     = 1;
    g_npu_task.cfg.dynamic_batch_num = 1;
    g_npu_task.cfg.total_t           = 0;
    g_npu_task.cfg.is_cached         = TD_FALSE;
    g_npu_task.cfg.model_idx         = 0;

    ret = sample_common_svp_npu_create_input(&g_npu_task);
    if (ret != TD_SUCCESS) {
        printf("[npu] create input failed %#x\n", ret);
        (td_void)sample_common_svp_npu_unload_model(0);
        return -1;
    }

    /* 拿到模型输入缓冲区并铺灰底(Y=114/UV=128, 和 YOLO letterbox 的惯例一致).
     * 内容区 = 等比缩到模型宽的 16:9 画面, 垂直居中. */
    if (sample_common_svp_npu_get_input_data_buffer_info(&g_npu_task, 0,
            &g_npu_in_virt, &g_npu_in_size, &g_npu_in_stride) != TD_SUCCESS || g_npu_in_virt == TD_NULL) {
        printf("[npu] get input buffer failed\n"); goto err_in;
    }
    g_npu_cw = (int)g_npu_insize.width;
    g_npu_ch = ((int)g_npu_insize.width * 9 / 16) & (~1);        /* 640 -> 360, 偶数 */
    if (g_npu_ch > (int)g_npu_insize.height) { g_npu_ch = (int)g_npu_insize.height; }
    g_npu_lb_y = (((int)g_npu_insize.height - g_npu_ch) / 2) & (~1);
    (td_void)memset_s(g_npu_in_virt, g_npu_in_size, 114,
        (size_t)g_npu_in_stride * g_npu_insize.height);          /* Y 平面 = 灰 */
    (td_void)memset_s(g_npu_in_virt + (size_t)g_npu_in_stride * g_npu_insize.height,
        g_npu_in_size - (size_t)g_npu_in_stride * g_npu_insize.height, 128,
        (size_t)g_npu_in_stride * g_npu_insize.height / 2);      /* UV 平面 = 中性 */
    printf("[npu] letterbox: 内容 %dx%d 居中于 %ux%u (y偏移 %d), 输入缓冲 %u 字节 stride %u\n",
        g_npu_cw, g_npu_ch, g_npu_insize.width, g_npu_insize.height, g_npu_lb_y,
        g_npu_in_size, g_npu_in_stride);
    /* 我们是按 YUV420SP 直接零拷贝喂的, 所以输入缓冲必须是 stride*h*3/2.
     * 转模型时忘了加 AIPP 的话输入会是 float32 RGB (stride 变 4 倍, size = stride*h*3),
     * 喂进去不会崩但结果全是垃圾 —— 这里明确报出来, 免得又花一晚上查"为什么检不出". */
    if (g_npu_in_size != (td_u32)((size_t)g_npu_in_stride * g_npu_insize.height * 3 / 2)) {
        printf("[npu] ⚠ 输入缓冲 %u 字节 ≠ YUV420SP 应有的 %u —— 这个 .om 多半没带 AIPP"
               "(输入是 float32/RGB), 检测结果不可信, 请用 --insert_op_conf 重转\n",
            g_npu_in_size, (td_u32)((size_t)g_npu_in_stride * g_npu_insize.height * 3 / 2));
    }
    ret = sample_common_svp_npu_create_output(&g_npu_task);
    if (ret != TD_SUCCESS) { printf("[npu] create output failed %#x\n", ret); goto err_in; }
    ret = sample_common_svp_npu_create_task_buf(&g_npu_task);
    if (ret != TD_SUCCESS) { printf("[npu] create task buf failed %#x\n", ret); goto err_out; }
    ret = sample_common_svp_npu_create_work_buf(&g_npu_task);
    if (ret != TD_SUCCESS) { printf("[npu] create work buf failed %#x\n", ret); goto err_taskbuf; }

    /* 阈值张量 "rpn_data": nms 用 g_npu_nms(可 web 调, 默认 0.45);
     * score 留 0.10 让模型多吐候选, 置信度门槛在我们自己解析时按 g_npu_score 卡 —— 这样
     * 调门槛不用重新初始化模型.
     * 只有带 RPN 检测输出层的模型(厂商那批 yolo .om)才有这个输入张量; YuNet 这种裸输出
     * 模型没有, 设失败很正常 —— 必须非致命, 否则会把能用的模型当坏模型踢掉. */
    if (g_npu_kind == NPU_KIND_RPN) {
        (td_void)memset_s(&th, sizeof(th), 0, sizeof(th));
        th.nms_threshold = (float)g_npu_nms / 100.0f; th.score_threshold = 0.10f;
        th.min_height = 1.0f; th.min_width = 1.0f; th.name = g_npu_rpn_name;
        ret = sample_common_svp_npu_set_threshold(&th, 1, &g_npu_task);
        if (ret != TD_SUCCESS) {
            printf("[npu] set threshold failed %#x (没有 %s 输入张量? 忽略, 用解析端门槛)\n",
                ret, g_npu_rpn_name);
        }
    } else {
        printf("[npu] 裸输出模型: NMS 在 A55 上做 (交并比 %d%%)\n", g_npu_nms);
    }

    g_npu_det.num_name = g_npu_num_name;
    g_npu_det.roi_name = g_npu_roi_name;
    g_npu_det.has_background = TD_FALSE;
    g_npu_det.roi_offset = 0;
    return 0;

err_taskbuf: (td_void)sample_common_svp_npu_destroy_task_buf(&g_npu_task);
err_out:     (td_void)sample_common_svp_npu_destroy_output(&g_npu_task);
err_in:      (td_void)sample_common_svp_npu_destroy_input(&g_npu_task);
             (td_void)sample_common_svp_npu_unload_model(0);
    return -1;      /* acl 保持已初始化状态, 供下次换模型复用 */
}

/* 卸载当前模型(停线程 + 释放 buf + 关扩展通道), 保留 acl. 换模型/关检测都走这里. */
static void cam_npu_teardown(void)
{
    if (g_npu_th_run) {
        g_npu_stop = 1;
        (td_void)pthread_join(g_npu_th, TD_NULL);   /* 线程最多 2s 内退出(取帧超时) */
        g_npu_th_run = 0;
    }
    g_npu_ready = 0;
    /* 通道不是 NPU 独有的了: 描边/斑马纹还要帧就留着(换模型时它俩只会断一下下) */
    if (!g_peak_en && !g_zebra_en) { cam_vis_chn_stop(); }
    (td_void)sample_common_svp_npu_destroy_work_buf(&g_npu_task);
    (td_void)sample_common_svp_npu_destroy_task_buf(&g_npu_task);
    (td_void)sample_common_svp_npu_destroy_output(&g_npu_task);
    (td_void)sample_common_svp_npu_destroy_input(&g_npu_task);
    (td_void)sample_common_svp_npu_unload_model(0);
    g_npu_in_virt = TD_NULL;
    g_npu_vb_virt = TD_NULL;        /* 新模型输入尺寸可能不同, VB 池重新映射 */
    g_npu_layout_logged = 0;
    pthread_mutex_lock(&g_npu_lock);
    g_npu_box_n = 0; g_npu_last_num = 0;
    pthread_mutex_unlock(&g_npu_lock);
}

/* 模型输入坐标(含灰边) -> 内容区千分比 */
static int cam_lb_x(td_float x)
{
    int v = (g_npu_cw > 0) ? (int)(x * 1000.0f / (float)g_npu_cw) : 0;
    return (v < 0) ? 0 : ((v > 1000) ? 1000 : v);
}
static int cam_lb_y(td_float y)
{
    int v = (g_npu_ch > 0) ? (int)((y - (float)g_npu_lb_y) * 1000.0f / (float)g_npu_ch) : 0;
    return (v < 0) ? 0 : ((v > 1000) ? 1000 : v);
}

/* 类别白名单: g_npu_cls 是逗号分隔的类别号, 空串=全要. 逐次解析字符串够快(每帧最多十几次) */
static int cam_npu_cls_ok(int cls)
{
    const char *p = g_npu_cls;
    if (p[0] == '\0') { return 1; }
    if (cls < 0) { return 0; }
    while (*p != '\0') {
        if (*p >= '0' && *p <= '9') {
            int v = atoi(p);
            if (v == cls) { return 1; }
            while (*p >= '0' && *p <= '9') { p++; }
        } else {
            p++;
        }
    }
    return 0;
}

/* 把解析好的一批框提交给 g_npu_box (加保持时间, 免得框闪). 各解析器共用. */
static void cam_npu_commit(const cam_npu_box *box, int j)
{
    unsigned long long now = sample_common_svp_npu_get_timestamp();   /* ms */
    pthread_mutex_lock(&g_npu_lock);
    if (j > 0) {                                                 /* 有目标: 换上新的一批 */
        (td_void)memcpy_s(g_npu_box, sizeof(g_npu_box), box, sizeof(cam_npu_box) * NPU_MAX_DET);
        g_npu_box_n = j;
        g_npu_last_num = j;
        g_npu_hit_ms = now;
    } else if (g_npu_box_n > 0 && now - g_npu_hit_ms > NPU_HOLD_MS) {
        g_npu_box_n = 0;                                         /* 空窗超过保持时间才清 */
        g_npu_last_num = 0;
    }
    pthread_mutex_unlock(&g_npu_lock);
}

/* 解析一次推理结果 -> g_npu_box (千分比坐标 + 类别 + 置信度). 返回目标数, 失败 -1.
 * .om 里已带 NMS. 实测本模型 (yolov8.om): num 张量 'output0' 末维=1 = 目标总数;
 * roi 张量 'output0_' 有 6 "行", 行间隔 stride 字节:
 *   行0..3 = xmin/ymin/xmax/ymax (模型输入 640 空间), 行4 = 置信度 0..1, 行5 = 类别号.
 * 行数/类别数都从 buffer_size/stride 和 dims 现场算, 不写死 —— 换模型也不会跑飞. */
static int cam_npu_parse_rpn(void)
{
    sample_svp_npu_model_info *mi = sample_common_svp_npu_get_model_info(0);
    svp_acl_mdl_io_dims dims;
    svp_acl_data_buffer *nb = TD_NULL, *rb = TD_NULL;
    size_t num_idx = 0, roi_idx = 0, rstride, rsize, rows, fs;
    td_float *nd = TD_NULL, *rd = TD_NULL, *xmin, *ymin, *xmax, *ymax, *score, *clsrow;
    cam_npu_box box[NPU_MAX_DET] = {{0}};
    int ncls, c, i, j = 0, total = 0;

    if (mi == TD_NULL || mi->model_desc == TD_NULL) { return -1; }
    if (svp_acl_mdl_get_output_index_by_name(mi->model_desc, g_npu_num_name, &num_idx) != SVP_ACL_SUCCESS ||
        svp_acl_mdl_get_output_index_by_name(mi->model_desc, g_npu_roi_name, &roi_idx) != SVP_ACL_SUCCESS) {
        return -1;
    }
    if (svp_acl_mdl_get_output_dims(mi->model_desc, num_idx, &dims) != SVP_ACL_SUCCESS) { return -1; }
    ncls = (dims.dim_count > 0) ? (int)dims.dims[dims.dim_count - 1] : 0;
    nb = svp_acl_mdl_get_dataset_buffer(g_npu_task.output_dataset, num_idx);
    rb = svp_acl_mdl_get_dataset_buffer(g_npu_task.output_dataset, roi_idx);
    if (nb == TD_NULL || rb == TD_NULL || ncls <= 0) { return -1; }
    nd = (td_float *)svp_acl_get_data_buffer_addr(nb);
    rd = (td_float *)svp_acl_get_data_buffer_addr(rb);
    rstride = svp_acl_get_data_buffer_stride(rb);
    rsize = svp_acl_get_data_buffer_size(rb);
    if (nd == TD_NULL || rd == TD_NULL || rstride == 0) { return -1; }
    rows = rsize / rstride;
    fs = rstride / sizeof(td_float);
    if (rows < 4) { return -1; }
    xmin = rd; ymin = rd + fs; xmax = rd + 2 * fs; ymax = rd + 3 * fs;
    score  = (rows >= 5) ? (rd + 4 * fs) : TD_NULL;
    clsrow = (rows >= 6) ? (rd + 5 * fs) : TD_NULL;

    for (c = 0; c < ncls; c++) {
        int cnt = (int)nd[c];                  /* 末维=1 时就是总数; 若某模型按类分组也照样吃 */
        for (i = 0; i < cnt; i++, total++) {
            int cls, sc;
            if (j >= NPU_MAX_DET) { continue; }
            sc = (score != TD_NULL) ? (int)(score[total] * 100.0f + 0.5f) : -1;
            if (sc >= 0 && sc < g_npu_score) { continue; }   /* 低置信度的丢掉 */
            if (clsrow != TD_NULL) {
                cls = (int)(clsrow[total] + 0.5f);           /* 行5 = 类别号 */
                if (cls < 0 || cls >= (int)(sizeof(g_coco) / sizeof(g_coco[0]))) { cls = -1; }
            } else {
                cls = (ncls > 1) ? c : -1;                   /* 退路: 按类分组的 num 张量 */
            }
            if (!cam_npu_cls_ok(cls)) { continue; }           /* 类别白名单 */
            /* 坐标在模型输入(含灰边)空间 -> 换算到内容区千分比 */
            box[j].x0 = cam_lb_x(xmin[total]);
            box[j].y0 = cam_lb_y(ymin[total]);
            box[j].x1 = cam_lb_x(xmax[total]);
            box[j].y1 = cam_lb_y(ymax[total]);
            box[j].cls = cls;
            box[j].score = (sc > 100) ? 100 : sc;
            j++;
        }
    }

    cam_npu_commit(box, j);

    if (!g_npu_layout_logged && total > 0) {   /* 有目标时才打, 否则看不到样本值 */
        g_npu_layout_logged = 1;
        printf("[npu] 输出布局: num='%s' 末维=%d | roi='%s' 行数=%d stride=%d | 置信度=%s 类别=%s\n",
            g_npu_num_name, ncls, g_npu_roi_name, (int)rows, (int)rstride,
            (score != TD_NULL) ? "行4" : "无", (clsrow != TD_NULL) ? "行5" : "无");
        printf("[npu] det0 原始值: x0=%.1f y0=%.1f x1=%.1f y1=%.1f 行4=%.3f 行5=%.3f\n",
            xmin[0], ymin[0], xmax[0], ymax[0],
            (score != TD_NULL) ? score[0] : -1.0f, (clsrow != TD_NULL) ? clsrow[0] : -1.0f);
    }
    return j;                                  /* 过完门槛后剩下的目标数 */
}

/* ===== YuNet(自转的裸输出人脸模型) 解码 =====
 * 三尺度 stride 8/16/32, 每个格点一个候选(无 anchor):
 *   score = sqrt(clamp(cls) * clamp(obj))       —— sigmoid 已在模型里做完
 *   cx = (列 + bbox0) * s    cy = (行 + bbox1) * s
 *   w  = exp(bbox2) * s      h  = exp(bbox3) * s
 *   关键点 kx = (列 + kps[2k]) * s, ky = (行 + kps[2k+1]) * s
 *     顺序 = 右眼 / 左眼 / 鼻尖 / 右嘴角 / 左嘴角 —— 前两个就是眼控对焦要的
 * (= OpenCV FaceDetectorYN 对 opencv_zoo 2023mar 权重的解码方式.) NMS 自己做. */

/* 输出张量取值器: NPU 的输出可能是「单行连续」「每格点一行」「每通道一行」三种摆法,
 * 现场用 size/stride/格点数 判出来, 别写死 —— 换个 atc 版本布局就可能变. */
typedef struct {
    const td_float *d;
    size_t fs;        /* 一行有多少个 float */
    int rows;         /* 行数 = size / stride */
    int per;          /* 每个格点几个数 (cls/obj=1, bbox=4, kps=10) */
    int layout;       /* 0=单行连续, 1=每格点一行, 2=每通道一行 */
} cam_tensor;

static int cam_npu_tensor(size_t idx, int na, int per, cam_tensor *t)
{
    svp_acl_data_buffer *b = svp_acl_mdl_get_dataset_buffer(g_npu_task.output_dataset, idx);
    size_t stride, size;
    if (b == TD_NULL) { return -1; }
    t->d = (const td_float *)svp_acl_get_data_buffer_addr(b);
    stride = svp_acl_get_data_buffer_stride(b);
    size = svp_acl_get_data_buffer_size(b);
    if (t->d == TD_NULL || stride == 0) { return -1; }
    t->fs = stride / sizeof(td_float);
    t->rows = (int)(size / stride);
    t->per = per;
    if (t->rows <= 1) {
        t->layout = 0;
        if (t->fs < (size_t)na * (size_t)per) { return -1; }      /* 数据不够, 宁可不解析 */
    } else if (t->rows >= na) {
        t->layout = 1;
        if (t->fs < (size_t)per) { return -1; }
    } else {
        t->layout = 2;
        if (t->rows < per || t->fs < (size_t)na) { return -1; }
    }
    return 0;
}

static td_float cam_t_get(const cam_tensor *t, int i, int k)
{
    if (t->layout == 0) { return t->d[(size_t)i * (size_t)t->per + (size_t)k]; }
    if (t->layout == 1) { return t->d[(size_t)i * t->fs + (size_t)k]; }
    return t->d[(size_t)k * t->fs + (size_t)i];
}

#define YUNET_CAND 64          /* NMS 前保留的候选上限 */
typedef struct {
    td_float x0, y0, x1, y1, sc;
    td_float kx[NPU_KPS], ky[NPU_KPS];
    int nkp;
} cam_face;

static td_float cam_face_iou(const cam_face *a, const cam_face *b)
{
    td_float ix0 = (a->x0 > b->x0) ? a->x0 : b->x0;
    td_float iy0 = (a->y0 > b->y0) ? a->y0 : b->y0;
    td_float ix1 = (a->x1 < b->x1) ? a->x1 : b->x1;
    td_float iy1 = (a->y1 < b->y1) ? a->y1 : b->y1;
    td_float iw = ix1 - ix0, ih = iy1 - iy0, inter, uni;
    if (iw <= 0.0f || ih <= 0.0f) { return 0.0f; }
    inter = iw * ih;
    uni = (a->x1 - a->x0) * (a->y1 - a->y0) + (b->x1 - b->x0) * (b->y1 - b->y0) - inter;
    return (uni > 0.0f) ? (inter / uni) : 0.0f;
}

static int cam_npu_parse_yunet(void)
{
    static cam_face cand[YUNET_CAND];     /* 4KB 级, 别放线程栈 */
    static int used[YUNET_CAND];
    cam_npu_box box[NPU_MAX_DET] = {{0}};
    int mw = (int)g_npu_insize.width, mh = (int)g_npu_insize.height;
    td_float thr = (td_float)g_npu_score / 100.0f;
    td_float iou_th = (td_float)g_npu_nms / 100.0f;
    int nc = 0, s, i, k, j = 0;

    if (mw <= 0 || mh <= 0) { return -1; }
    for (s = 0; s < YUNET_SCALES; s++) {
        cam_tensor tc, to, tb, tk;
        int st = g_yunet_stride[s];
        int cols = mw / st, rws = mh / st, na = cols * rws;
        int has_kp;
        if (g_yunet[s].ok == 0 || na <= 0) { continue; }
        has_kp = (g_yunet[s].ok == 2);
        if (cam_npu_tensor(g_yunet[s].cls,  na, 1, &tc) != 0) { continue; }
        if (cam_npu_tensor(g_yunet[s].obj,  na, 1, &to) != 0) { continue; }
        if (cam_npu_tensor(g_yunet[s].bbox, na, 4, &tb) != 0) { continue; }
        if (has_kp && cam_npu_tensor(g_yunet[s].kps, na, NPU_KPS * 2, &tk) != 0) { has_kp = 0; }
        if (!g_npu_layout_logged) {
            printf("[npu] yunet 尺度%d: 格点 %dx%d=%d, cls行数=%d layout=%d, bbox行数=%d layout=%d, 关键点=%d\n",
                st, cols, rws, na, tc.rows, tc.layout, tb.rows, tb.layout, has_kp);
        }
        for (i = 0; i < na; i++) {
            td_float cs = cam_t_get(&tc, i, 0), os = cam_t_get(&to, i, 0), sc;
            td_float cx, cy, w, h;
            int col = i % cols, row = i / cols, slot;
            if (cs < 0.0f) { cs = 0.0f; } else if (cs > 1.0f) { cs = 1.0f; }
            if (os < 0.0f) { os = 0.0f; } else if (os > 1.0f) { os = 1.0f; }
            sc = sqrtf(cs * os);
            if (sc < thr) { continue; }
            cx = ((td_float)col + cam_t_get(&tb, i, 0)) * (td_float)st;
            cy = ((td_float)row + cam_t_get(&tb, i, 1)) * (td_float)st;
            w  = expf(cam_t_get(&tb, i, 2)) * (td_float)st;
            h  = expf(cam_t_get(&tb, i, 3)) * (td_float)st;
            /* 反着写: NaN 参与比较全是 false, 这样脏数据也会被挡掉而不是漏过去 */
            if (!(w > 1.0f && h > 1.0f && w <= (td_float)(mw * 2) && h <= (td_float)(mh * 2))) { continue; }
            slot = nc;
            if (nc >= YUNET_CAND) {                    /* 候选满了: 顶掉分最低的 */
                int worst = 0;
                for (k = 1; k < YUNET_CAND; k++) {
                    if (cand[k].sc < cand[worst].sc) { worst = k; }
                }
                if (cand[worst].sc >= sc) { continue; }
                slot = worst;
            } else {
                nc++;
            }
            cand[slot].x0 = cx - w / 2.0f; cand[slot].y0 = cy - h / 2.0f;
            cand[slot].x1 = cx + w / 2.0f; cand[slot].y1 = cy + h / 2.0f;
            cand[slot].sc = sc;
            cand[slot].nkp = 0;
            if (has_kp) {
                for (k = 0; k < NPU_KPS; k++) {
                    cand[slot].kx[k] = ((td_float)col + cam_t_get(&tk, i, 2 * k))     * (td_float)st;
                    cand[slot].ky[k] = ((td_float)row + cam_t_get(&tk, i, 2 * k + 1)) * (td_float)st;
                }
                cand[slot].nkp = NPU_KPS;
            }
        }
    }
    g_npu_layout_logged = 1;     /* 布局信息第一遍就全了(不像 RPN 要等有目标), 打一次就够 */

    /* NMS: 每轮挑当前最高分, 把和它重叠超过阈值的都压掉 */
    for (i = 0; i < nc; i++) { used[i] = 0; }
    while (j < NPU_MAX_DET) {
        int best = -1;
        td_float bs = 0.0f;
        for (i = 0; i < nc; i++) {
            if (!used[i] && cand[i].sc > bs) { bs = cand[i].sc; best = i; }
        }
        if (best < 0) { break; }
        used[best] = 1;
        for (i = 0; i < nc; i++) {
            if (used[i]) { continue; }
            if (cam_face_iou(&cand[best], &cand[i]) > iou_th) { used[i] = 1; }
        }
        box[j].x0 = cam_lb_x(cand[best].x0);       /* 模型输入(含灰边)空间 -> 内容区千分比 */
        box[j].y0 = cam_lb_y(cand[best].y0);
        box[j].x1 = cam_lb_x(cand[best].x1);
        box[j].y1 = cam_lb_y(cand[best].y1);
        box[j].cls = -1;                           /* 人脸没有 COCO 类别 */
        box[j].score = (int)(cand[best].sc * 100.0f + 0.5f);
        if (box[j].score > 100) { box[j].score = 100; }
        box[j].nkp = cand[best].nkp;
        for (k = 0; k < cand[best].nkp && k < NPU_KPS; k++) {
            box[j].kx[k] = cam_lb_x(cand[best].kx[k]);
            box[j].ky[k] = cam_lb_y(cand[best].ky[k]);
        }
        j++;
    }
    cam_npu_commit(box, j);
    return j;
}

/* 按模型类别分派 */
static int cam_npu_parse(void)
{
    if (g_npu_kind == NPU_KIND_YUNET) { return cam_npu_parse_yunet(); }
    return cam_npu_parse_rpn();
}

/* 把扩展通道的一帧(640x360 YUV420SP)拷进模型输入缓冲区的内容区, 上下灰边不动.
 * 灰边只在初始化时铺一次 —— 内容区每帧覆盖, 边框区永远是灰的, 不用重铺. */
static td_s32 cam_npu_letterbox(const ot_video_frame_info *f)
{
    const ot_video_frame *v = &f->video_frame;
    td_u8 *sy, *suv, *dy, *duv;
    int r, sstride, cw, ch;
    if (g_npu_in_virt == TD_NULL || g_npu_vb_virt == TD_NULL) { return TD_FAILURE; }
    sstride = (int)v->stride[0];
    cw = ((int)v->width < g_npu_cw) ? (int)v->width : g_npu_cw;
    ch = ((int)v->height < g_npu_ch) ? (int)v->height : g_npu_ch;
    sy  = (td_u8 *)g_npu_vb_virt + (v->phys_addr[0] - g_npu_vb_pool.pool_phy_addr);
    suv = (td_u8 *)g_npu_vb_virt + (v->phys_addr[1] - g_npu_vb_pool.pool_phy_addr);
    dy  = g_npu_in_virt + (size_t)g_npu_lb_y * g_npu_in_stride;
    duv = g_npu_in_virt + (size_t)g_npu_in_stride * g_npu_insize.height
                        + (size_t)(g_npu_lb_y / 2) * g_npu_in_stride;
    for (r = 0; r < ch; r++) {
        (td_void)memcpy_s(dy + (size_t)r * g_npu_in_stride, (size_t)cw, sy + (size_t)r * sstride, (size_t)cw);
    }
    for (r = 0; r < ch / 2; r++) {          /* UV 交织平面, 行数是 Y 的一半 */
        (td_void)memcpy_s(duv + (size_t)r * g_npu_in_stride, (size_t)cw, suv + (size_t)r * sstride, (size_t)cw);
    }
    return TD_SUCCESS;
}

/* ===== 峰值对焦描边 + 过曝斑马纹 =====
 * 在扩展通道那帧的 Y 平面上直接算(不用再要一路帧, 也不用碰编码通道):
 *   描边 = 简单梯度 |左右差| + |上下差| 超过门槛 —— 合焦的地方边缘最锐, 描出来的点最密,
 *          手动拧镜头时看哪个位置描得最满就是合焦(比看焦点值数字直观)。
 *   斑马纹 = 亮度超过门槛的像素, 按 (x+y)/8 的奇偶打成 45° 斜纹, 一眼看出哪儿过曝了。
 * 结果写成 1bit/像素的掩膜, 由 OSD 线程铺到叠加层(见 cam_vis_blit)。 */
#define VIS_PERIOD_MS 150          /* 最快多久算一次 —— 比 OSD 重画(200ms)略快就够 */

static void cam_vis_analyze(const ot_video_frame_info *f)
{
    /* 线程私有暂存区: 先算完再整块拷进共享掩膜, 持锁时间压到一次 memcpy */
    static unsigned char pk[VIS_MASK_BYTES];
    static unsigned char zb[VIS_MASK_BYTES];
    const ot_video_frame *v = &f->video_frame;
    const unsigned char *y;
    int w, h, sstride, r, c, gth, zth;
    int peak = g_peak_en, zeb = g_zebra_en;
    td_u64 t0;

    if (g_npu_vb_virt == TD_NULL || (!peak && !zeb)) { return; }
    w = (int)v->width; h = (int)v->height; sstride = (int)v->stride[0];
    if (w > VIS_MASK_W) { w = VIS_MASK_W; }
    if (h > VIS_MASK_H) { h = VIS_MASK_H; }
    if (w < 8 || h < 8 || sstride <= 0) { return; }
    y = (const unsigned char *)g_npu_vb_virt + (v->phys_addr[0] - g_npu_vb_pool.pool_phy_addr);

    t0 = sample_common_svp_npu_get_timestamp();
    (td_void)memset_s(pk, sizeof(pk), 0, sizeof(pk));
    (td_void)memset_s(zb, sizeof(zb), 0, sizeof(zb));
    /* 灵敏度 1..100 -> 梯度门槛 80..4 (调高灵敏度 = 门槛降低 = 描得更多) */
    gth = 4 + (100 - g_peak_th) * 76 / 100;
    zth = g_zebra_th;
    for (r = 1; r < h - 1; r++) {
        const unsigned char *p  = y + (size_t)r * sstride;
        const unsigned char *pu = p - sstride;
        const unsigned char *pd = p + sstride;
        unsigned char *pkr = pk + (size_t)r * VIS_MASK_STRIDE;
        unsigned char *zbr = zb + (size_t)r * VIS_MASK_STRIDE;
        for (c = 1; c < w - 1; c++) {
            unsigned char bit = (unsigned char)(0x80 >> (c & 7));
            if (zeb && p[c] >= zth && ((((c + r) >> 3) & 1) == 0)) {
                zbr[c >> 3] |= bit;                    /* 8 像素宽的 45° 斜纹 */
            }
            if (peak) {
                int gx = (int)p[c + 1] - (int)p[c - 1];
                int gy = (int)pd[c] - (int)pu[c];
                if (gx < 0) { gx = -gx; }
                if (gy < 0) { gy = -gy; }
                if (gx + gy >= gth) { pkr[c >> 3] |= bit; }
            }
        }
    }
    pthread_mutex_lock(&g_vis_lock);
    (td_void)memcpy_s(g_peak_mask, sizeof(g_peak_mask), pk, sizeof(pk));
    (td_void)memcpy_s(g_zebra_mask, sizeof(g_zebra_mask), zb, sizeof(zb));
    g_vis_mw = w; g_vis_mh = h;
    pthread_mutex_unlock(&g_vis_lock);
    g_vis_ms = (int)(sample_common_svp_npu_get_timestamp() - t0);
}

/* 这一路帧的消费者: 检测 / 描边 / 斑马纹, 有一个要就得取帧 */
static int cam_vis_want(void)
{
    return (g_npu_en && g_npu_ready) || g_peak_en || g_zebra_en;
}

/* 视觉线程: 取扩展通道帧 -> (限速) 算描边/斑马纹 -> (每N帧) 补边喂 NPU 推理 -> 解析.
 * 画面上的东西一律由 OSD 线程画(单一写者), 这里只更新数据。 */
static void *cam_npu_thread(void *arg)
{
    ot_video_frame_info frame;
    td_s32 ret;
    int fno = 0, nlog = 0;
    unsigned long long last_vis = 0;     /* 上次算描边/斑马纹的时刻, 用来限速 */
    (td_void)arg;
    (td_void)prctl(PR_SET_NAME, "cam_vis", 0, 0, 0);
    (td_void)svp_acl_rt_set_device(NPU_DEV_ID);   /* 本线程绑 NPU 设备 */

    while (!g_npu_stop) {
        int do_npu, do_vis;
        unsigned long long now;
        if (!g_npu_en && g_npu_box_n != 0) {   /* 检测被关掉: 清空框(OSD 线程随即擦掉画面) */
            pthread_mutex_lock(&g_npu_lock);
            g_npu_box_n = 0; g_npu_last_num = 0;
            pthread_mutex_unlock(&g_npu_lock);
        }
        if (!cam_vis_want()) { usleep(200000); continue; }   /* 三个消费者都不要帧 */
        ret = ss_mpi_vpss_get_chn_frame(0, NPU_EXT_CHN, &frame, 2000);
        if (ret != TD_SUCCESS) { usleep(20000); continue; }
        /* 首帧: 映射该帧所在 VB 池一次, 之后零拷贝 */
        if (g_npu_vb_virt == TD_NULL) {
            if (ss_mpi_vb_get_pool_info((ot_vb_pool)frame.pool_id, &g_npu_vb_pool) == TD_SUCCESS) {
                g_npu_vb_virt = ss_mpi_sys_mmap(g_npu_vb_pool.pool_phy_addr, (td_u32)g_npu_vb_pool.pool_size);
            }
        }
        fno++;
        do_npu = (g_npu_en && g_npu_ready && (fno % g_npu_interval) == 0);
        now = sample_common_svp_npu_get_timestamp();
        do_vis = (g_peak_en || g_zebra_en) && (now - last_vis >= VIS_PERIOD_MS);
        if (do_vis) { last_vis = now; }
        if (g_npu_vb_virt != TD_NULL && do_vis) { cam_vis_analyze(&frame); }
        if (g_npu_vb_virt != TD_NULL && do_npu) {
            ret = cam_npu_letterbox(&frame);       /* 等比画面拷进灰底输入缓冲区 */
            if (ret == TD_SUCCESS) {
                td_u64 t0 = sample_common_svp_npu_get_timestamp();
                ret = sample_common_svp_npu_model_execute(&g_npu_task);
                g_npu_ms = (int)(sample_common_svp_npu_get_timestamp() - t0);
            }
            if (ret == TD_SUCCESS) {
                int n = cam_npu_parse();       /* 画框由 OSD 线程做, 这里只更新数据 */
                if (n >= 0) {
                    if (n > 0 && (nlog++ % 30) == 0) {   /* 日志别刷屏, 每 30 次打一条 */
                        int i;
                        printf("[npu] frame#%d 目标=%d (%d ms)\n", fno, n, g_npu_ms);
                        for (i = 0; i < n && i < NPU_MAX_DET; i++) {
                            printf("   %s %d%% (%d,%d)-(%d,%d)‰%s\n", cam_det_name(g_npu_box[i].cls),
                                g_npu_box[i].score, g_npu_box[i].x0, g_npu_box[i].y0,
                                g_npu_box[i].x1, g_npu_box[i].y1,
                                (g_npu_box[i].nkp >= 2) ? " [有眼睛坐标]" : "");
                        }
                    }
                }
            }
        }
        (td_void)ss_mpi_vpss_release_chn_frame(0, NPU_EXT_CHN, &frame);
    }
    return TD_NULL;
}

/* 起视觉线程(幂等). 检测和描边/斑马纹共用这一个线程. */
static void cam_vis_thread_start(void)
{
    if (g_npu_th_run) { return; }
    g_npu_stop = 0;
    if (pthread_create(&g_npu_th, TD_NULL, cam_npu_thread, TD_NULL) != 0) {
        printf("[vis] thread create failed\n");
        return;
    }
    g_npu_th_run = 1;
}

/* 只为描边/斑马纹要帧(没有模型也能跑): 开默认尺寸的扩展通道 + 起线程. 非致命. */
static void cam_vis_start(void)
{
    if (cam_vis_chn_start(VIS_MASK_W, VIS_MASK_H) != 0) {
        printf("[vis] 扩展通道起不来 -> 描边/斑马纹不可用(相机不受影响)\n");
        return;
    }
    cam_vis_thread_start();
    printf("[vis] 取景辅助已启动 (描边=%d 斑马纹=%d, %dx%d)\n",
        g_peak_en, g_zebra_en, VIS_MASK_W, VIS_MASK_H);
}

/* 启动 NPU 检测 (由 sample_venc_web 在所有通道就绪后调用, 受 g_npu_en 控制). 非致命.
 * 模型起不来时, 只要描边/斑马纹还开着就退回"只取景辅助"模式, 别把这一路帧一起关掉. */
static void cam_npu_start(void)
{
    if (cam_npu_init() != 0) {
        printf("[npu] init failed -> detection disabled (camera unaffected)\n");
        if (g_peak_en || g_zebra_en) { cam_vis_start(); }
        return;
    }
    if (cam_vis_chn_start(g_npu_cw, g_npu_ch) != 0) {
        printf("[npu] ext chn failed -> detection disabled\n");
        (td_void)sample_common_svp_npu_destroy_work_buf(&g_npu_task);
        (td_void)sample_common_svp_npu_destroy_task_buf(&g_npu_task);
        (td_void)sample_common_svp_npu_destroy_output(&g_npu_task);
        (td_void)sample_common_svp_npu_destroy_input(&g_npu_task);
        (td_void)sample_common_svp_npu_unload_model(0);
        return;
    }
    g_npu_ready = 1;
    cam_vis_thread_start();
    if (!g_npu_th_run) { g_npu_ready = 0; return; }
    printf("[npu] detection started (%s, ext chn %d, %ux%u, every %d frames)\n",
        g_npu_model, NPU_EXT_CHN, g_npu_insize.width, g_npu_insize.height, g_npu_interval);
}

/* 换模型(热切换): 卸掉当前的再按新文件名起一遍. 失败会退回原模型, 退不回就只是关掉检测. */
static void cam_npu_switch(const char *name)
{
    char old[64];
    (td_void)snprintf(old, sizeof(old), "%s", g_npu_model);
    if (g_npu_ready) { cam_npu_teardown(); }
    pthread_mutex_lock(&g_cfg_lock);
    (td_void)snprintf(g_npu_model, sizeof(g_npu_model), "%s", name);
    pthread_mutex_unlock(&g_cfg_lock);
    if (access(cam_npu_model_path(), R_OK) != 0) {
        printf("[npu] 模型 %s 不存在, 退回 %s\n", name, old);
        (td_void)snprintf(g_npu_model, sizeof(g_npu_model), "%s", old);
    }
    cam_npu_start();
    if (!g_npu_ready && strcmp(g_npu_model, old) != 0) {
        printf("[npu] %s 启动失败, 退回 %s\n", g_npu_model, old);
        (td_void)snprintf(g_npu_model, sizeof(g_npu_model), "%s", old);
        cam_npu_start();
    }
}

/* GET /peak -> 取景辅助(峰值描边 + 斑马纹)状态. w/h = 掩膜实际算的分辨率 */
static void cam_http_peak(int fd)
{
    char j[192];
    int n = snprintf(j, sizeof(j),
        "{\"peak\":%d,\"th\":%d,\"color\":%d,\"zebra\":%d,\"zth\":%d,\"ms\":%d,\"w\":%d,\"h\":%d}",
        g_peak_en, g_peak_th, g_peak_color, g_zebra_en, g_zebra_th, g_vis_ms, g_vis_mw, g_vis_mh);
    cam_http_body(fd, "application/json", j, n);
}

/* GET /setpeak?peak=&th=&color=&zebra=&zth= : 立即生效 + 存 param.
 * 从"两个都关"变成"有开着"时, 扩展通道/视觉线程可能还没起(比如检测是关的), 这里补起来. */
static void cam_http_setpeak(int fd, const char *req)
{
    char v[16];
    int changed = 0;
    if (cam_qparam(req, "peak", v, sizeof(v))) {
        pthread_mutex_lock(&g_cfg_lock);
        g_peak_en = atoi(v) ? 1 : 0;
        pthread_mutex_unlock(&g_cfg_lock);
        changed = 1;
    }
    if (cam_qparam(req, "th", v, sizeof(v))) {
        int t = atoi(v);
        if (t < 1) { t = 1; }
        if (t > 100) { t = 100; }
        pthread_mutex_lock(&g_cfg_lock);
        g_peak_th = t;
        pthread_mutex_unlock(&g_cfg_lock);
        changed = 1;
    }
    if (cam_qparam(req, "color", v, sizeof(v))) {
        pthread_mutex_lock(&g_cfg_lock);
        g_peak_color = atoi(v) & 3;
        pthread_mutex_unlock(&g_cfg_lock);
        changed = 1;
    }
    if (cam_qparam(req, "zebra", v, sizeof(v))) {
        pthread_mutex_lock(&g_cfg_lock);
        g_zebra_en = atoi(v) ? 1 : 0;
        pthread_mutex_unlock(&g_cfg_lock);
        changed = 1;
    }
    if (cam_qparam(req, "zth", v, sizeof(v))) {
        int t = atoi(v);
        if (t < 100) { t = 100; }
        if (t > 255) { t = 255; }
        pthread_mutex_lock(&g_cfg_lock);
        g_zebra_th = t;
        pthread_mutex_unlock(&g_cfg_lock);
        changed = 1;
    }
    if (changed) { cam_cfg_save(); }
    if (g_peak_en || g_zebra_en) {          /* 两个函数都是幂等的, 已经起来了就直接返回 */
        if (g_vis_chn_w == 0) { cam_vis_start(); } else { cam_vis_thread_start(); }
    } else {                                 /* 都关了: 掩膜作废, 免得下次开的瞬间闪一帧旧的 */
        pthread_mutex_lock(&g_vis_lock);
        g_vis_mw = 0; g_vis_mh = 0;
        pthread_mutex_unlock(&g_vis_lock);
    }
    cam_http_text(fd, "已保存");
}

/* GET /models -> 列出 /tmp/model 与 /opt/model 下的 .om (名字/大小/是否当前) */
static void cam_http_models(int fd)
{
    char j[1024];
    int n = 0, first = 1, d;
    n += snprintf(j + n, sizeof(j) - n, "{\"cur\":\"%s\",\"list\":[", g_npu_model);
    for (d = 0; d < NPU_MODEL_DIRS; d++) {
        DIR *dp = opendir(g_npu_model_dirs[d]);
        struct dirent *de;
        if (dp == TD_NULL) { continue; }
        while ((de = readdir(dp)) != TD_NULL && n < (int)sizeof(j) - 120) {
            size_t ln = strlen(de->d_name);
            char full[160];
            struct stat st;
            if (ln < 4 || strcmp(de->d_name + ln - 3, ".om") != 0) { continue; }
            (td_void)snprintf(full, sizeof(full), "%s/%s", g_npu_model_dirs[d], de->d_name);
            if (stat(full, &st) != 0) { continue; }
            n += snprintf(j + n, sizeof(j) - n, "%s{\"name\":\"%s\",\"kb\":%ld,\"dir\":\"%s\"}",
                first ? "" : ",", de->d_name, (long)(st.st_size / 1024), g_npu_model_dirs[d]);
            first = 0;
        }
        (td_void)closedir(dp);
    }
    n += snprintf(j + n, sizeof(j) - n, "]}");
    cam_http_body(fd, "application/json", j, n);
}

/* GET /npu -> 检测状态 + 最近一帧目标 (坐标换算到 720p 预览像素, w/h 给出参考画幅) */
static void cam_http_npu(int fd)
{
    char j[4096];
    cam_npu_box b[NPU_MAX_DET];
    int n = 0, i, k, cnt;
    pthread_mutex_lock(&g_npu_lock);
    cnt = g_npu_box_n;
    (td_void)memcpy_s(b, sizeof(b), g_npu_box, sizeof(g_npu_box));
    pthread_mutex_unlock(&g_npu_lock);
    n += snprintf(j + n, sizeof(j) - n,
        "{\"en\":%d,\"ready\":%d,\"draw\":%d,\"interval\":%d,\"score\":%d,\"nms\":%d,"
        "\"cls\":\"%s\",\"ms\":%d,\"w\":%d,\"h\":%d,\"kind\":\"%s\",\"model\":\"%s\",\"n\":%d,\"det\":[",
        g_npu_en, g_npu_ready, g_npu_draw, g_npu_interval, g_npu_score, g_npu_nms,
        g_npu_cls, g_npu_ms, DET_W, DET_H,
        (g_npu_kind == NPU_KIND_YUNET) ? "face" : "coco", g_npu_model, cnt);
    for (i = 0; i < cnt && i < NPU_MAX_DET && n < (int)sizeof(j) - 260; i++) {
        int x0 = b[i].x0 * DET_W / 1000, x1 = b[i].x1 * DET_W / 1000;
        int y0 = b[i].y0 * DET_H / 1000, y1 = b[i].y1 * DET_H / 1000;
        n += snprintf(j + n, sizeof(j) - n,
            "%s{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"c\":%d,\"s\":%d,\"n\":\"%s\",\"kp\":[",
            (i > 0) ? "," : "", x0, y0, x1 - x0, y1 - y0, b[i].cls, b[i].score,
            cam_det_name(b[i].cls));
        for (k = 0; k < b[i].nkp && k < NPU_KPS; k++) {     /* 关键点也换算到 720p 像素 */
            n += snprintf(j + n, sizeof(j) - n, "%s[%d,%d]", (k > 0) ? "," : "",
                b[i].kx[k] * DET_W / 1000, b[i].ky[k] * DET_H / 1000);
        }
        n += snprintf(j + n, sizeof(j) - n, "]}");
    }
    n += snprintf(j + n, sizeof(j) - n, "]}");
    cam_http_body(fd, "application/json", j, n);
}

/* GET /setnpu?en=&interval=&draw= : 立即生效 + 存 param. en 从 0->1 时懒启动 NPU. */
static void cam_http_setnpu(int fd, const char *req)
{
    char v[16];
    int changed = 0, start_now = 0;
    if (cam_qparam(req, "en", v, sizeof(v))) {
        int e = atoi(v) ? 1 : 0;
        if (e && !g_npu_ready) { start_now = 1; }        /* 开机时是关的 -> 现在才初始化 */
        pthread_mutex_lock(&g_cfg_lock);
        g_npu_en = e;
        pthread_mutex_unlock(&g_cfg_lock);
        changed = 1;
    }
    if (cam_qparam(req, "interval", v, sizeof(v))) {
        int iv = atoi(v);
        if (iv < 1) { iv = 1; }
        if (iv > 30) { iv = 30; }
        pthread_mutex_lock(&g_cfg_lock);
        g_npu_interval = iv;
        pthread_mutex_unlock(&g_cfg_lock);
        changed = 1;
    }
    if (cam_qparam(req, "draw", v, sizeof(v))) {
        pthread_mutex_lock(&g_cfg_lock);
        g_npu_draw = atoi(v) ? 1 : 0;
        pthread_mutex_unlock(&g_cfg_lock);
        changed = 1;
    }
    if (cam_qparam(req, "score", v, sizeof(v))) {
        int sc = atoi(v);
        if (sc < 1) { sc = 1; }
        if (sc > 99) { sc = 99; }
        pthread_mutex_lock(&g_cfg_lock);
        g_npu_score = sc;
        pthread_mutex_unlock(&g_cfg_lock);
        changed = 1;
    }
    if (cam_qparam(req, "nms", v, sizeof(v))) {
        int nv = atoi(v);
        if (nv < 5) { nv = 5; }
        if (nv > 95) { nv = 95; }
        pthread_mutex_lock(&g_cfg_lock);
        g_npu_nms = nv;
        pthread_mutex_unlock(&g_cfg_lock);
        /* 厂商 RPN 模型: 阈值张量可以在线改, 不用重载模型.
         * 裸输出模型(YuNet): NMS 在解析里做, g_npu_nms 改完下一帧就生效, 什么都不用调. */
        if (g_npu_ready && g_npu_kind == NPU_KIND_RPN) {
            sample_svp_npu_threshold th;
            (td_void)memset_s(&th, sizeof(th), 0, sizeof(th));
            th.nms_threshold = (float)g_npu_nms / 100.0f; th.score_threshold = 0.10f;
            th.min_height = 1.0f; th.min_width = 1.0f; th.name = g_npu_rpn_name;
            (td_void)sample_common_svp_npu_set_threshold(&th, 1, &g_npu_task);
        }
        changed = 1;
    }
    {   /* 类别白名单: 只留数字和逗号, 防止乱字符进配置文件 */
        char cl[128];
        if (cam_qparam(req, "cls", cl, sizeof(cl))) {
            char out[128];
            int i, o = 0;
            for (i = 0; cl[i] != '\0' && o < (int)sizeof(out) - 1; i++) {
                if ((cl[i] >= '0' && cl[i] <= '9') || cl[i] == ',') { out[o++] = cl[i]; }
            }
            out[o] = '\0';
            pthread_mutex_lock(&g_cfg_lock);
            snprintf(g_npu_cls, sizeof(g_npu_cls), "%s", out);
            pthread_mutex_unlock(&g_cfg_lock);
            changed = 1;
        }
    }
    {   /* 换模型: 文件名只允许字母数字和 . _ - (防路径穿越) */
        char mn[64];
        if (cam_qparam(req, "model", mn, sizeof(mn)) && mn[0] != '\0') {
            char out[64];
            int i, o = 0;
            for (i = 0; mn[i] != '\0' && o < (int)sizeof(out) - 1; i++) {
                char c = mn[i];
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '.' || c == '_' || c == '-') { out[o++] = c; }
            }
            out[o] = '\0';
            if (o > 3 && strcmp(out, g_npu_model) != 0) {
                cam_npu_switch(out);
                start_now = 0;                           /* switch 里已经起过了 */
                changed = 1;
            }
        }
    }
    if (start_now) { cam_npu_start(); }                  /* 载模型+建扩展通道+起线程, 非致命 */
    if (changed) { cam_cfg_save(); }                     /* 画面由 OSD 线程 200ms 内跟上 */
    cam_http_text(fd, "\xe5\xb7\xb2\xe4\xbf\x9d\xe5\xad\x98");  /* 已保存 */
}
/* =================== end NPU =================== */

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
    (td_void)cam_exp_apply();       /* 恢复上次的曝光设置(手动挡/补偿/最长快门) */
    /* 白平衡: **只在配置成手动时才碰 ISP**。自动本来就是 ISP 的默认状态,
     * 开机去做一次 get→原样 set 属于无谓写入 —— 这时 AWB 还没稳, 等于把一份
     * 没定型的 auto_attr 写回去。 */
    if (g_wb_manual) { (td_void)cam_wb_apply(); }
    cam_osd_init();
    { pthread_t osd_th; pthread_create(&osd_th, TD_NULL, cam_osd_thread, TD_NULL); }

    /* 若已保存"启用屏幕", 现在(通道都在产帧)开屏. 默认关闭 -> 通常跳过. 非致命. */
    if (g_scr_en) { cam_scr_start(); }

    /* NPU 目标检测 (YOLOv8): 所有通道已就绪, 建扩展通道 + 起推理线程. 受 g_npu_en 控制. 非致命. */
    if (g_npu_en) { cam_npu_start(); }
    /* 检测关着但存了"峰值描边/斑马纹": 也得把扩展通道和视觉线程起来 */
    if (!g_npu_ready && (g_peak_en || g_zebra_en)) { cam_vis_start(); }

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
