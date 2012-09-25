#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/wakelock.h>
#include <linux/android_pmem.h>
#include <linux/gpio.h>
#include <linux/mutex.h>

#include <linux/mfd/msm-adie-codec.h>
#include <linux/mfd/marimba.h>
#include <linux/msm_audio.h>

#include <mach/qdsp6v2/wm8993_kttech.h>
#include <mach/qdsp6v2/audio_dev_ctl.h>

/********** FEATURE **********/
#define WM8993_CHECK_RESET
#define WM8993_CODEC_FAST_CALL_REGISTER

#ifdef KTTECH_FINAL_BUILD //log blocking
#undef pr_info
#define pr_info(fmt, args...)
#endif

static AMP_PATH_TYPE_E	m_curr_path 	= AMP_PATH_NONE;
static AMP_PATH_TYPE_E	m_prev_path 	= AMP_PATH_NONE;
static AMP_CAL_TYPE_E	m_cal_type 		= AMP_CAL_NORMAL;
#ifndef WM8993_SINGLE_PATH
static AMP_PATH_TYPE_E	cur_tx = AMP_PATH_NONE;
static AMP_PATH_TYPE_E	cur_rx = AMP_PATH_NONE;
#endif /*WM8993_SINGLE_PATH*/

#define WM8993_RESET_REG		0x00
#define WM8993_RESET_VALUE		0x8993
#define WM8993_VOICEVOL_REG		0x20
#define WM8993_IDAC_REG			0x57
#define WM8993_SPK_SW_GPIO		105

struct wm8993_dev {
	struct mutex lock;
	uint32_t cal_type; // voip, video, etc(voice) call
	uint32_t eq_type;
	uint32_t mute_enabled;
	uint32_t voip_micgain;
	uint32_t voice_volume;
};

static struct mutex path_lock;
static u8 isspksw;
#ifdef WM8993_CHECK_RESET // Reset 중복 방지를 위해 체크한다.
static u8 isreset;
#endif /*WM8993_CHECK_RESET*/
//#ifdef WM8993_WHITE_NOISE_ZERO
static u8 amp_enabled;
//#endif
static struct wm8993_dev * wm8993_modules;

REG_MEMORY wm8993_register_type amp_none_path[REG_COUNT] = {
	{ 0x00 , 0x8993 },
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY wm8993_register_type amp_handset_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x1f , 0x0000 },
	{ 0x33 , 0x0011 },
	{ 0x20 , 0x0179 },
	{ 0x21 , 0x0179 },
	{ 0x2d , 0x0001 },
	{ 0x2e , 0x0001 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ AMP_REGISTER_DELAY , 50 },
	{ 0x07 , 0x0000 },
	{ 0x04 , 0x4010 },
	{ 0x01 , 0x0803 },
	{ 0x03 , 0x00f3 },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x05 , 0x4000 },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY wm8993_register_type amp_headset_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x1c , 0x0176 },
	{ 0x1d , 0x0176 },
	{ 0x60 , 0x0000 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ AMP_REGISTER_DELAY , 33 },
	{ 0x07 , 0x0000 },
	{ 0x04 , 0x4010 },
	{ AMP_REGISTER_DELAY , 10 },
	{ 0x44 , 0x0003 },
	{ 0x56 , 0x0003 },
	{ 0x44 , 0x0000 },
	{ 0x55 , 0x03e0 },
	{ 0x01 , 0x0303 },
	{ 0x60 , 0x0022 },
	{ 0x4c , 0x9f25 },
	{ AMP_REGISTER_DELAY , 5 },
	{ 0x03 , 0x00f3 },
	{ 0x05 , 0x8000 }, // L,R Switching
	{ 0x2d , 0x0100 },
	{ 0x2e , 0x0100 },
	{ 0x54 , 0x0303 },
	{ AMP_REGISTER_DELAY , 160 },
	{ 0x57 , 0x00fd },
	{ 0x54 , 0x000f },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x62 , 0x0001 },
	{ 0x63 , 0x000c },
	{ 0x64 , 0x000c },
	{ 0x65 , 0x000c },
	{ 0x66 , 0x000c },
	{ 0x67 , 0x000c },
	{ 0x68 , 0x0fca },
	{ 0x69 , 0x0400 },
	{ 0x6a , 0x00d8 },
	{ 0x6b , 0x1eb5 },
	{ 0x6c , 0xf145 },
	{ 0x6d , 0x0b75 },
	{ 0x6e , 0x01c5 },
	{ 0x6f , 0x1c58 },
	{ 0x70 , 0xf373 },
	{ 0x71 , 0x0a54 },
	{ 0x72 , 0x0558 },
	{ 0x73 , 0x168e },
	{ 0x74 , 0xf829 },
	{ 0x75 , 0x07ad },
	{ 0x76 , 0x1103 },
	{ 0x77 , 0x0564 },
	{ 0x78 , 0x0559 },
	{ 0x79 , 0x4000 },
	{ 0x60 , 0x00ee },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY wm8993_register_type amp_speaker_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x22 , 0x0000 },
	{ 0x23 , 0x0000 },
	{ 0x24 , 0x0018 },
	{ 0x25 , 0x0138 },
	{ 0x26 , 0x0173 },
	{ 0x27 , 0x0173 },
	{ 0x36 , 0x0003 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ 0x7b , 0xc718 },
	{ 0x7c , 0x1124 },
	{ 0x7d , 0x2c80 },
	{ 0x7e , 0x4600 },
	{ AMP_REGISTER_DELAY , 50 },
	{ 0x07 , 0x0000 },
	{ 0x04 , 0x4010 },
	{ 0x01 , 0x3003 },
	{ 0x03 , 0x0333 },
	{ 0x62 , 0x0001 },
	{ 0x63 , 0x0001 },
	{ 0x64 , 0x000b },
	{ 0x65 , 0x000f },
	{ 0x66 , 0x000d },
	{ 0x6f , 0x1b18 },
	{ 0x70 , 0xf48a },
	{ 0x71 , 0x040a },
	{ 0x72 , 0x11fa },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x05 , 0x4000 },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY wm8993_register_type amp_headset_speaker_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x05 , 0x4000 },
	{ 0x1c , 0x0176 },
	{ 0x1d , 0x0176 },
	{ 0x60 , 0x0000 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ AMP_REGISTER_DELAY , 33 },
	{ 0x07 , 0x0000 },
	{ 0x04 , 0x4010 },
	{ AMP_REGISTER_DELAY , 10 },
	{ 0x44 , 0x0003 },
	{ 0x56 , 0x0003 },
	{ 0x44 , 0x0000 },
	{ 0x55 , 0x03e0 },
	{ 0x01 , 0x0303 },
	{ 0x60 , 0x0022 },
	{ 0x4c , 0x9f25 },
	{ AMP_REGISTER_DELAY , 5 },
	{ 0x03 , 0x00f3 },
	{ 0x05 , 0x8000 }, // L,R Switching
	{ 0x2d , 0x0100 },
	{ 0x2e , 0x0100 },
	{ 0x54 , 0x0303 },
	{ AMP_REGISTER_DELAY , 160 },
	{ 0x57 , 0x00fd },
	{ 0x54 , 0x000f },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x60 , 0x00ee },
	{ 0x22 , 0x0000 },
	{ 0x23 , 0x0000 },
	{ 0x24 , 0x0018 },
	{ 0x25 , 0x0138 },
	{ 0x26 , 0x0173 },
	{ 0x27 , 0x0173 },
	{ 0x36 , 0x0003 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x3303 },
	{ 0x7b , 0xc718 },
	{ 0x7c , 0x1124 },
	{ 0x7d , 0x2c80 },
	{ 0x7e , 0x4600 },
	{ AMP_REGISTER_DELAY , 50 },
	{ 0x03 , 0x03f3 },
	{ 0x62 , 0x0001 },
	{ 0x63 , 0x0001 },
	{ 0x64 , 0x000b },
	{ 0x65 , 0x000f },
	{ 0x66 , 0x000d },
	{ 0x6f , 0x1b18 },
	{ 0x70 , 0xf48a },
	{ 0x71 , 0x040a },
	{ 0x72 , 0x11fa },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY wm8993_register_type amp_mainmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x07 , 0x8000 },
	{ AMP_REGISTER_DELAY , 50 },
	{ 0x01 , 0x0003 },
	{ 0x04 , 0x0010 },
	{ 0x28 , 0x0010 },
	{ 0x29 , 0x0030 },
	{ 0x0f , 0x01c0 },
	{ 0x18 , 0x010b },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x02 , 0x6243 },
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY wm8993_register_type amp_earmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x07 , 0x8000 },
	{ AMP_REGISTER_DELAY , 50 },
	{ 0x01 , 0x0023 },
	{ 0x04 , 0xc010 },
	{ 0x10 , 0x01c0 },
	{ 0x1a , 0x010b },
	{ 0x28 , 0x0001 },
	{ 0x2a , 0x0030 },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x02 , 0x6113 },
	{ AMP_REGISTER_END	 ,	0 },
};

REG_MEMORY wm8993_register_type amp_headset_earmic_loop_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x1c , 0x0173 },  // 20110317 by ssgun - volume up : 0x0176 -> 0x016d
	{ 0x1d , 0x0176 },  // 20110317 by ssgun - volume up : 0x0176 -> 0x016d
	{ 0x1a , 0x010f },
	{ 0x60 , 0x0000 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ 0x20 , 0x0179 },
	{ 0x21 , 0x0179 },
	{ AMP_REGISTER_DELAY , 33 },
	{ 0x07 , 0x0000 },
#if 0 // 20110322 by ssgun - enable earmic
	{ 0x04 , 0x4010 },
#else
	{ 0x04 , 0xc010 },
#endif
	{ AMP_REGISTER_DELAY , 10 },
	{ 0x44 , 0x0003 },
	{ 0x56 , 0x0003 },
	{ 0x44 , 0x0000 },
	{ 0x55 , 0x03e0 },
	{ 0x01 , 0x0323 },
	{ 0x60 , 0x0022 },
	{ 0x4c , 0x9f25 },
	{ AMP_REGISTER_DELAY , 5 },
	{ 0x03 , 0x00f3 },
	{ 0x28 , 0x0001 },
	{ 0x2a , 0x0020 },
	{ 0x2d , 0x0100 },
	{ 0x2e , 0x0100 },
	{ 0x10 , 0x01c0 },
	{ 0x54 , 0x0303 },
	{ AMP_REGISTER_DELAY , 160 },
	{ 0x57 , 0x00fd },
	{ 0x54 , 0x000f },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x60 , 0x00ee },
	{ 0x05 , 0x4001 },
	{ 0x02 , 0x6111 },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY wm8993_register_type amp_speaker_mainmic_loop_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
#if 0 // 20110510 by ssgun - volume down
	{ 0x22 , 0x0000 },
	{ 0x23 , 0x0000 },
	{ 0x24 , 0x0018 },
	{ 0x25 , 0x0138 },
	{ 0x26 , 0x0174 },
	{ 0x27 , 0x0174 },
	{ 0x36 , 0x0003 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ 0x0f , 0x01d0 },
	{ 0x20 , 0x0179 },
	{ 0x21 , 0x0179 },
	{ 0x7b , 0xc718 },
	{ 0x7c , 0x1124 },
	{ 0x7d , 0x2c80 },
	{ 0x7e , 0x4600 },
	{ AMP_REGISTER_DELAY , 50 },
	{ 0x07 , 0x0000 },
	{ 0x04 , 0x0010 },
	{ 0x01 , 0x3033 },
	{ 0x03 , 0x0333 },
	{ 0x01 , 0x3013 },
	{ 0x18 , 0x010b },
	{ 0x28 , 0x0010 },
	{ 0x29 , 0x0020 },
	{ 0x62 , 0x0001 },
	{ 0x63 , 0x0001 },
	{ 0x64 , 0x000b },
	{ 0x65 , 0x000f },
	{ 0x66 , 0x000d },
	{ 0x6f , 0x1b18 },
	{ 0x70 , 0xf48a },
	{ 0x71 , 0x040a },
	{ 0x72 , 0x11fa },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x05 , 0x4001 },
	{ 0x02 , 0x6242 },
	{ 0x0a , 0x0000 },
#else
	{ 0x22 , 0x0000},
	{ 0x23 , 0x0000},
	{ 0x24 , 0x0018},
	{ 0x25 , 0x0138},
	{ 0x26 , 0x0171},
	{ 0x27 , 0x0171},
	{ 0x36 , 0x0003},
	{ 0x39 , 0x0068},
	{ 0x01 , 0x0003},
	{ 0x0f , 0x01d0},
	{ 0x20 , 0x0179},
	{ 0x21 , 0x0179},
	{ 0x7b , 0xc718},
	{ 0x7c , 0x1124},
	{ 0x7d , 0x2c80},
	{ 0x7e , 0x4600},
	{ AMP_REGISTER_DELAY , 50 },
	{ 0x07 , 0x0000},
	{ 0x04 , 0x0010},
	{ 0x01 , 0x3003},
	{ 0x03 , 0x0333},
	{ 0x01 , 0x3013},
	{ 0x18 , 0x010b},
	{ 0x28 , 0x0010},
	{ 0x29 , 0x0020},
	{ 0x62 , 0x0001},
	{ 0x63 , 0x0001},
	{ 0x64 , 0x000b},
	{ 0x65 , 0x000f},
	{ 0x66 , 0x000d},
	{ 0x6f , 0x1b18},
	{ 0x70 , 0xf48a},
	{ 0x71 , 0x040a},
	{ 0x72 , 0x11fa},
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x05 , 0x4001},
	{ 0x02 , 0x6243},
	{ 0x0a , 0x0130},
#endif
	{ AMP_REGISTER_END   ,  0 },
};

// VOICE CALL
REG_MEMORY wm8993_register_type amp_call_handset_mainmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x07 , 0x0000 },
	{ 0x1f , 0x0000 },
	{ 0x33 , 0x0018 },
	{ 0x20 , 0x017f },
	{ 0x21 , 0x017f },
	{ 0x2d , 0x0001 },
	{ 0x2e , 0x0001 },
	{ 0x28 , 0x0010 },
	{ 0x29 , 0x0030 },
	{ 0x0f , 0x01c0 },
	{ 0x10 , 0x01c0 },
	{ 0x18 , 0x0106 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ AMP_REGISTER_DELAY , 50 },
	{ 0x04 , 0x4010 },
	{ 0x01 , 0x0813 },
	{ 0x03 , 0x00f3 },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x05 , 0x4000 },
	{ 0x0a , 0x0130 },
	{ 0x02 , 0x6342 },
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY wm8993_register_type amp_call_speaker_mainmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x22 , 0x0000 },
	{ 0x23 , 0x0000 },
	{ 0x24 , 0x0018 },
	{ 0x25 , 0x0138 },
	{ 0x26 , 0x0174 },
	{ 0x27 , 0x0174 },
	{ 0x36 , 0x0003 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ 0x0f , 0x01c0 },
	{ 0x20 , 0x0179 },
	{ 0x21 , 0x0179 },
	{ 0x7b , 0xc718 },
	{ 0x7c , 0x1124 },
	{ 0x7d , 0x2c80 },
	{ 0x7e , 0x4600 },
	{ AMP_REGISTER_DELAY , 50 },
	{ 0x07 , 0x0000 },
	{ 0x04 , 0x4010 },
	{ 0x01 , 0x3003 },
	{ 0x03 , 0x0333 },
	{ 0x01 , 0x3013 },
	{ 0x18 , 0x010b },
	{ 0x28 , 0x0010 },
	{ 0x29 , 0x0030 },
	{ 0x62 , 0x0001 },
	{ 0x63 , 0x0001 },
	{ 0x64 , 0x000b },
	{ 0x65 , 0x000f },
	{ 0x66 , 0x000d },
	{ 0x6f , 0x1b18 },
	{ 0x70 , 0xf48a },
	{ 0x71 , 0x040a },
	{ 0x72 , 0x11fa },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x05 , 0x4000 },
	{ 0x02 , 0x6242 },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END	 ,	0 },
};

REG_MEMORY wm8993_register_type amp_call_headset_mainmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x1c , 0x0179 },
	{ 0x1d , 0x0179 },
	{ 0x18 , 0x010b },
	{ 0x60 , 0x0000 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ 0x20 , 0x0179 },
	{ 0x21 , 0x0179 },
	{ AMP_REGISTER_DELAY , 33 },
	{ 0x07 , 0x0000 },
	{ 0x04 , 0x4010 },
	{ AMP_REGISTER_DELAY , 10 },
	{ 0x44 , 0x0003 },
	{ 0x56 , 0x0003 },
	{ 0x44 , 0x0000 },
	{ 0x55 , 0x03e0 },
	{ 0x01 , 0x0313 },
	{ 0x60 , 0x0022 },
	{ 0x4c , 0x9f25 },
	{ AMP_REGISTER_DELAY , 5 },
	{ 0x03 , 0x00f3 },
	{ 0x28 , 0x0010 },
	{ 0x29 , 0x0030 },
	{ 0x2d , 0x0100 },
	{ 0x2e , 0x0100 },
	{ 0x0f , 0x01c0 },
	{ 0x54 , 0x0303 },
	{ AMP_REGISTER_DELAY , 160 },
	{ 0x57 , 0x00fd },
	{ 0x54 , 0x000f },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x62 , 0x0001 },
	{ 0x63 , 0x000c },
	{ 0x64 , 0x000c },
	{ 0x65 , 0x000c },
	{ 0x66 , 0x000c },
	{ 0x67 , 0x000c },
	{ 0x68 , 0x0fca },
	{ 0x69 , 0x0400 },
	{ 0x6a , 0x00d8 },
	{ 0x6b , 0x1eb5 },
	{ 0x6c , 0xf145 },
	{ 0x6d , 0x0b75 },
	{ 0x6e , 0x01c5 },
	{ 0x6f , 0x1c58 },
	{ 0x70 , 0xf373 },
	{ 0x71 , 0x0a54 },
	{ 0x72 , 0x0558 },
	{ 0x73 , 0x168e },
	{ 0x74 , 0xf829 },
	{ 0x75 , 0x07ad },
	{ 0x76 , 0x1103 },
	{ 0x77 , 0x0564 },
	{ 0x78 , 0x0559 },
	{ 0x79 , 0x4000 },
	{ 0x60 , 0x00ee },
	{ 0x02 , 0x6242 },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY wm8993_register_type amp_call_headset_earmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x1c , 0x0179 },
	{ 0x1d , 0x0179 },
	{ 0x1a , 0x010b },
	{ 0x60 , 0x0000 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ 0x20 , 0x0179 },
	{ 0x21 , 0x0179 },
	{ AMP_REGISTER_DELAY , 33 },
	{ 0x04 , 0xc010 },
	{ 0x44 , 0x0003 },
	{ 0x56 , 0x0003 },
	{ 0x44 , 0x0000 },
	{ 0x55 , 0x03e0 },
	{ 0x01 , 0x0323 },
	{ 0x60 , 0x0022 },
	{ 0x4c , 0x9f25 },
	{ AMP_REGISTER_DELAY , 5 },
	{ 0x03 , 0x00f3 },
	{ 0x28 , 0x0001 },
	{ 0x2a , 0x0030 },
	{ 0x2d , 0x0100 },
	{ 0x2e , 0x0100 },
	{ 0x10 , 0x01c0 },
	{ 0x54 , 0x0303 },
	{ AMP_REGISTER_DELAY , 160 },
	{ 0x57 , 0x00fd },
	{ 0x54 , 0x000f },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x62 , 0x0001 },
	{ 0x63 , 0x000c },
	{ 0x64 , 0x000c },
	{ 0x65 , 0x000c },
	{ 0x66 , 0x000c },
	{ 0x67 , 0x000c },
	{ 0x68 , 0x0fca },
	{ 0x69 , 0x0400 },
	{ 0x6a , 0x00d8 },
	{ 0x6b , 0x1eb5 },
	{ 0x6c , 0xf145 },
	{ 0x6d , 0x0b75 },
	{ 0x6e , 0x01c5 },
	{ 0x6f , 0x1c58 },
	{ 0x70 , 0xf373 },
	{ 0x71 , 0x0a54 },
	{ 0x72 , 0x0558 },
	{ 0x73 , 0x168e },
	{ 0x74 , 0xf829 },
	{ 0x75 , 0x07ad },
	{ 0x76 , 0x1103 },
	{ 0x77 , 0x0564 },
	{ 0x78 , 0x0559 },
	{ 0x79 , 0x4000 },
	{ 0x60 , 0x00ee },
	{ 0x02 , 0x6111 },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END   ,  0 },
};

// VOIP CALL
REG_MEMORY wm8993_register_type amp_voip_handset_mainmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x07 , 0x0000 },
	{ 0x1f , 0x0000 },
	{ 0x33 , 0x0018 },
	{ 0x20 , 0x017f }, // 17f -> 179 (6db -> 0 db) -> 17f
	{ 0x21 , 0x017f }, // 17f -> 179 (6db -> 0 db) -> 17f
	{ 0x2d , 0x0001 },
	{ 0x2e , 0x0001 },
	{ 0x28 , 0x0010 },
	{ 0x29 , 0x0030 },
	{ 0x0f , 0x01c0 },
	{ 0x10 , 0x01c0 },
	{ 0x18 , 0x010b }, // 0x0106 -> 0x010b
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ AMP_REGISTER_DELAY , 50 },
	{ 0x04 , 0x4010 },
	{ 0x01 , 0x0803 }, // 0x0813 -> 0x0803
	{ 0x03 , 0x00f3 },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x05 , 0x4000 },
	{ 0x0a , 0x2000 }, // 0x0000 -> 0x2000
	{ 0x02 , 0x6342 },
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY wm8993_register_type amp_voip_headset_earmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x1c , 0x0179 },
	{ 0x1d , 0x0179 },
	{ 0x1a , 0x010b },
	{ 0x60 , 0x0000 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ 0x20 , 0x0179 },
	{ 0x21 , 0x0179 },
	{ AMP_REGISTER_DELAY , 33 },
	{ 0x04 , 0xc010 },
	{ 0x44 , 0x0003 },
	{ 0x56 , 0x0003 },
	{ 0x44 , 0x0000 },
	{ 0x55 , 0x03e0 },
	{ 0x01 , 0x0323 },
	{ 0x60 , 0x0022 },
	{ 0x4c , 0x9f25 },
	{ AMP_REGISTER_DELAY , 5 },
	{ 0x03 , 0x00f3 },
	{ 0x28 , 0x0001 },
	{ 0x2a , 0x0030 },
	{ 0x2d , 0x0100 },
	{ 0x2e , 0x0100 },
	{ 0x10 , 0x01c0 },
	{ 0x54 , 0x0303 },
	{ AMP_REGISTER_DELAY , 160 },
	{ 0x57 , 0x00fd },
	{ 0x54 , 0x000f },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x60 , 0x00ee },
	{ 0x02 , 0x6111 },
	{ 0x0a , 0x0130 },
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY wm8993_register_type amp_voip_headset_mainmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x1c , 0x0179 },
	{ 0x1d , 0x0179 },
	{ 0x18 , 0x010b },
	{ 0x60 , 0x0000 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ 0x20 , 0x0179 },
	{ 0x21 , 0x0179 },
	{ AMP_REGISTER_DELAY , 33 },
	{ 0x07 , 0x0000 },
	{ 0x04 , 0x4010 },
	{ AMP_REGISTER_DELAY , 10 },
	{ 0x44 , 0x0003 },
	{ 0x56 , 0x0003 },
	{ 0x44 , 0x0000 },
	{ 0x55 , 0x03e0 },
	{ 0x01 , 0x0313 },
	{ 0x60 , 0x0022 },
	{ 0x4c , 0x9f25 },
	{ AMP_REGISTER_DELAY , 5 },
	{ 0x03 , 0x00f3 },
	{ 0x28 , 0x0010 },
	{ 0x29 , 0x0030 },
	{ 0x2d , 0x0100 },
	{ 0x2e , 0x0100 },
	{ 0x0f , 0x01c0 },
	{ 0x54 , 0x0303 },
	{ AMP_REGISTER_DELAY , 160 },
	{ 0x57 , 0x00fd },
	{ 0x54 , 0x000f },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x60 , 0x00ee },
	{ 0x02 , 0x6242 },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END	 ,	0 },
};

REG_MEMORY wm8993_register_type amp_voip_speaker_mainmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x22 , 0x0000 },
	{ 0x23 , 0x0000 },
	{ 0x24 , 0x0018 },
	{ 0x25 , 0x0138 },
	{ 0x26 , 0x0174 },
	{ 0x27 , 0x0174 },
	{ 0x36 , 0x0003 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ 0x0f , 0x01c0 },
	{ 0x20 , 0x0179 },
	{ 0x21 , 0x0179 },
	{ 0x7b , 0xc718 },
	{ 0x7c , 0x1124 },
	{ 0x7d , 0x2c80 },
	{ 0x7e , 0x4600 },
	{ AMP_REGISTER_DELAY , 50 },
	{ 0x07 , 0x0000 },
	{ 0x04 , 0x4010 },
	{ 0x01 , 0x3003 },
	{ 0x03 , 0x0333 },
	{ 0x01 , 0x3013 },
	{ 0x18 , 0x010b },
	{ 0x28 , 0x0010 },
	{ 0x29 , 0x0030 },
	{ 0x62 , 0x0001 },
	{ 0x63 , 0x0001 },
	{ 0x64 , 0x000b },
	{ 0x65 , 0x000f },
	{ 0x66 , 0x000d },
	{ 0x6f , 0x1b18 },
	{ 0x70 , 0xf48a },
	{ 0x71 , 0x040a },
	{ 0x72 , 0x11fa },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x05 , 0x4000 },
	{ 0x02 , 0x6242 },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END	 ,	0 },
};

// VIDEO CALL
REG_MEMORY wm8993_register_type amp_video_handset_mainmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x22 , 0x0000 },
	{ 0x23 , 0x0000 },
	{ 0x24 , 0x0018 },
	{ 0x25 , 0x0138 },
	{ 0x26 , 0x0174 },
	{ 0x27 , 0x0174 },
	{ 0x36 , 0x0003 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ 0x0f , 0x01c0 },
	{ 0x20 , 0x0179 },
	{ 0x21 , 0x0179 },
	{ 0x7b , 0xc718 },
	{ 0x7c , 0x1124 },
	{ 0x7d , 0x2c80 },
	{ 0x7e , 0x4600 },
	{ AMP_REGISTER_DELAY , 50 },
	{ 0x07 , 0x0000 },
	{ 0x04 , 0x4010 },
	{ 0x01 , 0x3003 },
	{ 0x03 , 0x0333 },
	{ 0x01 , 0x3013 },
	{ 0x18 , 0x010b },
	{ 0x28 , 0x0010 },
	{ 0x29 , 0x0030 },
	{ 0x62 , 0x0001 },
	{ 0x63 , 0x0001 },
	{ 0x64 , 0x000b },
	{ 0x65 , 0x000f },
	{ 0x66 , 0x000d },
	{ 0x6f , 0x1b18 },
	{ 0x70 , 0xf48a },
	{ 0x71 , 0x040a },
	{ 0x72 , 0x11fa },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x05 , 0x4000 },
	{ 0x02 , 0x6242 },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY wm8993_register_type amp_video_headset_earmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x1c , 0x0179 },
	{ 0x1d , 0x0179 },
	{ 0x1a , 0x010b },
	{ 0x60 , 0x0000 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ 0x20 , 0x0179 },
	{ 0x21 , 0x0179 },
	{ AMP_REGISTER_DELAY , 33 },
	{ 0x04 , 0xc010 },
	{ 0x44 , 0x0003 },
	{ 0x56 , 0x0003 },
	{ 0x44 , 0x0000 },
	{ 0x55 , 0x03e0 },
	{ 0x01 , 0x0323 },
	{ 0x60 , 0x0022 },
	{ 0x4c , 0x9f25 },
	{ AMP_REGISTER_DELAY , 5 },
	{ 0x03 , 0x00f3 },
	{ 0x28 , 0x0001 },
	{ 0x2a , 0x0030 },
	{ 0x2d , 0x0100 },
	{ 0x2e , 0x0100 },
	{ 0x10 , 0x01c0 },
	{ 0x54 , 0x0303 },
	{ AMP_REGISTER_DELAY , 160 },
	{ 0x57 , 0x00fd },
	{ 0x54 , 0x000f },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x62 , 0x0001 },
	{ 0x63 , 0x000c },
	{ 0x64 , 0x000c },
	{ 0x65 , 0x000c },
	{ 0x66 , 0x000c },
	{ 0x67 , 0x000c },
	{ 0x68 , 0x0fca },
	{ 0x69 , 0x0400 },
	{ 0x6a , 0x00d8 },
	{ 0x6b , 0x1eb5 },
	{ 0x6c , 0xf145 },
	{ 0x6d , 0x0b75 },
	{ 0x6e , 0x01c5 },
	{ 0x6f , 0x1c58 },
	{ 0x70 , 0xf373 },
	{ 0x71 , 0x0a54 },
	{ 0x72 , 0x0558 },
	{ 0x73 , 0x168e },
	{ 0x74 , 0xf829 },
	{ 0x75 , 0x07ad },
	{ 0x76 , 0x1103 },
	{ 0x77 , 0x0564 },
	{ 0x78 , 0x0559 },
	{ 0x79 , 0x4000 },
	{ 0x60 , 0x00ee },
	{ 0x02 , 0x6111 },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY wm8993_register_type amp_video_headset_mainmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x1c , 0x0179 },
	{ 0x1d , 0x0179 },
	{ 0x18 , 0x010b },
	{ 0x60 , 0x0000 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ 0x20 , 0x0179 },
	{ 0x21 , 0x0179 },
	{ AMP_REGISTER_DELAY , 33 },
	{ 0x07 , 0x0000 },
	{ 0x04 , 0x4010 },
	{ AMP_REGISTER_DELAY , 10 },
	{ 0x44 , 0x0003 },
	{ 0x56 , 0x0003 },
	{ 0x44 , 0x0000 },
	{ 0x55 , 0x03e0 },
	{ 0x01 , 0x0313 },
	{ 0x60 , 0x0022 },
	{ 0x4c , 0x9f25 },
	{ AMP_REGISTER_DELAY , 5 },
	{ 0x03 , 0x00f3 },
	{ 0x28 , 0x0010 },
	{ 0x29 , 0x0030 },
	{ 0x2d , 0x0100 },
	{ 0x2e , 0x0100 },
	{ 0x0f , 0x01c0 },
	{ 0x54 , 0x0303 },
	{ AMP_REGISTER_DELAY , 160 },
	{ 0x57 , 0x00fd },
	{ 0x54 , 0x000f },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x62 , 0x0001 },
	{ 0x63 , 0x000c },
	{ 0x64 , 0x000c },
	{ 0x65 , 0x000c },
	{ 0x66 , 0x000c },
	{ 0x67 , 0x000c },
	{ 0x68 , 0x0fca },
	{ 0x69 , 0x0400 },
	{ 0x6a , 0x00d8 },
	{ 0x6b , 0x1eb5 },
	{ 0x6c , 0xf145 },
	{ 0x6d , 0x0b75 },
	{ 0x6e , 0x01c5 },
	{ 0x6f , 0x1c58 },
	{ 0x70 , 0xf373 },
	{ 0x71 , 0x0a54 },
	{ 0x72 , 0x0558 },
	{ 0x73 , 0x168e },
	{ 0x74 , 0xf829 },
	{ 0x75 , 0x07ad },
	{ 0x76 , 0x1103 },
	{ 0x77 , 0x0564 },
	{ 0x78 , 0x0559 },
	{ 0x79 , 0x4000 },
	{ 0x60 , 0x00ee },
	{ 0x02 , 0x6242 },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END	 ,	0 },
};

REG_MEMORY wm8993_register_type amp_video_speaker_mainmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x22 , 0x0000 },
	{ 0x23 , 0x0000 },
	{ 0x24 , 0x0018 },
	{ 0x25 , 0x0138 },
	{ 0x26 , 0x0174 },
	{ 0x27 , 0x0174 },
	{ 0x36 , 0x0003 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ 0x0f , 0x01c0 },
	{ 0x20 , 0x0179 },
	{ 0x21 , 0x0179 },
	{ 0x7b , 0xc718 },
	{ 0x7c , 0x1124 },
	{ 0x7d , 0x2c80 },
	{ 0x7e , 0x4600 },
	{ AMP_REGISTER_DELAY , 50 },
	{ 0x07 , 0x0000 },
	{ 0x04 , 0x4010 },
	{ 0x01 , 0x3003 },
	{ 0x03 , 0x0333 },
	{ 0x01 , 0x3013 },
	{ 0x18 , 0x010b },
	{ 0x28 , 0x0010 },
	{ 0x29 , 0x0030 },
	{ 0x62 , 0x0001 },
	{ 0x63 , 0x0001 },
	{ 0x64 , 0x000b },
	{ 0x65 , 0x000f },
	{ 0x66 , 0x000d },
	{ 0x6f , 0x1b18 },
	{ 0x70 , 0xf48a },
	{ 0x71 , 0x040a },
	{ 0x72 , 0x11fa },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x05 , 0x4000 },
	{ 0x02 , 0x6242 },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END	 ,	0 },
};

// MEDIA RX+TX
REG_MEMORY wm8993_register_type amp_handset_mainmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x07 , 0x0000 },
	{ 0x1f , 0x0000 },
	{ 0x33 , 0x0018 },
	{ 0x20 , 0x017f },
	{ 0x21 , 0x017f },
	{ 0x2d , 0x0001 },
	{ 0x2e , 0x0001 },
	{ 0x28 , 0x0010 },
	{ 0x29 , 0x0030 },
	{ 0x0f , 0x01c0 },
	{ 0x10 , 0x01c0 },
	{ 0x18 , 0x0106 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ AMP_REGISTER_DELAY , 50 },
	{ 0x04 , 0x4010 },
	{ 0x01 , 0x0813 },
	{ 0x03 , 0x00f3 },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x05 , 0x4000 },
	{ 0x0a , 0x0130 },
	{ 0x02 , 0x6342 },
	{ AMP_REGISTER_END	 ,	0 },
};

REG_MEMORY wm8993_register_type amp_headset_earmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x1c , 0x0179 },
	{ 0x1d , 0x0179 },
	{ 0x1a , 0x010b },
	{ 0x60 , 0x0000 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ 0x20 , 0x0179 },
	{ 0x21 , 0x0179 },
	{ AMP_REGISTER_DELAY , 33 },
	{ 0x04 , 0xc010 },
	{ 0x44 , 0x0003 },
	{ 0x56 , 0x0003 },
	{ 0x44 , 0x0000 },
	{ 0x55 , 0x03e0 },
	{ 0x01 , 0x0323 },
	{ 0x60 , 0x0022 },
	{ 0x4c , 0x9f25 },
	{ AMP_REGISTER_DELAY , 5 },
	{ 0x03 , 0x00f3 },
	{ 0x28 , 0x0001 },
	{ 0x2a , 0x0030 },
	{ 0x2d , 0x0100 },
	{ 0x2e , 0x0100 },
	{ 0x10 , 0x01c0 },
	{ 0x54 , 0x0303 },
	{ AMP_REGISTER_DELAY , 160 },
	{ 0x57 , 0x00fd },
	{ 0x54 , 0x000f },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x62 , 0x0001 },
	{ 0x63 , 0x000c },
	{ 0x64 , 0x000c },
	{ 0x65 , 0x000c },
	{ 0x66 , 0x000c },
	{ 0x67 , 0x000c },
	{ 0x68 , 0x0fca },
	{ 0x69 , 0x0400 },
	{ 0x6a , 0x00d8 },
	{ 0x6b , 0x1eb5 },
	{ 0x6c , 0xf145 },
	{ 0x6d , 0x0b75 },
	{ 0x6e , 0x01c5 },
	{ 0x6f , 0x1c58 },
	{ 0x70 , 0xf373 },
	{ 0x71 , 0x0a54 },
	{ 0x72 , 0x0558 },
	{ 0x73 , 0x168e },
	{ 0x74 , 0xf829 },
	{ 0x75 , 0x07ad },
	{ 0x76 , 0x1103 },
	{ 0x77 , 0x0564 },
	{ 0x78 , 0x0559 },
	{ 0x79 , 0x4000 },
	{ 0x60 , 0x00ee },
	{ 0x02 , 0x6111 },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY wm8993_register_type amp_headset_mainmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x1c , 0x0179 },
	{ 0x1d , 0x0179 },
	{ 0x18 , 0x010b },
	{ 0x60 , 0x0000 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ 0x20 , 0x0179 },
	{ 0x21 , 0x0179 },
	{ AMP_REGISTER_DELAY , 33 },
	{ 0x07 , 0x0000 },
	{ 0x04 , 0x4010 },
	{ AMP_REGISTER_DELAY , 10 },
	{ 0x44 , 0x0003 },
	{ 0x56 , 0x0003 },
	{ 0x44 , 0x0000 },
	{ 0x55 , 0x03e0 },
	{ 0x01 , 0x0313 },
	{ 0x60 , 0x0022 },
	{ 0x4c , 0x9f25 },
	{ AMP_REGISTER_DELAY , 5 },
	{ 0x03 , 0x00f3 },
	{ 0x28 , 0x0010 },
	{ 0x29 , 0x0030 },
	{ 0x2d , 0x0100 },
	{ 0x2e , 0x0100 },
	{ 0x0f , 0x01c0 },
	{ 0x54 , 0x0303 },
	{ AMP_REGISTER_DELAY , 160 },
	{ 0x57 , 0x00fd },
	{ 0x54 , 0x000f },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x62 , 0x0001 },
	{ 0x63 , 0x000c },
	{ 0x64 , 0x000c },
	{ 0x65 , 0x000c },
	{ 0x66 , 0x000c },
	{ 0x67 , 0x000c },
	{ 0x68 , 0x0fca },
	{ 0x69 , 0x0400 },
	{ 0x6a , 0x00d8 },
	{ 0x6b , 0x1eb5 },
	{ 0x6c , 0xf145 },
	{ 0x6d , 0x0b75 },
	{ 0x6e , 0x01c5 },
	{ 0x6f , 0x1c58 },
	{ 0x70 , 0xf373 },
	{ 0x71 , 0x0a54 },
	{ 0x72 , 0x0558 },
	{ 0x73 , 0x168e },
	{ 0x74 , 0xf829 },
	{ 0x75 , 0x07ad },
	{ 0x76 , 0x1103 },
	{ 0x77 , 0x0564 },
	{ 0x78 , 0x0559 },
	{ 0x79 , 0x4000 },
	{ 0x60 , 0x00ee },
	{ 0x02 , 0x6242 },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END	 ,	0 },
};

REG_MEMORY wm8993_register_type amp_speaker_mainmic_path[REG_COUNT] = {
#ifdef CONFIG_KTTECH_I2S_MSM_SLAVE // 20110210 by ssgun - slave mode
	{ 0x08 , 0x8000 },
#endif /*CONFIG_KTTECH_I2S_MSM_SLAVE*/
	{ 0x22 , 0x0000 },
	{ 0x23 , 0x0000 },
	{ 0x24 , 0x0018 },
	{ 0x25 , 0x0138 },
	{ 0x26 , 0x0174 },
	{ 0x27 , 0x0174 },
	{ 0x36 , 0x0003 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x0003 },
	{ 0x0f , 0x01c0 },
	{ 0x20 , 0x0179 },
	{ 0x21 , 0x0179 },
	{ 0x7b , 0xc718 },
	{ 0x7c , 0x1124 },
	{ 0x7d , 0x2c80 },
	{ 0x7e , 0x4600 },
	{ AMP_REGISTER_DELAY , 50 },
	{ 0x07 , 0x0000 },
	{ 0x04 , 0x4010 },
	{ 0x01 , 0x3003 },
	{ 0x03 , 0x0333 },
	{ 0x01 , 0x3013 },
	{ 0x18 , 0x010b },
	{ 0x28 , 0x0010 },
	{ 0x29 , 0x0030 },
	{ 0x62 , 0x0001 },
	{ 0x63 , 0x0001 },
	{ 0x64 , 0x000b },
	{ 0x65 , 0x000f },
	{ 0x66 , 0x000d },
	{ 0x6f , 0x1b18 },
	{ 0x70 , 0xf48a },
	{ 0x71 , 0x040a },
	{ 0x72 , 0x11fa },
	{ AMP_REGISTER_DELAY , 15 },
	{ 0x05 , 0x4000 },
	{ 0x02 , 0x6242 },
	{ 0x0a , 0x0000 },
	{ AMP_REGISTER_END	 ,	0 },
};

REG_MEMORY wm8993_register_type amp_handset_earmic_path[REG_COUNT] = {
	{ 0x00 , 0x8993 },
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY wm8993_register_type amp_speaker_earmic_path[REG_COUNT] = {
	{ 0x00 , 0x8993 },
	{ AMP_REGISTER_END   ,  0 },
};

static const wm8993_register_type *amp_sequence_path[AMP_PATH_MAX] [AMP_CAL_MAX]= {
	// AMP_PATH_NONE
	{
		amp_none_path, // AMP_CAL_NORMAL
		amp_none_path, // AMP_CAL_VOICECALL
		amp_none_path, // AMP_CAL_VIDEOCALL
		amp_none_path, // AMP_CAL_VOIPCALL
		amp_none_path  // AMP_CAL_LOOPBACK
	},

	// AMP_PATH_HANDSET
	{
		amp_handset_path,
		amp_call_handset_mainmic_path,
		amp_video_handset_mainmic_path,
		amp_voip_handset_mainmic_path,
		amp_none_path
	},

	// AMP_PATH_HEADSET
	{
		amp_headset_path,
		amp_call_headset_earmic_path,
		amp_video_headset_earmic_path,
		amp_voip_headset_earmic_path,
		amp_headset_earmic_loop_path
	},

	// AMP_PATH_SPEAKER
	{
		amp_speaker_path,
		amp_call_speaker_mainmic_path,
		amp_video_speaker_mainmic_path,
		amp_voip_speaker_mainmic_path,
		amp_speaker_mainmic_loop_path
	},

	// AMP_PATH_HEADSET_SPEAKER
	{
		amp_headset_speaker_path,
		amp_headset_speaker_path,
		amp_headset_speaker_path,
		amp_headset_speaker_path,
		amp_none_path
	},

	// AMP_PATH_MAINMIC
	{
		amp_mainmic_path,
		amp_handset_mainmic_path,
		amp_headset_mainmic_path,
		amp_speaker_mainmic_path,
		amp_speaker_mainmic_loop_path
	},

	// AMP_PATH_EARMIC
	{
		amp_earmic_path,
		amp_handset_earmic_path,
		amp_headset_earmic_path,
		amp_speaker_earmic_path,
		amp_headset_earmic_loop_path
	},

	// AMP_PATH_HEADSET_NOMIC
	{
		amp_headset_path,
		amp_call_headset_mainmic_path,
		amp_video_headset_mainmic_path,
		amp_voip_headset_mainmic_path,
		amp_headset_mainmic_path
	},
};

#ifdef CONFIG_KTTECH_SOUND_TUNE
static void wm8993_apply_register (void *data, size_t size)
{
#ifdef WM8993_DEGUB_MSG
	int i = 0;
	AMP_PATH_TYPE_E path = (AMP_PATH_TYPE_E)pFirstData->reg;
	AMP_CAL_TYPE_E cal   = (AMP_PATH_TYPE_E)pFirstData->value;
	int nCMDCount = 0;
#endif
	wm8993_register_type *pFirstData = (wm8993_register_type*)data;
	wm8993_register_type *pCurData = (wm8993_register_type*)data;
	wm8993_register_type *amp_regs;

#ifdef WM8993_DEGUB_MSG
    nCMDCount = size / sizeof(wm8993_register_type);
    APM_INFO("CODEC_TUNING PATH = %d, CAL = %d COUNT =%d \n", path, cal, nCMDCount);
	for ( i = 0 ; i < nCMDCount ; i ++ )
	{
		APM_INFO("CMD = [0X%.2x] , [0X%.4x] \n" , pCurData->reg , pCurData->value);
		pCurData = pCurData + 1;
	}
#endif
	pCurData = pFirstData + 1;
	amp_regs = pCurData;

	wm8993_set_register((wm8993_register_type *)amp_regs);
}

void wm8993_tuning(void *data, size_t size)
{
	if (data == NULL || size == 0 || size > (sizeof(wm8993_register_type) * AMP_REGISTER_MAX))
	{
		APM_INFO("invalid prarameters data = %d, size = %d \n", (int)data, size);
		return;
	}

	wm8993_apply_register(data, size);
}
EXPORT_SYMBOL(wm8993_tuning);
#endif /*CONFIG_KTTECH_SOUND_TUNE*/

/**
 * wm8993_set_caltype - Set calibration kind in WM8993
 * @param cal: kind of cal type
 *
 * @returns void
 */
void wm8993_set_caltype(AMP_CAL_TYPE_E cal)
{
	if (cal < AMP_CAL_NORMAL || cal >= AMP_CAL_MAX)
	{
		APM_INFO("not support calibration type on wm8993 cal = %d\n", cal);
		return;
	}

	m_cal_type = cal;
	APM_INFO("Set calibration type on wm8993 cal = %d\n", m_cal_type);
}
EXPORT_SYMBOL(wm8993_set_caltype);

/**
 * wm8993_get_caltype - get calibration kind in WM8993
 *
 * @returns AMP_CAL_TYPE_E
 */
AMP_CAL_TYPE_E wm8993_get_caltype(void)
{
	APM_INFO("Get calibration type on wm8993 cal = %d\n", m_cal_type);
	return m_cal_type;
}
EXPORT_SYMBOL(wm8993_get_caltype);

/**
 * wm8993_set_caltype - Set path kind in WM8993
 * @param cal: kind of cal type
 *
 * @returns void
 */
void wm8993_set_pathtype(AMP_PATH_TYPE_E path)
{
	if (path < AMP_PATH_NONE || path >= AMP_PATH_MAX)
	{
		APM_INFO("not support path type on wm8993 path = %d\n", path);
		return;
	}

	m_curr_path = path;
	APM_INFO("Set path type on wm8993 path = %d\n", m_cal_type);
}
EXPORT_SYMBOL(wm8993_set_pathtype);

/**
 * wm8993_get_pathtype - get path kind in WM8993
 *
 * @returns AMP_PATH_TYPE_E
 */
AMP_PATH_TYPE_E wm8993_get_pathtype(void)
{
	APM_INFO("Get path type on wm8993 path = %d\n", m_curr_path);
	return m_curr_path;
}
EXPORT_SYMBOL(wm8993_get_pathtype);

void wm8993_set_register(wm8993_register_type *amp_regs)
{
	uint32_t loop = 0;

#ifdef WM8993_CHECK_RESET // 새로운 레지스터를 설정한다.
	if(isreset) isreset = 0;
#endif /*WM8993_CHECK_RESET*/

	while (amp_regs[loop].reg != AMP_REGISTER_END)
	{
		if (amp_regs[loop].reg == AMP_REGISTER_DELAY)
		{
			msleep(amp_regs[loop].value);
		}
		else if (amp_regs[loop].reg == WM8993_IDAC_REG) // read R59h & R5Ah and apply -2 code offset to left and right iDAC values
		{
			unsigned short r59 = 0, r5a = 0, r57 = 0;
			wm8993_read(0x59, &r59);
			wm8993_read(0x5a, &r5a);
			r59 -= 2;
			r5a -= 2;
			r57 = (r5a & 0x00FF) | ((r59 & 0x00FF)<<8);
			wm8993_write(amp_regs[loop].reg, r57);
		}
		else
		{
			wm8993_write(amp_regs[loop].reg, amp_regs[loop].value);
#ifdef WM8993_DEGUB_MSG				
			APM_INFO("reg 0x%x , value 0x%x",amp_regs[loop].reg, amp_regs[loop].value);
#endif
		}
		loop++;
	}

	return;
}
EXPORT_SYMBOL(wm8993_set_register);

//#ifdef WM8993_WHITE_NOISE_ZERO
void wm8993_enable_amplifier(void)
{
	u8 addr;
	unsigned short value;

	if(amp_enabled == 1)
	{
		APM_INFO("The device(%d) is already open\n", cur_rx);
		return;
	}

	addr = 0x01;
	if (cur_rx == AMP_PATH_HEADSET || cur_rx == AMP_PATH_HEADSET_NOMIC)
	{
		if(cur_tx == AMP_PATH_MAINMIC)
			value = 0x0317; //0x0303;
		else if(cur_tx == AMP_PATH_EARMIC)
			value = 0x0327; //0x0303;
		else
			value = 0x0307; //0x0303;
	}
	else if (cur_rx == AMP_PATH_SPEAKER)
	{
		if(cur_tx == AMP_PATH_MAINMIC)
			value = 0x3017; //0x3003;
		else if(cur_tx == AMP_PATH_EARMIC)
			value = 0x3027; //0x3003;
		else
			value = 0x3007; //0x3003;
	}
	else if (cur_rx == AMP_PATH_HEADSET_SPEAKER)
	{
		if(cur_tx == AMP_PATH_MAINMIC)
			value = 0x3317; //0x3303;
		else if(cur_tx == AMP_PATH_EARMIC)
			value = 0x3327; //0x3303;
		else
			value = 0x3307; //0x3303;
	}
	else
	{
		APM_INFO("Enable Amplifier - Unsupported Device\n");
		return;
	}
	APM_INFO("Enable Amplifier - Register(0x01,0x%x)\n", value);

	amp_enabled = 1;
	wm8993_write(addr, value);
	return;
}
EXPORT_SYMBOL(wm8993_enable_amplifier);

void wm8993_disable_amplifier(void)
{
	u8 addr;
	unsigned short value;

	if(amp_enabled == 0)
	{
		APM_INFO("The device(%d) is already closed\n", cur_rx);
		return;
	}

	if(cur_rx == AMP_PATH_NONE)
	{
		APM_INFO("Disable Amplifier - Unsupported Device\n");
		return;
	}

	addr = 0x01;
	if(cur_tx == AMP_PATH_MAINMIC)
		value = 0x0013;
	else if(cur_tx == AMP_PATH_EARMIC)
		value = 0x0023;
	else
		value = 0x0003;
	APM_INFO("Disable Amplifier - Register(0x0x,0x0003)\n");

	amp_enabled = 0;
	wm8993_write(addr, value);
	return;
}
EXPORT_SYMBOL(wm8993_disable_amplifier);
//#endif

/**
 * wm8993_enable - Enable path  in WM8993
 * @param path: amp path
 *
 * @returns void
 */
void wm8993_enable(AMP_PATH_TYPE_E path)
{
#ifdef WM8993_SINGLE_PATH // 20110510 by ssgun
	const wm8993_register_type *amp_regs = amp_sequence_path[path][m_cal_type];
	int isvoice = msm_device_get_isvoice();

	APM_INFO("path = new[%d] vs old[%d:%d], cal_type = %d\n", path, m_curr_path, m_prev_path, m_cal_type);

	if(amp_regs == NULL || path <= AMP_PATH_NONE || path >= AMP_PATH_MAX)
	{
		APM_INFO("not support path on wm8993 path = %d\n", path);
		return;
	}

	mutex_lock(&path_lock);
	if(path == m_curr_path)
	{
		mutex_unlock(&path_lock);
		APM_INFO("same previous path on wm8993 path = %d\n", path);
		return;
	}

	// 통화의 경우 Rx, Tx가 거의 동시에 열린다.
	if(isvoice == 1) // AUDDEV_EVT_START_VOICE
	{
		if((m_curr_path != AMP_PATH_NONE) && (m_curr_path != path))
		{
			m_prev_path = m_curr_path;
			m_curr_path = path;
			mutex_unlock(&path_lock);
			APM_INFO("========== RETURN ==========\n");

			return;
		}

		if(m_cal_type == AMP_CAL_NORMAL) // if it is'nt call
		{
			APM_INFO("[CALL] OLD : path = %d, old reg = %p, cal_type = %d\n", m_curr_path, amp_regs, m_cal_type);
			m_cal_type = AMP_CAL_VOICECALL;
			amp_regs = amp_sequence_path[path][m_cal_type];
			APM_INFO("[CALL] NEW : path = %d, new reg = %p, cal_type = %d\n", path, amp_regs, m_cal_type);
		}
	}
	// 미디어의 경우 RX, TX가 각각 열리거나 순차적으로 열린다.
	else // MEDIA
	{
		bool bTxRxOpen = false;
		unsigned short newpath = 0;
		unsigned short newcaltype = 0;

		if(m_curr_path != AMP_PATH_NONE && m_curr_path != path)
		{
			if(m_curr_path == AMP_PATH_HANDSET && path == AMP_PATH_MAINMIC)
			{
				bTxRxOpen = true;
				newcaltype = AMP_CAL_VOICECALL;
				newpath = path;
			}
			else if(m_curr_path == AMP_PATH_HEADSET && path == AMP_PATH_MAINMIC)
			{
				bTxRxOpen = true;
				newcaltype = AMP_CAL_VIDEOCALL;
				newpath = path;
			}
			else if(m_curr_path == AMP_PATH_SPEAKER && path == AMP_PATH_MAINMIC)
			{
				bTxRxOpen = true;
				newcaltype = AMP_CAL_VOIPCALL;
				newpath = path;
			}
			else if(m_curr_path == AMP_PATH_HEADSET && path == AMP_PATH_EARMIC)
			{
				bTxRxOpen = true;
				newcaltype = AMP_CAL_VIDEOCALL;
				newpath = path;
			}
			else if(m_curr_path == AMP_PATH_MAINMIC && path == AMP_PATH_HANDSET)
			{
				bTxRxOpen = true;
				newcaltype = AMP_CAL_VOICECALL;
				newpath = AMP_PATH_MAINMIC;
			}
			else if(m_curr_path == AMP_PATH_MAINMIC && path == AMP_PATH_HEADSET)
			{
				bTxRxOpen = true;
				newcaltype = AMP_CAL_VIDEOCALL;
				newpath = AMP_PATH_MAINMIC;
			}
			else if(m_curr_path == AMP_PATH_EARMIC && path == AMP_PATH_HEADSET)
			{
				bTxRxOpen = true;
				newcaltype = AMP_CAL_VIDEOCALL;
				newpath = AMP_PATH_EARMIC;
			}
			else if(m_curr_path == AMP_PATH_MAINMIC && path == AMP_PATH_SPEAKER)
			{
				bTxRxOpen = true;
				newcaltype = AMP_CAL_VOIPCALL;
				newpath = AMP_PATH_MAINMIC;
			}
			else
			{
			}

			if(bTxRxOpen)
			{
				wm8993_reset(m_curr_path);

				APM_INFO("[MEDIA] OLD : path = %d, cal_type = %d => reg = %p\n", path, m_cal_type, amp_regs);
				if(m_cal_type == AMP_CAL_LOOPBACK) newcaltype = AMP_CAL_LOOPBACK;
				amp_regs = amp_sequence_path[newpath][newcaltype];
				APM_INFO("[MEDIA] NEW : path = %d, cal_type = %d => reg = %p\n", newpath, newcaltype, amp_regs);
			}
		}
	}

	m_prev_path = m_curr_path;
	m_curr_path = path;

	mutex_unlock(&path_lock);
#else
	int tx = 0, rx = 0;
	int new_cal = 0, new_path = 0;
	int new_rx = 0, new_tx = 0;
	int isvoice = 0;
	bool reset = false;
	const wm8993_register_type *amp_regs;

	if(path <= AMP_PATH_NONE || path >= AMP_PATH_MAX)
	{
		APM_INFO("not support path on wm8993 path = %d\n", path);
		return;
	}

	if(path == AMP_PATH_MAINMIC || path == AMP_PATH_EARMIC)
	{
		new_tx = path;
	}
	else
	{
		new_rx = path;
//#ifdef WM8993_WHITE_NOISE_ZERO
		amp_enabled = 1;
//#endif
	}

	isvoice = msm_device_get_isvoice();

	APM_INFO("Input Path [%d], CAL[%d] - NEW R:Tx[%d:%d] vs Cur R:Tx[%d:%d]\n", path, new_rx, new_tx, cur_rx, cur_tx, m_cal_type);

	mutex_lock(&path_lock);

#ifdef WM8993_CODEC_FAST_CALL_REGISTER
	// 통화 중이면 Rx open시 Tx Register도 같이 적용한다.
	// 이후 Rx 변경 시도 체크하여 R/Tx Register 동시 적용한다.
	if(isvoice == 1)
	{
		bool bSetReg = false;
		bool bReset = false;

		if(new_tx) // 통화 시 Tx Device만 변경되거나 적용될 수 없다.
		{
			cur_tx = m_curr_path = new_path = new_tx;
		}
		if(new_rx) // 통화 시 Rx Device가 먼저 open을 시도한다.
		{
			bSetReg = true;
			if(new_rx != cur_rx) bReset = true;
			else if(new_rx == cur_rx) bSetReg = false;
			cur_rx = m_curr_path = new_path = new_rx;
		}

		// Exception Case: HEADSET_NOMIC
		if(cur_tx == AMP_PATH_MAINMIC && cur_rx == AMP_PATH_HEADSET)
		{
			new_path = AMP_PATH_HEADSET_NOMIC;
			bSetReg = true;
			bReset = true;
		}

		if(bSetReg)
		{
			if(m_cal_type == AMP_CAL_NORMAL) m_cal_type = AMP_CAL_VOICECALL;
			new_cal = m_cal_type;

			if(bReset) wm8993_reset(m_curr_path);

			amp_regs = amp_sequence_path[new_path][new_cal];
			if(amp_regs == NULL)
			{
				APM_INFO("WM8993 Path:Cal [%d:%d] is empty value\n", new_path, new_cal);
				mutex_unlock(&path_lock);
				return;
			}

			mutex_unlock(&path_lock);
			goto set_register;
		}
		else
		{
			APM_INFO("WM8993 Path:Cal [%d:%d] Call State\n", new_path, new_cal);
			mutex_unlock(&path_lock);
			return;
		}
	}
#endif /*WM8993_CODEC_FAST_CALL_REGISTER*/

	if(new_rx != 0 && cur_rx != 0)
	{
		cur_rx = rx = new_rx;
		reset = true;
	}
	else if(new_rx)
	{
		cur_rx = rx = new_rx;
	}
	else if(cur_rx)
	{
		rx = cur_rx;
	}

	if(new_tx != 0 && cur_tx != 0)
	{
		cur_tx = tx = new_tx;
		reset = true;
	}
	else if(new_tx)
	{
		cur_tx = tx = new_tx;
	}
	else if(cur_tx)
	{
		tx = cur_tx;
	}

	if(isvoice == 1)
	{
		if(m_cal_type == AMP_CAL_NORMAL) m_cal_type = AMP_CAL_VOICECALL;
	}
	else
	{
		// voice, video call은 HAL Layer에서 설정되는 값으로 변경하지 않는다.
		if(m_cal_type == AMP_CAL_VOICECALL) m_cal_type = AMP_CAL_NORMAL;
	}
	mutex_unlock(&path_lock);

	if(rx != 0 && tx != 0)
	{
		reset = true;
		if(isvoice == 1)
		{
 			APM_INFO("WM8993 Call State - New R:Tx [%d:%d] Cur R:Tx[%d:%d]\n", new_rx, new_tx, rx, tx);

			// 20110707 by ssgun - 통화 처리 간소화
			if(rx == AMP_PATH_HANDSET)
			{
				new_cal = m_cal_type;
				new_path = rx;
			}
			else if(rx == AMP_PATH_HEADSET)
			{
				new_cal = m_cal_type;
				if(tx == AMP_PATH_MAINMIC)
					new_path = AMP_PATH_HEADSET_NOMIC;
				else
					new_path = rx;
			}
			else if(rx == AMP_PATH_SPEAKER)
			{
				new_cal = m_cal_type;
				new_path = rx;
			}
			else if(rx == AMP_PATH_HEADSET_SPEAKER)
			{
				new_cal = AMP_CAL_NORMAL; // Exception Case
				new_path = rx;
			}
			else if(rx == AMP_PATH_HEADSET_NOMIC)
			{
				new_cal = m_cal_type;
				if(tx == AMP_PATH_MAINMIC)
					new_path = rx;
				else // Exception Case
					new_path = AMP_PATH_HEADSET;
			}
			else
			{
				APM_INFO("WM8993 Unknown Path - New R:Tx [%d:%d] Cal[%d]\n", new_rx, new_tx, m_cal_type);
				return;
			}
		}
		else
		{
			APM_INFO("WM8993 Media State - New R:Tx [%d:%d] Cur R:Tx[%d:%d]\n", new_rx, new_tx, rx, tx);

			// 20110707 by ssgun - 다음 마이피플 예외 상황 처리
			// 예외 케이스
			// - 아래 예외 케이스를 대비하기 위해 CAL Type을 무시하고 Sound Device를 체크하여 재 설정한다.
			// 1) INCALL_MODE 설정 후 어플 실행 중 해제되는 케이스
			// 2) VOIP 등 CAL TYPE 설정 후 어플 실행 중 해제되는 케이스
			if(rx == AMP_PATH_HANDSET)
			{
				if(m_cal_type == AMP_CAL_VOICECALL)
				{
					new_cal = m_cal_type;
					new_path = rx;
				}
				else
				{
					new_cal = AMP_CAL_VOICECALL;
					new_path = tx;
				}
			}
			else if(rx == AMP_PATH_HEADSET)
			{
				if(m_cal_type == AMP_CAL_VOICECALL)
				{
					new_cal = m_cal_type;
					new_path = rx;
				}
				else
				{
					new_cal = AMP_CAL_VIDEOCALL;
					new_path = tx;
				}
			}
			else if(rx == AMP_PATH_SPEAKER)
			{
				if(m_cal_type == AMP_CAL_VOICECALL)
				{
					new_cal = m_cal_type;
					new_path = rx;
				}
				else
				{
					new_cal = AMP_CAL_VOIPCALL;
					new_path = tx;
				}
			}
			else if(rx == AMP_PATH_HEADSET_SPEAKER)
			{
				if(m_cal_type == AMP_CAL_VOICECALL)
				{
					new_cal = m_cal_type;
					new_path = rx;
				}
				else
				{
					new_cal = AMP_CAL_NORMAL; // Exception Case
					new_path = rx;
				}
			}
			else if(rx == AMP_PATH_HEADSET_NOMIC)
			{
				if(m_cal_type == AMP_CAL_VOICECALL)
				{
					new_cal = m_cal_type;
					new_path = rx;
				}
				else
				{
					new_cal = AMP_CAL_VIDEOCALL;
					new_path = rx;
				}
			}
			else
			{
				APM_INFO("WM8993 Unknown Path - New R:Tx [%d:%d] Cal[%d]\n", new_rx, new_tx, m_cal_type);
				return;
			}
		}
	}
	else if(rx)
	{
		new_path = rx;
		new_cal = m_cal_type; // 20110516 by ssgun
	}
	else if(tx)
	{
		new_path = tx;
		new_cal = m_cal_type; // 20110516 by ssgun
	}
	else
	{
		APM_INFO("WM8993 Unknown Path [%d] Cal[%d]\n", new_path, new_cal);
		return;
	}
	m_curr_path = new_path;

	APM_INFO("WM8993 Set Path - New R:Tx [%d:%d] Current R:Tx [%d:%d]\n", new_path, new_cal, cur_rx, cur_tx);

	if(reset)
	{
		if(path == cur_rx)
			wm8993_reset(cur_tx);
		else if(path == cur_tx)
			wm8993_reset(cur_rx);
		else
			wm8993_reset(AMP_PATH_NONE);
	}

	amp_regs = amp_sequence_path[new_path][new_cal];
	if(amp_regs == NULL)
	{
		APM_INFO("WM8993 Path[%d:%d] is empty value\n", new_path, new_cal);
		return;
	}
#endif /*WM8993_SINGLE_PATH*/

#ifdef WM8993_CODEC_FAST_CALL_REGISTER
set_register:
#endif /*WM8993_CODEC_FAST_CALL_REGISTER*/
	mutex_lock(&path_lock);
	if((path == AMP_PATH_HANDSET) && (isspksw == 0))
	{
		APM_INFO("HANDSET[%d]-SPK_SW_GPIO:HIGH\n", path);
		gpio_set_value(WM8993_SPK_SW_GPIO, 1);
		isspksw = 1;
	}

	wm8993_set_register((wm8993_register_type *)amp_regs);
	mutex_unlock(&path_lock);
	APM_INFO("WM8993 Apply to register is completed - Path [%d:%d] Cal[%d]\n", path, new_path, new_cal);
}
EXPORT_SYMBOL(wm8993_enable);

/**
 * wm8993_disable - Disable path  in WM8993
 * @param path: amp path
 *
 * @returns void
 */
void wm8993_disable(AMP_PATH_TYPE_E path)
{
#ifdef WM8993_SINGLE_PATH // 20110510 by ssgun
	int isvoice = msm_device_get_isvoice();

	APM_INFO("path = new[%d] vs old[%d:%d], cal_type = %d\n", path, m_curr_path, m_prev_path, m_cal_type);

	mutex_lock(&path_lock);

	// Call, Media 모두 Rx, Tx Device가 동시에 열릴 수 있다.
	// 따라서 PATH 값을 Rx, Tx 두 개로 처리해야 한다.
	// 향후 Rx, Tx에 대한 PATH를 구분하여 처리하도록 하자.
	if(path == m_prev_path)
	{
		m_prev_path = AMP_PATH_NONE;
		APM_INFO("disable path = %d vs enable path = %d\n", path, m_curr_path);
		goto noreset;
	}
	else if(path == m_curr_path)
	{
		if(m_prev_path != AMP_PATH_NONE)
		{
			m_curr_path = m_prev_path;
			m_prev_path = AMP_PATH_NONE;
			APM_INFO("disable path = %d vs enable path = %d\n", path, m_curr_path);
			goto noreset;
		}
	}

	m_curr_path = AMP_PATH_NONE;
	m_prev_path = AMP_PATH_NONE;

	wm8993_reset(path);

noreset:
	mutex_unlock(&path_lock);

	if((path == AMP_PATH_HANDSET) && (isspksw == 1))
	{
		APM_INFO("HANDSET[%d]-SPK_SW_GPIO:LOW\n", path);
		gpio_set_value(WM8993_SPK_SW_GPIO, 0);
		isspksw = 0;
	}

	// Media일 경우 이전 레지스터 데이터를 복원해야 한다. (Call은 거의 동시 처리되므로 의미 없다.)
	if((m_curr_path != AMP_PATH_NONE) && (isvoice != 1))
	{
		const wm8993_register_type *amp_regs = NULL;

		APM_INFO("new apply path = %d -> %d, caltype = %d\n", path, m_curr_path, m_cal_type);
		m_cal_type = AMP_CAL_NORMAL;
		amp_regs = amp_sequence_path[m_curr_path][m_cal_type];
		if(amp_regs == NULL)
		{
			APM_INFO("not support path on wm8993 path = %d, cal = %d\n", m_curr_path, m_cal_type);
			return;
		}

		wm8993_reset(path);
		wm8993_set_register((wm8993_register_type *)amp_regs);
	}
#else
	int new_rx = 0, new_tx = 0;
	int new_cal = 0, new_path = 0;
	int isvoice = 0;
	bool reset = true;

	if(path <= AMP_PATH_NONE || path >= AMP_PATH_MAX)
	{
		APM_INFO("not support path on wm8993 path = %d\n", path);
		return;
	}

	if(path == AMP_PATH_MAINMIC || path == AMP_PATH_EARMIC)
	{
		new_tx = path;
	}
	else
	{
		new_rx = path;
//#ifdef WM8993_WHITE_NOISE_ZERO
		amp_enabled = 0;
//#endif
	}

	isvoice = msm_device_get_isvoice();

	APM_INFO("[%d] NEW R:Tx[%d:%d] vs CUR R:Tx[%d:%d], CAL[%d]\n", path, new_rx, new_tx, cur_rx, cur_tx, m_cal_type);

	mutex_lock(&path_lock);

	if(new_tx != AMP_PATH_NONE)
	{
		if(cur_tx != new_tx)
		{
			// Exception Case
		}
		m_prev_path = cur_tx;
		cur_tx = AMP_PATH_NONE;

		if(cur_rx != AMP_PATH_NONE)
		{
			new_path = cur_rx;
		}
	}

	if(new_rx != AMP_PATH_NONE)
	{
		if(cur_rx != new_rx)
		{
			// Exception Case
		}
		m_prev_path = cur_rx;
		cur_rx = AMP_PATH_NONE;

		if(cur_tx != AMP_PATH_NONE)
		{
			new_path = cur_tx;
		}
	}
	m_curr_path = new_path;

	if((path == AMP_PATH_HANDSET) && (isspksw == 1))
	{
		APM_INFO("HANDSET[%d]-SPK_SW_GPIO:LOW\n", path);
		gpio_set_value(WM8993_SPK_SW_GPIO, 0);
		isspksw = 0;
	}
 
	if(new_path != AMP_PATH_NONE && (isvoice != 1))
	{
		const wm8993_register_type *amp_regs = NULL;

		m_cal_type = AMP_CAL_NORMAL;
		new_cal = m_cal_type;
		APM_INFO("new apply path = %d -> %d, caltype = %d\n", path, new_path, new_cal);

		amp_regs = amp_sequence_path[new_path][new_cal];
		if(amp_regs == NULL)
		{
			mutex_unlock(&path_lock);
			APM_INFO("not support path on wm8993 path = %d, cal = %d\n", new_path, new_cal);
			return;
		}

		if(reset) wm8993_reset(path);
		wm8993_set_register((wm8993_register_type *)amp_regs);
	}
	else
	{
		APM_INFO("Calling : apply path = %d -> %d, caltype = %d\n", path, new_path, new_cal);
		if(new_path == AMP_PATH_NONE) wm8993_reset(path);
	}

	mutex_unlock(&path_lock);
#endif /*WM8993_SINGLE_PATH*/
}
EXPORT_SYMBOL(wm8993_disable);

void wm8993_reset(AMP_PATH_TYPE_E path)
{
#ifdef WM8993_CHECK_RESET // Reset 중복 방지를 위해 체크한다.
	// 레지스터 설정시 isreset은 0으로 변경한다.
	// 레지스터 리셋시 신규 레지스터를 설정한 경우만 리셋처리한다.
	if(isreset == 0)
	{
		isreset = 1;
		wm8993_write(WM8993_RESET_REG, WM8993_RESET_VALUE);
		APM_INFO("========== WM8993 RESET ==========\n");
	}
#else
	APM_INFO("========== WM8993 RESET ==========\n");

	wm8993_write(WM8993_RESET_REG, WM8993_RESET_VALUE);
#endif /*WM8993_CHECK_RESET*/
}
EXPORT_SYMBOL(wm8993_reset);

static long wm8993_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct wm8993_dev *wm8993 = file->private_data;
	int rc = 0;

	mutex_lock(&wm8993->lock);
	switch (cmd) {
		case AUDIO_SET_CONFIG:
		{
			struct msm_audio_config config;
			pr_info("%s: AUDIO_SET_CONFIG\n", __func__);

			if (copy_from_user(&config, (void *) arg, sizeof(config))) {
				rc = -EFAULT;
				break;
			}

			wm8993->cal_type = config.type;
			APM_INFO("Set calibration type = %d, %d\n", wm8993->cal_type, config.type);
			if(wm8993->cal_type == 0) {
				wm8993_set_caltype(AMP_CAL_NORMAL);
			} else if(wm8993->cal_type == 1) {
				wm8993_set_caltype(AMP_CAL_VOICECALL);
			} else if(wm8993->cal_type == 2) {
				wm8993_set_caltype(AMP_CAL_VIDEOCALL);
			} else if(wm8993->cal_type == 3) {
				wm8993_set_caltype(AMP_CAL_VOIPCALL);
			} else if(wm8993->cal_type == 4) {
				wm8993_set_caltype(AMP_CAL_LOOPBACK);
			} else {
				wm8993_set_caltype(AMP_CAL_NORMAL);
			}
		}
		break;

		case AUDIO_GET_CONFIG:
		{
			struct msm_audio_config config;
			pr_info("%s: AUDIO_GET_CONFIG\n", __func__);

			memset(&config, 0x00, sizeof(struct msm_audio_config));
			if (copy_to_user((void *) arg, &config, sizeof(config)))
				rc = -EFAULT;
		}
		break;

		case AUDIO_SET_EQ:
		{
			struct msm_audio_config config;
			pr_info("%s: AUDIO_SET_CONFIG\n", __func__);

			if (copy_from_user(&config, (void *) arg, sizeof(config))) {
				rc = -EFAULT;
				break;
			}

			wm8993->eq_type = config.type;
			APM_INFO("Set EQ type = %d\n", wm8993->eq_type);
		}
		break;

		case AUDIO_SET_MUTE:
		{
			struct msm_audio_config config;
			pr_info("%s: AUDIO_SET_CONFIG\n", __func__);

			if (copy_from_user(&config, (void *) arg, sizeof(config))) {
				rc = -EFAULT;
				break;
			}

			wm8993->mute_enabled = config.type;
			APM_INFO("Set Mute type = %d\n", wm8993->mute_enabled);
			if(wm8993->mute_enabled == 1)
				msm_set_voice_mute(DIR_RX, 1);
			else
				msm_set_voice_mute(DIR_RX, 0);
		 }
		 break;

		case AUDIO_SET_VOLUME:
		{
			struct msm_audio_config config;
			pr_info("%s: AUDIO_SET_VOLUME\n", __func__);

			if (copy_from_user(&config, (void *) arg, sizeof(config))) {
				rc = -EFAULT;
				break;
			}

			wm8993->voip_micgain = config.channel_count;
			APM_INFO("Set Voip Mic Gain type = %d\n", wm8993->voip_micgain);
			if(wm8993->voip_micgain > 0)
				msm_set_voice_vol(DIR_TX, wm8993->voip_micgain);
		}
		break;

		default:
		rc = -EINVAL;
		break;
	}
	mutex_unlock(&wm8993->lock);

	return rc;
}

static int wm8993_open(struct inode *inode, struct file *file)
{
	struct wm8993_dev *wm8993;

	wm8993 = kzalloc(sizeof(struct wm8993_dev), GFP_KERNEL);
	if (!wm8993)
		return -ENOMEM;

	mutex_init(&wm8993->lock);
	wm8993->cal_type = 0;
	wm8993->eq_type = 0;
	wm8993->mute_enabled = 0;
	wm8993->voip_micgain = 0;
	wm8993->voice_volume = 0;
	file->private_data = wm8993;
	wm8993_modules = wm8993;

	return 0;
}

static int wm8993_release(struct inode *inode, struct file *file)
{
	struct wm8993_dev *wm8993 = file->private_data;

	pr_debug("%s\n", __func__);

	mutex_destroy(&path_lock);

	kfree(wm8993);
	return 0;
}

static struct file_operations wm8993_dev_fops = {
	.owner      = THIS_MODULE,
	.open		= wm8993_open,
	.release	= wm8993_release,
	.unlocked_ioctl = wm8993_ioctl,
};

struct miscdevice wm8993_control_device = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "snddev_wm8993",
	.fops   = &wm8993_dev_fops,
};

static int __init wm8993_ctl_init(void)
{
	pr_debug("%s\n", __func__);

	wm8993_modules = NULL;
	isspksw = 0;
#ifdef WM8993_CHECK_RESET // Reset 중복 방지를 위해 체크한다.
	isreset = 0;
#endif /*WM8993_CHECK_RESET*/
//#ifdef WM8993_WHITE_NOISE_ZERO
	amp_enabled = 0;
//#endif

#ifndef WM8993_SINGLE_PATH // 20110510 by ssgun
	cur_tx = 0;
	cur_rx = 0;
#endif /*WM8993_SINGLE_PATH*/

	mutex_init(&path_lock);

	return misc_register(&wm8993_control_device);
}

device_initcall(wm8993_ctl_init);
