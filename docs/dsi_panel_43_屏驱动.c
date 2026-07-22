/*
 * dsi_panel_43.c —— Waveshare 4.3" 800x480 DSI LCD 驱动 (Hi3519DV500 VO + mipi_tx)
 * 甜源创新 IMX678 相机 —— 屏显示扩展 (回国接硬件后调试)
 *
 * 屏规格 (从树莓派内核 panel-waveshare-dsi / vc4-kms-dsi-waveshare-800x480 overlay
 *         + rpi-panel-attiny-regulator.c 提取, 已交叉核对):
 *   - 800x480 RGB888, MIPI-DSI VIDEO 模式, 桥片 TC358762 (DSI->RGB)
 *   - 像素时钟 27.777 MHz, 60Hz
 *   - 时序: hsync=2 hbp=45 hact=800 hfp=59 ; vsync=2 vbp=22 vact=480 vfp=7
 *   - data-lane = 1 (树莓派用 1 lane; 座子引出 2 条, SoC 可配 1/2/4, 悬空多余的)
 *   - 伴随芯片 ATTINY 在 I2C 地址 0x45: 上电时序 + 背光 (无 DSI DCS 初始化)
 *
 * ⚠ 需硬件到位后确认/调的项 (下面都标了 TODO):
 *   [HW1] ✅已确认: 板子 J5 引出 MIPI TX(DSI) 4-lane, 与 BT656 复用.
 *         接法: 屏DP0/DN0<->J5 pin17/18, 屏CP/CN<->J5 pin14/15,
 *               屏SCL0/SDA0<->J5 pin24/23(I2C2), GND<->J5 3/8/13/16/21.
 *         ⚠ J5 无 3V3! 屏 3V3@~400mA 需从板子别处取.
 *         ⚠ 信号完整性: D0/CLK 两对差分线务必超短+绞合.
 *   [HW2] ✅已确认: I2C2 -> 见 PANEL_I2C_BUS. (上电后 i2cdetect -y 2 应见 0x45)
 *   [HW3] VO user sync_info 的 18 字段顺序 (按 SDK ot_vo 头文件的 ot_vo_sync_info 核对)
 *   [HW4] phy_data_rate / PLL 微调 (出图花屏/偏色时调)
 *
 * 集成: 把相机 VPSS 某个通道 sample_comm_vpss_bind_vo() 到本 VO layer, 即可屏上出实时画面.
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include "sample_comm.h"

/* ---------------- [HW2] attiny(0x45) 电源/背光 —— I2C ---------------- */
#define PANEL_I2C_BUS   "/dev/i2c-2"   /* J5 的 I2C2_SDA/SCL(pin23/24); 若非i2c-2, i2cdetect 各总线找 0x45 */
#define ATTINY_ADDR     0x45
/* attiny 寄存器 (rpi-panel-attiny-regulator.c) */
#define REG_PORTA 0x81
#define REG_PORTB 0x82
#define REG_PORTC 0x83
#define REG_POWERON 0x85
#define REG_PWM   0x86
#define PA_LCD_LR       0x04  /* BIT2 方向 */
#define PB_LCD_MAIN     0x80  /* BIT7 主电源+屏电 */
#define PC_LED_EN       0x01  /* BIT0 背光LED使能 */
#define PC_RST_TP_N     0x02  /* BIT1 触摸复位(高=放开) */
#define PC_RST_LCD_N    0x04  /* BIT2 LCD复位 */
#define PC_RST_BRIDGE_N 0x08  /* BIT3 TC358762桥复位 */

static int attiny_wr(int fd, unsigned char reg, unsigned char val)
{
    unsigned char b[2] = { reg, val };
    struct i2c_msg m = { .addr = ATTINY_ADDR, .flags = 0, .len = 2, .buf = b };
    struct i2c_rdwr_ioctl_data x = { .msgs = &m, .nmsgs = 1 };
    return ioctl(fd, I2C_RDWR, &x) < 0 ? -1 : 0;
}

/* 上电点亮屏 (attiny 时序, 严格照 attiny_lcd_power_enable) */
int panel_power_on(int brightness /* 0..255 */)
{
    int fd = open(PANEL_I2C_BUS, O_RDWR);
    if (fd < 0) { perror("open i2c"); return -1; }
    attiny_wr(fd, REG_PORTC, 0x00);                 usleep(8000);   /* 桥/触摸保持复位 */
    attiny_wr(fd, REG_PORTA, PA_LCD_LR);            usleep(8000);   /* 方向 */
    attiny_wr(fd, REG_PORTB, PB_LCD_MAIN);          usleep(8000);   /* 主电源 + 屏供电 */
    /* 放开 LCD/桥/触摸复位 + 使能背光LED (RST_*_N 高=放开) */
    attiny_wr(fd, REG_PORTC, PC_LED_EN | PC_RST_LCD_N | PC_RST_BRIDGE_N | PC_RST_TP_N);
    usleep(80000);                                                  /* 桥片稳定 80ms */
    /* 背光: PWM 寄存器, 0xff-亮度 (树莓派用反相); 若反了改成直接 brightness */
    attiny_wr(fd, REG_PWM, (unsigned char)(0xff - (brightness & 0xff)));
    close(fd);
    return 0;
}

int panel_power_off(void)
{
    int fd = open(PANEL_I2C_BUS, O_RDWR);
    if (fd < 0) { return -1; }
    attiny_wr(fd, REG_PWM, 0x00);          usleep(8000);
    attiny_wr(fd, REG_PORTA, 0x00);        usleep(8000);
    attiny_wr(fd, REG_PORTB, 0x02);        usleep(8000);  /* PB_LCD_VCC_N */
    attiny_wr(fd, REG_PORTC, 0x00);        usleep(30000);
    close(fd);
    return 0;
}

/* ---------------- VO + mipi_tx 配置 (适配 sample_vo 的结构) ---------------- */
/* 无 DSI DCS 初始化: 桥片由 attiny 上电搞定, cmd_count=0 */
static const sample_vo_mipi_tx_cfg g_panel_43_cfg = {
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
        /* TODO[HW3]: 18 字段顺序按 ot_vo 头 ot_vo_sync_info 核对.
         * 依 1200x1920/320x240 例子推位置:
         * {?,iop,itf, vact, vbp, vfp, hact, hbp, hfp, ?, ?, ?, ?, hpw, vpw, ?, idv, ?} */
        .sync_info = {0, 1, 1, 480, 22, 7, 800, 45, 59, 1, 400, 9, 7, 2, 2, 0, 0, 0},
        .user_sync = {
            .auto_user_sync_info.pixel_clk = 27777, /* KHz, AUTO 模式 SDK 自算 PLL */
            .clk_reverse_en = TD_FALSE,
            .op_mode = OT_OP_MODE_AUTO,
        },
        .dev_frame_rate = SAMPLE_VO_DEV_FRAME_RATE,
    },
    .tx_config = {
        .intf_sync = OT_MIPI_TX_OUT_USER,
        .cmd_count = 0,          /* 无 DSI 初始化命令 (桥片走 I2C) */
        .cmd_info = NULL,
        .combo_dev_cfg = {
            .devno = 0,
            .lane_id = {0, -1, -1, -1},        /* [HW/lane] 1 lane; 2 lane 用 {0,1,-1,-1} */
            .out_mode = OUT_MODE_DSI_VIDEO,
            .out_format = OUT_FORMAT_RGB_24BIT, /* RGB888 */
            .video_mode = BURST_MODE,           /* 花屏可试 NON_BURST_MODE_SYNC_PULSES */
            .sync_info = {
                .hact = 800, .hfp = 59, .hbp = 45, .hpw = 2,
                .vact = 480, .vfp = 7,  .vbp = 22, .vpw = 2,
            },
            .phy_data_rate = 720,   /* Mbps/lane (27.777M*24/1≈666, 留余量; TODO[HW4] 微调) */
            .pixel_clk = 27777,     /* KHz */
        },
    },
};

/* 启动屏: 先 attiny 上电, 再起 VO + mipi_tx (sample_comm 里的助手) */
int panel_43_start(void)
{
    int ret;
    panel_power_on(200);   /* 先给屏上电+背光, 桥片就绪 */
    ret = sample_comm_vo_start_vo(&g_panel_43_cfg.vo_config);
    if (ret != TD_SUCCESS) { printf("start vo fail 0x%x\n", ret); return ret; }
    ret = sample_comm_start_mipi_tx(&g_panel_43_cfg.tx_config);
    if (ret != TD_SUCCESS) { printf("start mipi_tx fail 0x%x\n", ret); return ret; }
    printf("[panel] 4.3in 800x480 DSI up. 现在把相机 VPSS 通道 bind 到 VO layer %d 即出画面\n",
           g_panel_43_cfg.vo_config.vo_layer);
    /* TODO: sample_comm_vpss_bind_vo(vpss_grp, vpss_chn,
     *            g_panel_43_cfg.vo_config.vo_dev, g_panel_43_cfg.vo_config.vo_layer); */
    return TD_SUCCESS;
}

void panel_43_stop(void)
{
    sample_comm_stop_mipi_tx(g_panel_43_cfg.vo_config.vo_intf_type);
    sample_comm_vo_stop_vo(&g_panel_43_cfg.vo_config);
    panel_power_off();
}
