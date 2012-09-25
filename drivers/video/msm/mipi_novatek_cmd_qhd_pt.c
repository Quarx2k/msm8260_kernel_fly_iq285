/* Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include "msm_fb.h"
#include "mipi_dsi.h"
#include "mipi_novatek.h"

static struct msm_panel_info pinfo;

#define DSI_BIT_CLK_500MHZ //64 FPS over(Vsync TRUE)
//#define DSI_BIT_CLK_468MHZ // 60 FPS(Vsync TRUE)
//#define DSI_BIT_CLK_454MHZ //70 FPS over(Vsync FALSE)
//#define DSI_BIT_CLK_400MHZ // 64 FPS under(Vsync FALSE)
//#define DSI_BIT_CLK_380MHZ //60 FPS under(Vsync FALSE)

static struct mipi_dsi_phy_ctrl dsi_cmd_mode_phy_db = {

/* DSI_BIT_CLK at 500Mhz, 2 lane, RGB888 */
#if defined(DSI_BIT_CLK_500MHZ)
                {0x03, 0x01, 0x01, 0x00},       /* regulator */
                /* timing   */
                {0xB9, 0x8E, 0x20, 0x00, 0x23, 0x96, 0x23,
                0x90, 0x23, 0x03, 0x04},
                {0x7f, 0x00, 0x00, 0x00},       /* phy ctrl */
                {0xee, 0x02, 0x86, 0x00},       /* strength */
                /* pll control */
                {0x41, 0xE0, 0x31, 0xd9, 0x00, 0x50, 0x48, 0x63,
                0x31, 0x0f, 0x07,
                0x05, 0x14, 0x03, 0x0, 0x0, 0x54, 0x06, 0x10, 0x04, 0x0},
#endif

/* DSI_BIT_CLK at 480MHz, 2 lane, RGB888 */
#if defined(DSI_BIT_CLK_480MHZ)
                {0x03, 0x01, 0x01, 0x00},       /* regulator */
                /* timing   */
                {0xB4, 0x8D, 0x1D, 0x00, 0x20, 0x94, 0x20,
                0x8F, 0x20, 0x03, 0x04},
                {0x7f, 0x00, 0x00, 0x00},       /* phy ctrl */
                {0xee, 0x02, 0x86, 0x00},       /* strength */
                /* pll control */
                {0x41, 0xDF, 0x31, 0xda, 0x00, 0x50, 0x48, 0x63,
                0x31, 0x0f, 0x07,
                0x05, 0x14, 0x03, 0x0, 0x0, 0x54, 0x06, 0x10, 0x04, 0x0},
#endif

/* DSI_BIT_CLK at 454MHz, 2 lane, RGB888 */
#if defined(DSI_BIT_CLK_454MHZ)
		{0x03, 0x01, 0x01, 0x00},	/* regulator */
		/* timing   */
		{0xB4, 0x8D, 0x1D, 0x00, 0x20, 0x94, 0x20,
		0x8F, 0x20, 0x03, 0x04},
		{0x7f, 0x00, 0x00, 0x00},	/* phy ctrl */
		{0xee, 0x02, 0x86, 0x00},	/* strength */
		/* pll control */
		{0x40, 0xf9, 0xb0, 0xda, 0x00, 0x50, 0x48, 0x63,
#if defined(NOVATEK_TWO_LANE)
		0x30, 0x07, 0x03,
#else           /* default set to 1 lane */
		0x30, 0x07, 0x07,
#endif
		0x05, 0x14, 0x03, 0x0, 0x0, 0x54, 0x06, 0x10, 0x04, 0x0},

/* DSI_BIT_CLK at 400MHz, 2 lane, RGB888 */
#elif defined(DSI_BIT_CLK_400MHZ)
		{0x03, 0x01, 0x01, 0x00},	/* regulator */
		/* timing   */
		{0x64, 0x1e, 0x14, 0x00, 0x2d, 0x23, 0x1e, 0x1c,
		0x0b, 0x13, 0x04},
		{0x7f, 0x00, 0x00, 0x00},	/* phy ctrl */
		{0xee, 0x03, 0x86, 0x03},	/* strength */
		{0x41, 0x8f, 0xb1, 0xda, 0x00, 0x50, 0x48, 0x63,
		0x31, 0x0f, 0x07,
		0x05, 0x14, 0x03, 0x03, 0x03, 0x54, 0x06, 0x10, 0x04, 0x03 },		
		
/* DSI_BIT_CLK at 380MHz, 2 lane, RGB888 */
#elif defined(DSI_BIT_CLK_380MHZ)
		{0x03, 0x01, 0x01, 0x00},	/* regulator */
		/* timing   */
		{0x79, 0x25, 0x1F, 0x00, 0x4C, 0x34, 0x25, 0x25,
		0x1C, 0x03, 0x04},
		{0x7f, 0x00, 0x00, 0x00},	/* phy ctrl */
		{0xee, 0x03, 0x86, 0x03},	/* strength */
		/* pll control */
		{0x41, 0xf7, 0xb2, 0xf5, 0x00, 0x50, 0x48, 0x63,
		0x31, 0x0f, 0x07,
		0x05, 0x14, 0x03, 0x03, 0x03, 0x54, 0x06, 0x10, 0x04, 0x03 },
#endif		
};

static int __init mipi_cmd_novatek_blue_qhd_pt_init(void)
{
	int ret;

#ifdef CONFIG_FB_MSM_MIPI_PANEL_DETECT
	if (msm_fb_detect_client("mipi_cmd_novatek_qhd"))
		return 0;
#endif

	pinfo.xres = 540;
	pinfo.yres = 960;
	pinfo.type = MIPI_CMD_PANEL;
	pinfo.pdest = DISPLAY_1;
	pinfo.wait_cycle = 0;
	pinfo.bpp = 24;
	pinfo.lcdc.h_back_porch = 50;
	pinfo.lcdc.h_front_porch = 50;
	pinfo.lcdc.h_pulse_width = 20;
	pinfo.lcdc.v_back_porch = 11;
	pinfo.lcdc.v_front_porch = 10;
	pinfo.lcdc.v_pulse_width = 5;
	pinfo.lcdc.border_clr = 0;	/* blk */
	pinfo.lcdc.underflow_clr = 0xff;	/* blue */
	pinfo.lcdc.hsync_skew = 0;
	pinfo.bl_max = 28; // KT Tech : By Jhoonkim - 15;
	pinfo.bl_min = 1;
	pinfo.fb_num = 2;
#if defined(DSI_BIT_CLK_500MHZ)
	pinfo.clk_rate = 500000000;
#elif defined(DSI_BIT_CLK_480MHZ)
	pinfo.clk_rate = 500000000;
#elif defined(DSI_BIT_CLK_454MHZ)
	pinfo.clk_rate = 454000000;
#elif defined(DSI_BIT_CLK_400MHZ)
	pinfo.clk_rate = 400000000;
#elif defined(DSI_BIT_CLK_380MHZ)
	pinfo.clk_rate = 380000000;
#endif
#ifdef CONFIG_MACH_KTTECH
	pinfo.lcd.vsync_enable = TRUE; /* You need to check : mdp4_overlay.c */
	pinfo.lcd.hw_vsync_mode = TRUE;
	pinfo.lcd.refx100 = 6400; /* adjust refx100 to prevent tearing  */
#else
	pinfo.is_3d_panel = FB_TYPE_3D_PANEL;
	pinfo.lcd.vsync_enable = TRUE;
	pinfo.lcd.hw_vsync_mode = TRUE;
	pinfo.lcd.refx100 = 6000; /* adjust refx100 to prevent tearing */
#endif

	pinfo.mipi.mode = DSI_CMD_MODE;
	pinfo.mipi.dst_format = DSI_CMD_DST_FORMAT_RGB888;
	pinfo.mipi.vc = 0;
	pinfo.mipi.rgb_swap = DSI_RGB_SWAP_BGR;
	pinfo.mipi.data_lane0 = TRUE;
#if defined(NOVATEK_TWO_LANE)
	pinfo.mipi.data_lane1 = TRUE;
#endif
	pinfo.mipi.t_clk_post = 0x22;
	pinfo.mipi.t_clk_pre = 0x3f;
	pinfo.mipi.stream = 0;	/* dma_p */
	pinfo.mipi.mdp_trigger = DSI_CMD_TRIGGER_SW;
	pinfo.mipi.dma_trigger = DSI_CMD_TRIGGER_SW;
	pinfo.mipi.te_sel = 1; /* TE from vsycn gpio */
	pinfo.mipi.interleave_max = 1;
	pinfo.mipi.insert_dcs_cmd = TRUE;
	pinfo.mipi.wr_mem_continue = 0x3c;
	pinfo.mipi.wr_mem_start = 0x2c;
	pinfo.mipi.dsi_phy_db = &dsi_cmd_mode_phy_db;

	ret = mipi_novatek_device_register(&pinfo, MIPI_DSI_PRIM,
						MIPI_DSI_PANEL_WVGA_PT);
	if (ret)
		pr_err("%s: failed to register device!\n", __func__);

	return ret;
}

module_init(mipi_cmd_novatek_blue_qhd_pt_init);
