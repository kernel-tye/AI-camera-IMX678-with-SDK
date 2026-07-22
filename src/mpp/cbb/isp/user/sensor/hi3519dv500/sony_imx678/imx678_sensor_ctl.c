/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef OT_GPIO_I2C
#include "gpioi2c_ex.h"
#else
#include "ot_i2c.h"
#endif
#include "securec.h"

#include "imx678_cmos.h"

#define I2C_DEV_FILE_NUM     16
#define I2C_BUF_NUM          8

static int g_fd[OT_ISP_MAX_PIPE_NUM] = {[0 ...(OT_ISP_MAX_PIPE_NUM - 1)] = -1};

int imx678_i2c_init(ot_vi_pipe vi_pipe)
{
    if (g_fd[vi_pipe] >= 0) {
        return TD_SUCCESS;
    }
#ifdef OT_GPIO_I2C
    g_fd[vi_pipe] = open("/dev/gpioi2c_ex", O_RDONLY, S_IRUSR);
    if (g_fd[vi_pipe] < 0) {
        isp_err_trace("Open gpioi2c_ex error!\n");
        return TD_FAILURE;
    }
#else
    int ret;
    char dev_file[I2C_DEV_FILE_NUM] = {0};
    td_u8 dev_num;
    ot_isp_sns_commbus *imx678businfo = TD_NULL;
    imx678businfo = imx678_get_bus_info(vi_pipe);
    dev_num = imx678businfo->i2c_dev;
    if (snprintf_s(dev_file, sizeof(dev_file), sizeof(dev_file) - 1, "/dev/i2c-%u", dev_num) < 0) {
        isp_err_trace("snprintf_s error!\n");
        return TD_FAILURE;
    }

    g_fd[vi_pipe] = open(dev_file, O_RDWR, S_IRUSR | S_IWUSR);
    if (g_fd[vi_pipe] < 0) {
        isp_err_trace("Open /dev/ot_i2c_drv-%u error!\n", dev_num);
        return TD_FAILURE;
    }

    ret = ioctl(g_fd[vi_pipe], OT_I2C_SLAVE_FORCE, (IMX678_I2C_ADDR >> 1));
    if (ret < 0) {
        isp_err_trace("I2C_SLAVE_FORCE error!\n");
        close(g_fd[vi_pipe]);
        g_fd[vi_pipe] = -1;
        return ret;
    }
#endif

    return TD_SUCCESS;
}

int imx678_i2c_exit(ot_vi_pipe vi_pipe)
{
    if (g_fd[vi_pipe] >= 0) {
        close(g_fd[vi_pipe]);
        g_fd[vi_pipe] = -1;
        return TD_SUCCESS;
    }
    return TD_FAILURE;
}

td_s32 imx678_read_register(ot_vi_pipe vi_pipe, td_u32 addr)
{
    ot_unused(vi_pipe);
    ot_unused(addr);
    return TD_SUCCESS;
}

td_s32 imx678_write_register(ot_vi_pipe vi_pipe, td_u32 addr, td_u32 data)
{
    td_s32 ret;
    if (g_fd[vi_pipe] < 0) {
        return TD_SUCCESS;
    }

#ifdef OT_GPIO_I2C
    i2c_data.dev_addr = IMX678_I2C_ADDR;
    i2c_data.reg_addr = addr;
    i2c_data.addr_byte_num = IMX678_ADDR_BYTE;
    i2c_data.data = data;
    i2c_data.data_byte_num = IMX678_DATA_BYTE;

    ret = ioctl(g_fd[vi_pipe], GPIO_I2C_WRITE, &i2c_data);
    if (ret) {
        isp_err_trace("GPIO-I2C write failed!\n");
        return ret;
    }
#else
    td_u32 idx = 0;
    td_u8 buf[I2C_BUF_NUM];

    if (IMX678_ADDR_BYTE == 2) {  /* 2 byte */
        buf[idx] = (addr >> 8) & 0xff;  /* shift 8 */
        idx++;
        buf[idx] = addr & 0xff;
        idx++;
    } else {
    }

    if (IMX678_DATA_BYTE == 2) {  /* 2 byte */
    } else {
        buf[idx] = data & 0xff;
        idx++;
    }

    ret = write(g_fd[vi_pipe], buf, IMX678_ADDR_BYTE + IMX678_DATA_BYTE);
    if (ret < 0) {
        isp_err_trace("I2C_WRITE error!\n");
        return TD_FAILURE;
    }

#endif
    return TD_SUCCESS;
}

static void delay_ms(int ms)
{
    usleep(ms * 1000); /* 1ms: 1000us */
    return;
}

void imx678_prog(ot_vi_pipe vi_pipe, const td_u32 *rom)
{
    ot_unused(vi_pipe);
    ot_unused(rom);
}

void imx678_standby(ot_vi_pipe vi_pipe)
{
    td_s32 ret = TD_SUCCESS;
    ret += imx678_write_register(vi_pipe, 0x3000, 0x01);  /* STANDBY */
    ret += imx678_write_register(vi_pipe, 0x3002, 0x01);  /* XTMSTA */
    if (ret != TD_SUCCESS) {
        isp_err_trace("write register failed!\n");
    }
    return;
}

void imx678_restart(ot_vi_pipe vi_pipe)
{
    td_s32 ret = TD_SUCCESS;
    ret += imx678_write_register(vi_pipe, 0x3000, 0x00);  /* standby */
    delay_ms(20); /* 20ms */
    ret += imx678_write_register(vi_pipe, 0x3002, 0x00);  /* master mode start */
    if (ret != TD_SUCCESS) {
        isp_err_trace("write register failed!\n");
    }
    return;
}

void imx678_mirror_flip(ot_vi_pipe vi_pipe, ot_isp_sns_mirrorflip_type sns_mirror_flip)
{
    switch (sns_mirror_flip) {
        case ISP_SNS_NORMAL:
            imx678_write_register(vi_pipe, 0x3030, 0x00);
            break;
        case ISP_SNS_MIRROR:
            imx678_write_register(vi_pipe, 0x3030, 0x01);
            break;
        case ISP_SNS_FLIP:
            imx678_write_register(vi_pipe, 0x3030, 0x02);
            break;
        case ISP_SNS_MIRROR_FLIP:
            imx678_write_register(vi_pipe, 0x3030, 0x03);
            break;
        default:
            break;
    }
    return;
}

td_void imx678_blc_clamp(ot_vi_pipe vi_pipe, ot_isp_sns_blc_clamp blc_clamp)
{
    td_s32 ret = TD_SUCCESS;

    imx678_set_blc_clamp_value(vi_pipe, blc_clamp.blc_clamp_en);

    if (blc_clamp.blc_clamp_en == TD_TRUE) {
        ret += imx678_write_register(vi_pipe, 0x300e, 0x01);
    } else {
        ret += imx678_write_register(vi_pipe, 0x300e, 0x00);
    }

    if (ret != TD_SUCCESS) {
        isp_err_trace("write register failed!\n");
    }
    return;
}

void imx678_comm_init(ot_vi_pipe vi_pipe);
void imx678_linear_8m_init(ot_vi_pipe vi_pipe);

void imx678_default_reg_init(ot_vi_pipe vi_pipe)
{
    td_u32 i;
    td_s32 ret = TD_SUCCESS;
    ot_isp_sns_state *pastimx678 = TD_NULL;
    pastimx678 = imx678_get_ctx(vi_pipe);
    for (i = 0; i < pastimx678->regs_info[0].reg_num; i++) {
        ret += imx678_write_register(vi_pipe,
                                     pastimx678->regs_info[0].i2c_data[i].reg_addr,
                                     pastimx678->regs_info[0].i2c_data[i].data);
    }
    if (ret != TD_SUCCESS) {
        isp_err_trace("write register failed!\n");
    }
    return;
}

void imx678_init(ot_vi_pipe vi_pipe)
{
    ot_wdr_mode wdr_mode;
    td_bool          init;
    td_s32 ret;
    ot_isp_sns_state *pastimx678 = TD_NULL;
    pastimx678 = imx678_get_ctx(vi_pipe);
    init       = pastimx678->init;
    wdr_mode   = pastimx678->wdr_mode;

    ret = imx678_i2c_init(vi_pipe);
    if (ret != TD_SUCCESS) {
        isp_err_trace("i2c init failed!\n");
        return;
    }
    /* When sensor first init, config all registers */
    if (init == TD_FALSE) {
        if (OT_WDR_MODE_2To1_LINE == wdr_mode) {
        } else {
            imx678_linear_8m_init(vi_pipe);
        }
    } else {
        /* When sensor switch mode(linear<->WDR or resolution), config different registers(if possible) */
        if (OT_WDR_MODE_2To1_LINE == wdr_mode) {
        } else {
            imx678_linear_8m_init(vi_pipe);
        }
    }

    pastimx678->init = TD_TRUE;
    return;
}

void imx678_exit(ot_vi_pipe vi_pipe)
{
    td_s32 ret;
    ret = imx678_i2c_exit(vi_pipe);
    if (ret != TD_SUCCESS) {
        isp_err_trace("IMX678 exit failed!\n");
    }
    return;
}

void imx678_linear_8m_init(ot_vi_pipe vi_pipe)
{
    td_s32 ret = TD_SUCCESS;
/* ==== BLOB reconstruction path A (true exec order) ==== */
/* direct (0x3eb0) */
    ret += imx678_write_register(vi_pipe, 0x3015, 0x4);
    ret += imx678_write_register(vi_pipe, 0x301b, 0x0);  /* ADDMODE non-binning (orig live) */
    ret += imx678_write_register(vi_pipe, 0x3022, 0x1);  /* ADBIT (orig live) */
    ret += imx678_write_register(vi_pipe, 0x302c, 0x4c);
    ret += imx678_write_register(vi_pipe, 0x302d, 0x4);
/* sub 0x3a30 */
    ret += imx678_write_register(vi_pipe, 0x3014, 0x0);  /* INCK_SEL=74.25MHz (this SoC feeds 74.25, not 37.125!) */
    ret += imx678_write_register(vi_pipe, 0x3018, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3023, 0x1);
    ret += imx678_write_register(vi_pipe, 0x3028, 0x8c);  /* VMAX lo =2700 (orig 25fps) */
    ret += imx678_write_register(vi_pipe, 0x3029, 0xa);  /* VMAX mid */
    ret += imx678_write_register(vi_pipe, 0x302a, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3040, 0x3);
    ret += imx678_write_register(vi_pipe, 0x3050, 0x3);
    ret += imx678_write_register(vi_pipe, 0x3051, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3052, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3070, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3071, 0x0);
    ret += imx678_write_register(vi_pipe, 0x355a, 0x64);
    ret += imx678_write_register(vi_pipe, 0x3eb4, 0xb);
    ret += imx678_write_register(vi_pipe, 0x3eb5, 0x2);
    ret += imx678_write_register(vi_pipe, 0x3eb6, 0x4d);
    ret += imx678_write_register(vi_pipe, 0x3f24, 0x10);
    ret += imx678_write_register(vi_pipe, 0x4420, 0xb);
    ret += imx678_write_register(vi_pipe, 0x4421, 0x2);
    ret += imx678_write_register(vi_pipe, 0x4422, 0x4d);
    ret += imx678_write_register(vi_pipe, 0x44a4, 0x2c);
    ret += imx678_write_register(vi_pipe, 0x44a6, 0x2c);
    ret += imx678_write_register(vi_pipe, 0x44a8, 0x2c);
    ret += imx678_write_register(vi_pipe, 0x44aa, 0x2c);
    ret += imx678_write_register(vi_pipe, 0x44b4, 0x2c);
    ret += imx678_write_register(vi_pipe, 0x44b6, 0x2c);
    ret += imx678_write_register(vi_pipe, 0x44b8, 0x2c);
    ret += imx678_write_register(vi_pipe, 0x44ba, 0x2c);
    ret += imx678_write_register(vi_pipe, 0x44c4, 0x2c);
    ret += imx678_write_register(vi_pipe, 0x44c6, 0x2c);
    ret += imx678_write_register(vi_pipe, 0x44c8, 0x2c);
    ret += imx678_write_register(vi_pipe, 0x453d, 0x1b);
    ret += imx678_write_register(vi_pipe, 0x453e, 0x1b);
    ret += imx678_write_register(vi_pipe, 0x453f, 0x15);
    ret += imx678_write_register(vi_pipe, 0x4540, 0x15);
    ret += imx678_write_register(vi_pipe, 0x4541, 0x15);
    ret += imx678_write_register(vi_pipe, 0x4542, 0x15);
    ret += imx678_write_register(vi_pipe, 0x4543, 0x15);
    ret += imx678_write_register(vi_pipe, 0x4544, 0x15);
    ret += imx678_write_register(vi_pipe, 0x4549, 0x1);
    ret += imx678_write_register(vi_pipe, 0x454a, 0x1);
    ret += imx678_write_register(vi_pipe, 0x454b, 0x6);
    ret += imx678_write_register(vi_pipe, 0x454c, 0x6);
    ret += imx678_write_register(vi_pipe, 0x454d, 0x6);
    ret += imx678_write_register(vi_pipe, 0x454e, 0x6);
    ret += imx678_write_register(vi_pipe, 0x454f, 0x6);
    ret += imx678_write_register(vi_pipe, 0x4550, 0x6);
/* sub 0x1890 path A */
    ret += imx678_write_register(vi_pipe, 0x30a6, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3460, 0x22);
    ret += imx678_write_register(vi_pipe, 0x3a02, 0x7a);
    ret += imx678_write_register(vi_pipe, 0x3a10, 0xec);
    ret += imx678_write_register(vi_pipe, 0x3a12, 0x71);
    ret += imx678_write_register(vi_pipe, 0x3a14, 0xde);
    ret += imx678_write_register(vi_pipe, 0x3a20, 0x2b);
    ret += imx678_write_register(vi_pipe, 0x3a24, 0x22);
    ret += imx678_write_register(vi_pipe, 0x3a25, 0x25);
    ret += imx678_write_register(vi_pipe, 0x3a26, 0x2a);
    ret += imx678_write_register(vi_pipe, 0x3a27, 0x2c);
    ret += imx678_write_register(vi_pipe, 0x3a28, 0x39);
    ret += imx678_write_register(vi_pipe, 0x3a29, 0x38);
    ret += imx678_write_register(vi_pipe, 0x3a30, 0x4);
    ret += imx678_write_register(vi_pipe, 0x3a31, 0x4);
    ret += imx678_write_register(vi_pipe, 0x3a32, 0x3);
    ret += imx678_write_register(vi_pipe, 0x3a33, 0x3);
    ret += imx678_write_register(vi_pipe, 0x3a34, 0x9);
    ret += imx678_write_register(vi_pipe, 0x3a35, 0x6);
    ret += imx678_write_register(vi_pipe, 0x3a38, 0xcd);
    ret += imx678_write_register(vi_pipe, 0x3a3a, 0x4c);
    ret += imx678_write_register(vi_pipe, 0x3a3c, 0xb9);
    ret += imx678_write_register(vi_pipe, 0x3a3e, 0x30);
    ret += imx678_write_register(vi_pipe, 0x3a40, 0x2c);
    ret += imx678_write_register(vi_pipe, 0x3a42, 0x39);
    ret += imx678_write_register(vi_pipe, 0x3a4e, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3a52, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3a56, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3a5a, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3a5e, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3a62, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3a6e, 0xa0);
    ret += imx678_write_register(vi_pipe, 0x3a70, 0x50);
    ret += imx678_write_register(vi_pipe, 0x3a8c, 0x4);
    ret += imx678_write_register(vi_pipe, 0x3a8d, 0x3);
    ret += imx678_write_register(vi_pipe, 0x3a8e, 0x9);
    ret += imx678_write_register(vi_pipe, 0x3a90, 0x38);
    ret += imx678_write_register(vi_pipe, 0x3a91, 0x42);
    ret += imx678_write_register(vi_pipe, 0x3a92, 0x3c);
    ret += imx678_write_register(vi_pipe, 0x3b0e, 0xf3);
    ret += imx678_write_register(vi_pipe, 0x3b12, 0xe5);
    ret += imx678_write_register(vi_pipe, 0x3b27, 0xc0);
    ret += imx678_write_register(vi_pipe, 0x3b2e, 0xef);
    ret += imx678_write_register(vi_pipe, 0x3b30, 0x6a);
    ret += imx678_write_register(vi_pipe, 0x3b32, 0xf6);
    ret += imx678_write_register(vi_pipe, 0x3b36, 0xe1);
    ret += imx678_write_register(vi_pipe, 0x3b3a, 0xe8);
    ret += imx678_write_register(vi_pipe, 0x3b5a, 0x17);
    ret += imx678_write_register(vi_pipe, 0x3b5e, 0xef);
    ret += imx678_write_register(vi_pipe, 0x3b60, 0x6a);
    ret += imx678_write_register(vi_pipe, 0x3b62, 0xf6);
    ret += imx678_write_register(vi_pipe, 0x3b66, 0xe1);
    ret += imx678_write_register(vi_pipe, 0x3b6a, 0xe8);
    ret += imx678_write_register(vi_pipe, 0x3b88, 0xec);
    ret += imx678_write_register(vi_pipe, 0x3b8a, 0xed);
    ret += imx678_write_register(vi_pipe, 0x3b94, 0x71);
    ret += imx678_write_register(vi_pipe, 0x3b96, 0x72);
    ret += imx678_write_register(vi_pipe, 0x3b98, 0xde);
    ret += imx678_write_register(vi_pipe, 0x3b9a, 0xdf);
    ret += imx678_write_register(vi_pipe, 0x3c0f, 0x6);
    ret += imx678_write_register(vi_pipe, 0x3c10, 0x6);
    ret += imx678_write_register(vi_pipe, 0x3c11, 0x6);
    ret += imx678_write_register(vi_pipe, 0x3c12, 0x6);
    ret += imx678_write_register(vi_pipe, 0x3c13, 0x6);
    ret += imx678_write_register(vi_pipe, 0x3c18, 0x20);
    ret += imx678_write_register(vi_pipe, 0x3c3a, 0x7a);
    ret += imx678_write_register(vi_pipe, 0x3c40, 0xf4);
    ret += imx678_write_register(vi_pipe, 0x3c48, 0xe6);
    ret += imx678_write_register(vi_pipe, 0x3c54, 0xce);
    ret += imx678_write_register(vi_pipe, 0x3c56, 0xd0);
    ret += imx678_write_register(vi_pipe, 0x3c6c, 0x53);
    ret += imx678_write_register(vi_pipe, 0x3c6e, 0x55);
    ret += imx678_write_register(vi_pipe, 0x3c70, 0xc0);
    ret += imx678_write_register(vi_pipe, 0x3c72, 0xc2);
    ret += imx678_write_register(vi_pipe, 0x3c7e, 0xce);
    ret += imx678_write_register(vi_pipe, 0x3c8c, 0xcf);
    ret += imx678_write_register(vi_pipe, 0x3c8e, 0xeb);
    ret += imx678_write_register(vi_pipe, 0x3c98, 0x54);
    ret += imx678_write_register(vi_pipe, 0x3c9a, 0x70);
    ret += imx678_write_register(vi_pipe, 0x3c9c, 0xc1);
    ret += imx678_write_register(vi_pipe, 0x3c9e, 0xdd);
    ret += imx678_write_register(vi_pipe, 0x3cb0, 0x7a);
    ret += imx678_write_register(vi_pipe, 0x3cb2, 0xba);
    ret += imx678_write_register(vi_pipe, 0x3cc8, 0xbc);
    ret += imx678_write_register(vi_pipe, 0x3cca, 0x7c);
    ret += imx678_write_register(vi_pipe, 0x3cd4, 0xea);
    ret += imx678_write_register(vi_pipe, 0x3cd5, 0x1);
    ret += imx678_write_register(vi_pipe, 0x3cd6, 0x4a);
    ret += imx678_write_register(vi_pipe, 0x3cd8, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3cd9, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3cda, 0xff);
    ret += imx678_write_register(vi_pipe, 0x3cdb, 0x3);
    ret += imx678_write_register(vi_pipe, 0x3cdc, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3cdd, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3cde, 0xff);
    ret += imx678_write_register(vi_pipe, 0x3cdf, 0x3);
    ret += imx678_write_register(vi_pipe, 0x3ce4, 0x4c);
    ret += imx678_write_register(vi_pipe, 0x3ce6, 0xec);
    ret += imx678_write_register(vi_pipe, 0x3ce7, 0x1);
    ret += imx678_write_register(vi_pipe, 0x3ce8, 0xff);
    ret += imx678_write_register(vi_pipe, 0x3ce9, 0x3);
    ret += imx678_write_register(vi_pipe, 0x3cea, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3ceb, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3cec, 0xff);
    ret += imx678_write_register(vi_pipe, 0x3ced, 0x3);
    ret += imx678_write_register(vi_pipe, 0x3cee, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3cef, 0x0);
    ret += imx678_write_register(vi_pipe, 0x3e28, 0x82);
    ret += imx678_write_register(vi_pipe, 0x3e2a, 0x80);
    ret += imx678_write_register(vi_pipe, 0x3e30, 0x85);
    ret += imx678_write_register(vi_pipe, 0x3e32, 0x7d);
    ret += imx678_write_register(vi_pipe, 0x3e5c, 0xce);
    ret += imx678_write_register(vi_pipe, 0x3e5e, 0xd3);
    ret += imx678_write_register(vi_pipe, 0x3e70, 0x53);
    ret += imx678_write_register(vi_pipe, 0x3e72, 0x58);
    ret += imx678_write_register(vi_pipe, 0x3e74, 0xc0);
    ret += imx678_write_register(vi_pipe, 0x3e76, 0xc5);
    ret += imx678_write_register(vi_pipe, 0x3e78, 0xc0);
    ret += imx678_write_register(vi_pipe, 0x3e79, 0x1);
    ret += imx678_write_register(vi_pipe, 0x3e7a, 0xd4);
    ret += imx678_write_register(vi_pipe, 0x3e7b, 0x1);
    ret += imx678_write_register(vi_pipe, 0x3eec, 0xf3);
    ret += imx678_write_register(vi_pipe, 0x3eee, 0xe7);
    ret += imx678_write_register(vi_pipe, 0x3f01, 0x1);
    ret += imx678_write_register(vi_pipe, 0x3f28, 0x2d);
    ret += imx678_write_register(vi_pipe, 0x3f2a, 0x2d);
    ret += imx678_write_register(vi_pipe, 0x3f2c, 0x2d);
    ret += imx678_write_register(vi_pipe, 0x3f2e, 0x2d);
    ret += imx678_write_register(vi_pipe, 0x3f30, 0x23);
    ret += imx678_write_register(vi_pipe, 0x3f38, 0x2d);
    ret += imx678_write_register(vi_pipe, 0x3f3a, 0x2d);
    ret += imx678_write_register(vi_pipe, 0x3f3c, 0x2d);
    ret += imx678_write_register(vi_pipe, 0x3f3e, 0x28);
    ret += imx678_write_register(vi_pipe, 0x3f40, 0x1e);
    ret += imx678_write_register(vi_pipe, 0x3f48, 0x2d);
    ret += imx678_write_register(vi_pipe, 0x3f4a, 0x2d);
    ret += imx678_write_register(vi_pipe, 0x4004, 0xe4);
    ret += imx678_write_register(vi_pipe, 0x4006, 0xff);
    ret += imx678_write_register(vi_pipe, 0x4018, 0x69);
    ret += imx678_write_register(vi_pipe, 0x401a, 0x84);
    ret += imx678_write_register(vi_pipe, 0x401c, 0xd6);
    ret += imx678_write_register(vi_pipe, 0x401e, 0xf1);
    ret += imx678_write_register(vi_pipe, 0x4038, 0xde);
    ret += imx678_write_register(vi_pipe, 0x403a, 0x0);
    ret += imx678_write_register(vi_pipe, 0x403b, 0x1);
    ret += imx678_write_register(vi_pipe, 0x404c, 0x63);
    ret += imx678_write_register(vi_pipe, 0x404e, 0x85);
    ret += imx678_write_register(vi_pipe, 0x4050, 0xd0);
    ret += imx678_write_register(vi_pipe, 0x4052, 0xf2);
    ret += imx678_write_register(vi_pipe, 0x4108, 0xdd);
    ret += imx678_write_register(vi_pipe, 0x410a, 0xf7);
    ret += imx678_write_register(vi_pipe, 0x411c, 0x62);
    ret += imx678_write_register(vi_pipe, 0x411e, 0x7c);
    ret += imx678_write_register(vi_pipe, 0x4120, 0xcf);
    ret += imx678_write_register(vi_pipe, 0x4122, 0xe9);
    ret += imx678_write_register(vi_pipe, 0x4138, 0xe6);
    ret += imx678_write_register(vi_pipe, 0x413a, 0xf1);
    ret += imx678_write_register(vi_pipe, 0x414c, 0x6b);
    ret += imx678_write_register(vi_pipe, 0x414e, 0x76);
    ret += imx678_write_register(vi_pipe, 0x4150, 0xd8);
    ret += imx678_write_register(vi_pipe, 0x4152, 0xe3);
    ret += imx678_write_register(vi_pipe, 0x417e, 0x3);
    ret += imx678_write_register(vi_pipe, 0x417f, 0x1);
    ret += imx678_write_register(vi_pipe, 0x4186, 0xe0);
    ret += imx678_write_register(vi_pipe, 0x4190, 0xf3);
    ret += imx678_write_register(vi_pipe, 0x4192, 0xf7);
    ret += imx678_write_register(vi_pipe, 0x419c, 0x78);
    ret += imx678_write_register(vi_pipe, 0x419e, 0x7c);
    ret += imx678_write_register(vi_pipe, 0x41a0, 0xe5);
    ret += imx678_write_register(vi_pipe, 0x41a2, 0xe9);
    ret += imx678_write_register(vi_pipe, 0x41c8, 0xe2);
    ret += imx678_write_register(vi_pipe, 0x41ca, 0xfd);
    ret += imx678_write_register(vi_pipe, 0x41dc, 0x67);
    ret += imx678_write_register(vi_pipe, 0x41de, 0x82);
    ret += imx678_write_register(vi_pipe, 0x41e0, 0xd4);
    ret += imx678_write_register(vi_pipe, 0x41e2, 0xef);
    ret += imx678_write_register(vi_pipe, 0x4200, 0xde);
    ret += imx678_write_register(vi_pipe, 0x4202, 0xda);
    ret += imx678_write_register(vi_pipe, 0x4218, 0x63);
    ret += imx678_write_register(vi_pipe, 0x421a, 0x5f);
    ret += imx678_write_register(vi_pipe, 0x421c, 0xd0);
    ret += imx678_write_register(vi_pipe, 0x421e, 0xcc);
    ret += imx678_write_register(vi_pipe, 0x425a, 0x82);
    ret += imx678_write_register(vi_pipe, 0x425c, 0xef);
    ret += imx678_write_register(vi_pipe, 0x4348, 0xfe);
    ret += imx678_write_register(vi_pipe, 0x4349, 0x6);
    ret += imx678_write_register(vi_pipe, 0x4352, 0xce);
    ret += imx678_write_register(vi_pipe, 0x4426, 0xf5);
    ret += imx678_write_register(vi_pipe, 0x442a, 0xe7);
    ret += imx678_write_register(vi_pipe, 0x4432, 0xf5);
    ret += imx678_write_register(vi_pipe, 0x4436, 0xe7);
    ret += imx678_write_register(vi_pipe, 0x4466, 0xb4);
    ret += imx678_write_register(vi_pipe, 0x446e, 0x32);
    ret += imx678_write_register(vi_pipe, 0x449f, 0x1c);
    ret += imx678_write_register(vi_pipe, 0x4506, 0xf3);
    ret += imx678_write_register(vi_pipe, 0x450e, 0xe5);
    ret += imx678_write_register(vi_pipe, 0x4516, 0xf3);
    ret += imx678_write_register(vi_pipe, 0x4522, 0xe5);
    ret += imx678_write_register(vi_pipe, 0x4524, 0xf3);
    ret += imx678_write_register(vi_pipe, 0x452c, 0xe5);
    ret += imx678_write_register(vi_pipe, 0x453c, 0x22);
    ret += imx678_write_register(vi_pipe, 0x4548, 0x0);
    ret += imx678_write_register(vi_pipe, 0x4554, 0x55);
    ret += imx678_write_register(vi_pipe, 0x4555, 0x2);
    ret += imx678_write_register(vi_pipe, 0x4556, 0x42);
    ret += imx678_write_register(vi_pipe, 0x4557, 0x5);
    ret += imx678_write_register(vi_pipe, 0x4558, 0xfd);
    ret += imx678_write_register(vi_pipe, 0x4559, 0x5);
    ret += imx678_write_register(vi_pipe, 0x455a, 0x94);
    ret += imx678_write_register(vi_pipe, 0x455b, 0x6);
    ret += imx678_write_register(vi_pipe, 0x455d, 0x6);
    ret += imx678_write_register(vi_pipe, 0x455e, 0x49);
    ret += imx678_write_register(vi_pipe, 0x455f, 0x7);
    ret += imx678_write_register(vi_pipe, 0x4560, 0x7f);
    ret += imx678_write_register(vi_pipe, 0x4561, 0x7);
    ret += imx678_write_register(vi_pipe, 0x4562, 0xa5);
    ret += imx678_write_register(vi_pipe, 0x4564, 0x55);
    ret += imx678_write_register(vi_pipe, 0x4565, 0x2);
    ret += imx678_write_register(vi_pipe, 0x4566, 0x42);
    ret += imx678_write_register(vi_pipe, 0x4567, 0x5);
    ret += imx678_write_register(vi_pipe, 0x4568, 0xfd);
    ret += imx678_write_register(vi_pipe, 0x4569, 0x5);
    ret += imx678_write_register(vi_pipe, 0x456a, 0x94);
    ret += imx678_write_register(vi_pipe, 0x456b, 0x6);
    ret += imx678_write_register(vi_pipe, 0x456d, 0x6);
    ret += imx678_write_register(vi_pipe, 0x456e, 0x49);
    ret += imx678_write_register(vi_pipe, 0x456f, 0x7);
    ret += imx678_write_register(vi_pipe, 0x4572, 0xa5);
    ret += imx678_write_register(vi_pipe, 0x460c, 0x7d);
    ret += imx678_write_register(vi_pipe, 0x460e, 0xb1);
    ret += imx678_write_register(vi_pipe, 0x4614, 0xa8);
    ret += imx678_write_register(vi_pipe, 0x4616, 0xb2);
    ret += imx678_write_register(vi_pipe, 0x461c, 0x7e);
    ret += imx678_write_register(vi_pipe, 0x461e, 0xa7);
    ret += imx678_write_register(vi_pipe, 0x4624, 0xa8);
    ret += imx678_write_register(vi_pipe, 0x4626, 0xb2);
    ret += imx678_write_register(vi_pipe, 0x462c, 0x7e);
    ret += imx678_write_register(vi_pipe, 0x462e, 0x8a);
    ret += imx678_write_register(vi_pipe, 0x4630, 0x94);
    ret += imx678_write_register(vi_pipe, 0x4632, 0xa7);
    ret += imx678_write_register(vi_pipe, 0x4634, 0xfb);
    ret += imx678_write_register(vi_pipe, 0x4636, 0x2f);
    ret += imx678_write_register(vi_pipe, 0x4638, 0x81);
    ret += imx678_write_register(vi_pipe, 0x4639, 0x1);
    ret += imx678_write_register(vi_pipe, 0x463a, 0xb5);
    ret += imx678_write_register(vi_pipe, 0x463b, 0x1);
    ret += imx678_write_register(vi_pipe, 0x463c, 0x26);
    ret += imx678_write_register(vi_pipe, 0x463e, 0x30);
    ret += imx678_write_register(vi_pipe, 0x4640, 0xac);
    ret += imx678_write_register(vi_pipe, 0x4641, 0x1);
    ret += imx678_write_register(vi_pipe, 0x4642, 0xb6);
    ret += imx678_write_register(vi_pipe, 0x4643, 0x1);
    ret += imx678_write_register(vi_pipe, 0x4644, 0xfc);
    ret += imx678_write_register(vi_pipe, 0x4646, 0x25);
    ret += imx678_write_register(vi_pipe, 0x4648, 0x82);
    ret += imx678_write_register(vi_pipe, 0x4649, 0x1);
    ret += imx678_write_register(vi_pipe, 0x464a, 0xab);
    ret += imx678_write_register(vi_pipe, 0x464b, 0x1);
    ret += imx678_write_register(vi_pipe, 0x464c, 0x26);
    ret += imx678_write_register(vi_pipe, 0x464e, 0x30);
    ret += imx678_write_register(vi_pipe, 0x4654, 0xfc);
    ret += imx678_write_register(vi_pipe, 0x4656, 0x8);
    ret += imx678_write_register(vi_pipe, 0x4658, 0x12);
    ret += imx678_write_register(vi_pipe, 0x465a, 0x25);
    ret += imx678_write_register(vi_pipe, 0x4662, 0xfc);
    ret += imx678_write_register(vi_pipe, 0x46a2, 0xfb);
    ret += imx678_write_register(vi_pipe, 0x46d6, 0xf3);
    ret += imx678_write_register(vi_pipe, 0x46e6, 0x0);
    ret += imx678_write_register(vi_pipe, 0x46e8, 0xff);
    ret += imx678_write_register(vi_pipe, 0x46e9, 0x3);
    ret += imx678_write_register(vi_pipe, 0x46ec, 0x7a);
    ret += imx678_write_register(vi_pipe, 0x46ee, 0xe5);
    ret += imx678_write_register(vi_pipe, 0x46f4, 0xee);
    ret += imx678_write_register(vi_pipe, 0x46f6, 0xf2);
    ret += imx678_write_register(vi_pipe, 0x470c, 0xff);
    ret += imx678_write_register(vi_pipe, 0x470d, 0x3);
    ret += imx678_write_register(vi_pipe, 0x470e, 0x0);
    ret += imx678_write_register(vi_pipe, 0x4714, 0xe0);
    ret += imx678_write_register(vi_pipe, 0x4716, 0xe4);
    ret += imx678_write_register(vi_pipe, 0x471e, 0xed);
    ret += imx678_write_register(vi_pipe, 0x472e, 0x0);
    ret += imx678_write_register(vi_pipe, 0x4730, 0xff);
    ret += imx678_write_register(vi_pipe, 0x4731, 0x3);
    ret += imx678_write_register(vi_pipe, 0x4734, 0x7b);
    ret += imx678_write_register(vi_pipe, 0x4736, 0xdf);
    ret += imx678_write_register(vi_pipe, 0x4754, 0x7d);
    ret += imx678_write_register(vi_pipe, 0x4756, 0x8b);
    ret += imx678_write_register(vi_pipe, 0x4758, 0x93);
    ret += imx678_write_register(vi_pipe, 0x475a, 0xb1);
    ret += imx678_write_register(vi_pipe, 0x475c, 0xfb);
    ret += imx678_write_register(vi_pipe, 0x475e, 0x9);
    ret += imx678_write_register(vi_pipe, 0x4760, 0x11);
    ret += imx678_write_register(vi_pipe, 0x4762, 0x2f);
    ret += imx678_write_register(vi_pipe, 0x4766, 0xcc);
    ret += imx678_write_register(vi_pipe, 0x4776, 0xcb);
    ret += imx678_write_register(vi_pipe, 0x477e, 0x4a);
    ret += imx678_write_register(vi_pipe, 0x478e, 0x49);
    ret += imx678_write_register(vi_pipe, 0x4794, 0x7c);
    ret += imx678_write_register(vi_pipe, 0x4796, 0x8f);
    ret += imx678_write_register(vi_pipe, 0x4798, 0xb3);
    ret += imx678_write_register(vi_pipe, 0x4799, 0x0);
    ret += imx678_write_register(vi_pipe, 0x479a, 0xcc);
    ret += imx678_write_register(vi_pipe, 0x479c, 0xc1);
    ret += imx678_write_register(vi_pipe, 0x479e, 0xcb);
    ret += imx678_write_register(vi_pipe, 0x47a4, 0x7d);
    ret += imx678_write_register(vi_pipe, 0x47a6, 0x8e);
    ret += imx678_write_register(vi_pipe, 0x47a8, 0xb4);
    ret += imx678_write_register(vi_pipe, 0x47a9, 0x0);
    ret += imx678_write_register(vi_pipe, 0x47aa, 0xc0);
    ret += imx678_write_register(vi_pipe, 0x47ac, 0xfa);
    ret += imx678_write_register(vi_pipe, 0x47ae, 0xd);
    ret += imx678_write_register(vi_pipe, 0x47b0, 0x31);
    ret += imx678_write_register(vi_pipe, 0x47b1, 0x1);
    ret += imx678_write_register(vi_pipe, 0x47b2, 0x4a);
    ret += imx678_write_register(vi_pipe, 0x47b3, 0x1);
    ret += imx678_write_register(vi_pipe, 0x47b4, 0x3f);
    ret += imx678_write_register(vi_pipe, 0x47b6, 0x49);
    ret += imx678_write_register(vi_pipe, 0x47bc, 0xfb);
    ret += imx678_write_register(vi_pipe, 0x47be, 0xc);
    ret += imx678_write_register(vi_pipe, 0x47c0, 0x32);
    ret += imx678_write_register(vi_pipe, 0x47c1, 0x1);
    ret += imx678_write_register(vi_pipe, 0x47c2, 0x3e);
    ret += imx678_write_register(vi_pipe, 0x47c3, 0x1);
/* sub 0x3e00 */
    ret += imx678_write_register(vi_pipe, 0x4550, 0x6);
    /* release standby / start master */
    ret += imx678_write_register(vi_pipe, 0x3000, 0x00);
    delay_ms(20);
    ret += imx678_write_register(vi_pipe, 0x3002, 0x00);
    if (ret != TD_SUCCESS) { isp_err_trace("imx678 write register failed!\n"); return; }
    printf("===IMX678 LINEAR 4K30 Init OK (blob pathA full 376)!===\n");
    return;
}
