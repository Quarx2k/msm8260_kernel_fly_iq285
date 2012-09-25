/* 
 * snddev_wm8993.c -- WM8993 audio driver
 *
 * Copyright 2011 KTTech
 *
 * Author: Gun Song
 */

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
#include <linux/msm_audio.h>

#include <mach/qdsp6v2/snddev_wm8993.h>
#include <mach/qdsp6v2/audio_dev_ctl.h>

static AMP_PATH_TYPE_E	m_curr_path 	= AMP_PATH_NONE;
static AMP_PATH_TYPE_E	m_prev_path 	= AMP_PATH_NONE;
static AMP_CAL_TYPE_E	m_cal_type 		= AMP_CAL_NORMAL;

struct snddev_wm8993 wm8993_modules;
static bool bMutex_enable;
static u8 isspksw;

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
	{ 0x26 , 0x0171 },
	{ 0x27 , 0x0171 },
	{ 0x36 , 0x0003 },
	{ 0x39 , 0x0068 },
	{ 0x01 , 0x3303 },
	{ AMP_REGISTER_DELAY , 50 },
	{ 0x03 , 0x03f3 },
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
	{ 0x0a , 0x0000 },
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
	{ 0x0a , 0x0000 },
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
	{ 0x0a , 0x0000 },
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
	{ 0x0a , 0x0000 },
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

void wm8993_init(void)
{
	pr_debug("%s\n", __func__);

	bMutex_enable = false;
}
EXPORT_SYMBOL(wm8993_init);

void wm8993_exit(void)
{
	pr_debug("%s\n", __func__);

	bMutex_enable = false;
}
EXPORT_SYMBOL(wm8993_exit);

#ifdef CONFIG_KTTECH_SOUND_TUNE
static void wm8993_apply_register (void *data, size_t size)
{
#ifdef WM8993_DEGUB_MSG
	int i = 0;
#endif
	int nCMDCount = 0;
	wm8993_register_type *pFirstData = (wm8993_register_type*)data;
	wm8993_register_type *pCurData = (wm8993_register_type*)data;

	AMP_PATH_TYPE_E path = (AMP_PATH_TYPE_E)pFirstData->reg;
	AMP_CAL_TYPE_E cal   = (AMP_PATH_TYPE_E)pFirstData->value;
	wm8993_register_type *amp_regs;

	nCMDCount = size / sizeof(wm8993_register_type);
	APM_INFO("CODEC_TUNING PATH = %d, CAL = %d COUNT =%d \n", path, cal, nCMDCount);
#ifdef WM8993_DEGUB_MSG
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
 * wm8993_set
 * @param cal: kind of cal type
 *
 * @returns void
*/
void wm8993_set(int type, int value)
{
	if(type == AMP_TYPE_CAL)
	{
		wm8993_set_caltype(value);
	}
	else
	{
	}
}
EXPORT_SYMBOL(wm8993_set);

/**
 * wm8993_get
 * @param cal: kind of cal type
 *
 * @returns void
*/
void wm8993_get(int type, int * value)
{
	if(type == AMP_TYPE_CAL)
	{
		*value = wm8993_get_caltype();
	}
	else
	{
	}
}
EXPORT_SYMBOL(wm8993_get);

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

void wm8993_set_register(wm8993_register_type *amp_regs)
{
	uint32_t loop = 0;

	while (amp_regs[loop].reg != AMP_REGISTER_END)
	{
		if (amp_regs[loop].reg == AMP_REGISTER_DELAY)
		{
			msleep(amp_regs[loop].value);
		}
		else if (amp_regs[loop].reg == 0x57) // read R59h & R5Ah and apply -2 code offset to left and right iDAC values
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

/**
 * wm8993_enable - Enable path  in WM8993
 * @param path: amp path
 *
 * @returns void
 */
void wm8993_enable(AMP_PATH_TYPE_E path)
{
	const wm8993_register_type *amp_regs = amp_sequence_path[path][m_cal_type];
	struct snddev_wm8993 *wm8993 = &wm8993_modules;

	APM_INFO("path = new[%d] vs old[%d:%d], cal_type = %d\n", path, m_curr_path, m_prev_path, m_cal_type);

	mutex_lock(&wm8993->path_lock);

	if(amp_regs == NULL || path <= AMP_PATH_NONE || path >= AMP_PATH_MAX)
	{
		mutex_unlock(&wm8993->path_lock);
		APM_INFO("not support path on wm8993 path = %d\n", path);
		return;
	}

	if(path == m_curr_path)
	{
		mutex_unlock(&wm8993->path_lock);
		APM_INFO("same previous path on wm8993 path = %d\n", path);
		return;
	}

	// 통화의 경우 Rx, Tx가 거의 동시에 열린다.
	if(msm_device_get_isvoice() == 1) // AUDDEV_EVT_START_VOICE
	{
		if((m_curr_path != AMP_PATH_NONE) && (m_curr_path != path))
		{
			m_prev_path = m_curr_path;
			m_curr_path = path;

			mutex_unlock(&wm8993->path_lock);
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
				wm8993_reset();

				APM_INFO("[MEDIA] OLD : path = %d, cal_type = %d => reg = %p\n", path, m_cal_type, amp_regs);
				if(m_cal_type == AMP_CAL_LOOPBACK) newcaltype = AMP_CAL_LOOPBACK;
				amp_regs = amp_sequence_path[newpath][newcaltype];
				APM_INFO("[MEDIA] NEW : path = %d, cal_type = %d => reg = %p\n", newpath, newcaltype, amp_regs);
			}
		}
	}

	m_prev_path = m_curr_path;
	m_curr_path = path;

	mutex_unlock(&wm8993->path_lock);

	if((path == AMP_PATH_HANDSET) && (isspksw == 0))
	{
		APM_INFO("HANDSET[%d]-SPK_SW_GPIO:HIGH\n", path);
		gpio_set_value(WM8993_SPK_SW_GPIO, 1);
		isspksw = 1;
	}

	wm8993_set_register((wm8993_register_type *)amp_regs);
	APM_INFO("PATH : %d - completed!!!\n", path);
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
	struct snddev_wm8993 *wm8993 = &wm8993_modules;

	APM_INFO("path = new[%d] vs old[%d:%d], cal_type = %d\n", path, m_curr_path, m_prev_path, m_cal_type);

	mutex_lock(&wm8993->path_lock);

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

	mutex_unlock(&wm8993->path_lock);

	wm8993_reset();

noreset:
	if((path == AMP_PATH_HANDSET) && (isspksw == 1))
	{
		APM_INFO("HANDSET[%d]-SPK_SW_GPIO:LOW\n", path);
		gpio_set_value(WM8993_SPK_SW_GPIO, 0);
		isspksw = 0;
	}

	// Media일 경우 이전 레지스터 데이터를 복원해야 한다. (Call은 거의 동시 처리되므로 의미 없다.)
	if((m_curr_path != AMP_PATH_NONE) && (msm_device_get_isvoice() != 1))
	{
		const wm8993_register_type *amp_regs = NULL;

		APM_INFO("new apply path = %d -> %d, caltype = %d\n", path, m_curr_path, m_cal_type);
		m_cal_type = AMP_CAL_NORMAL;
		amp_regs = amp_sequence_path[m_curr_path][m_cal_type];
		if(amp_regs == NULL)
		{
			APM_INFO("Register is Null: new apply path = %d -> %d, caltype = %d\n", path, m_curr_path, m_cal_type);
			return;
		}
		APM_INFO("new apply path = %d -> %d, caltype = %d\n", path, m_curr_path, m_cal_type);

		wm8993_reset();
		wm8993_set_register((wm8993_register_type *)amp_regs);
	}
}
EXPORT_SYMBOL(wm8993_disable);

void wm8993_reset(void)
{
	APM_INFO("========== WM8993 RESET ==========\n");

	wm8993_write(WM8993_RESET_REG, WM8993_RESET_VALUE);
}
EXPORT_SYMBOL(wm8993_reset);

/********************************************************************/
/* WM8993 I2C Driver */
/********************************************************************/

/**
 * wm8993_write - Sets register in WM8993
 * @param wm8993: wm8993 structure pointer passed by client
 * @param reg: register address
 * @param value: buffer values to be written
 * @param num_bytes: n bytes to write
 *
 * @returns result of the operation.
 */
int wm8993_write(u8 reg, unsigned short value)
{
	int rc, retry;
	unsigned char buf[3];
	struct i2c_msg msg[2];
	struct snddev_wm8993 *wm8993;

	wm8993 = &wm8993_modules;

	mutex_lock(&wm8993->xfer_lock);
 
	memset(buf, 0x00, sizeof(buf));

	buf[0] = (reg & 0x00FF);
	buf[1] = (value & 0xFF00)>>8;
	buf[2] = (value & 0x00FF);

	msg[0].addr = wm8993->client->addr;
	msg[0].flags = 0;
	msg[0].len = 3;
	msg[0].buf = buf;

	for (retry = 0; retry <= 2; retry++)
	{
		rc = i2c_transfer(wm8993->client->adapter, msg, 1);
		if(rc > 0)
		{
			break;
		}
		else
		{
			printk("[%d] i2c_write failed, addr = 0x%x, val = 0x%x\n", retry, reg, value);
			msleep(10);
		}
	}

	mutex_unlock(&wm8993->xfer_lock);

	return rc;
} 

/**
 * wm8993_read - Reads registers in WM8993
 * @param wm8993: wm8993 structure pointer passed by client
 * @param reg: register address
 * @param value: i2c read of the register to be stored
 * @param num_bytes: n bytes to read.
 * @param mask: bit mask concerning its register
 *
 * @returns result of the operation.
*/
int wm8993_read(u8 reg, unsigned short *value)
{
	int rc, retry;
	struct i2c_msg msg[2];
	struct snddev_wm8993 *wm8993;

	wm8993 = &wm8993_modules;

	mutex_lock(&wm8993->xfer_lock);

	value[0] = (reg & 0x00FF);

	msg[0].addr = wm8993->client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = (u8*)value;

	msg[1].addr = wm8993->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 2;
	msg[1].buf = (u8*)value;

	for (retry = 0; retry <= 2; retry++)
	{
		rc = i2c_transfer(wm8993->client->adapter, msg, 2);
		if(rc > 0)
		{
			break;
		}
		else
		{
			printk("%s:%d] failed, retry=%d, addr=0x%x, val=0x%x\n", __FUNCTION__, __LINE__, retry, reg, *value);
			msleep(10);
		}
	}

	*value = ((*value) >> 8 ) | (((*value) & 0x00FF) << 8); // switch MSB , LSB

	mutex_unlock(&wm8993->xfer_lock);

	return rc;
}

static long wm8993_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct snddev_wm8993 *wm8993 = file->private_data;
	int rc = 0;

	pr_debug("%s\n", __func__);

	switch (cmd) {
		case AUDIO_SET_CONFIG:
		{
			struct msm_audio_config config;
			pr_debug("%s: AUDIO_SET_CONFIG\n", __func__);

			if (copy_from_user(&config, (void *) arg, sizeof(config))) {
				rc = -EFAULT;
				break;
			}

			mutex_lock(&wm8993->lock);
			wm8993->cal_type = config.type;
			APM_INFO("Set calibration type = %d, %d\n", wm8993->cal_type, config.type);
#if 1 // 20110318 by ssgun - cal type 별로 명확히 설정한다.
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
#else	
			if(wm8993->cal_type > 0 && wm8993->cal_type != wm8993_get_caltype()) {
				wm8993_set_caltype(wm8993->cal_type);
			} else {
				wm8993_set_caltype(AMP_CAL_NORMAL);
			}
#endif
			mutex_unlock(&wm8993->lock);
		}
		break;

		case AUDIO_GET_CONFIG:
		{
			struct msm_audio_config config;
			pr_debug("%s: AUDIO_GET_CONFIG\n", __func__);

			memset(&config, 0x00, sizeof(struct msm_audio_config));
			if (copy_to_user((void *) arg, &config, sizeof(config)))
				rc = -EFAULT;
		}
		break;

		case AUDIO_SET_EQ:
		{
			struct msm_audio_config config;
			pr_debug("%s: AUDIO_SET_CONFIG\n", __func__);

			if (copy_from_user(&config, (void *) arg, sizeof(config))) {
				rc = -EFAULT;
				break;
			}

			mutex_lock(&wm8993->lock);
			wm8993->eq_type = config.type;
			mutex_unlock(&wm8993->lock);
			APM_INFO("Set EQ type = %d\n", wm8993->eq_type);
		}
		break;

		case AUDIO_SET_MUTE:
		{
			struct msm_audio_config config;
			pr_debug("%s: AUDIO_SET_CONFIG\n", __func__);

			if (copy_from_user(&config, (void *) arg, sizeof(config))) {
				rc = -EFAULT;
				break;
			}

			mutex_lock(&wm8993->lock);
			wm8993->mute_enabled = config.type;
			mutex_unlock(&wm8993->lock);
			APM_INFO("Set Mute type = %d\n", wm8993->mute_enabled);
			if(wm8993->mute_enabled == 1)
				msm_set_voice_mute(DIR_TX, 1);
			else
				msm_set_voice_mute(DIR_TX, 0);
		 }
		 break;

		case AUDIO_SET_VOLUME:
		{
			struct msm_audio_config config;
			pr_debug("%s: AUDIO_SET_CONFIG\n", __func__);

			if (copy_from_user(&config, (void *) arg, sizeof(config))) {
				rc = -EFAULT;
				break;
			}

			mutex_lock(&wm8993->lock);
			wm8993->voip_micgain = config.type;
			mutex_unlock(&wm8993->lock);
			APM_INFO("Set Voip Mic Gain type = %d\n", wm8993->voip_micgain);
			if(wm8993->voip_micgain > 0)
				msm_set_voice_vol(DIR_TX, wm8993->voip_micgain);
		}
		break;

		default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int wm8993_open(struct inode *inode, struct file *file)
{
	pr_debug("%s\n", __func__);

	file->private_data = &wm8993_modules;
	return 0;
}

static int wm8993_release(struct inode *inode, struct file *file)
{
	pr_debug("%s\n", __func__);

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

static int wm8993_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct wm8993_platform_data *pdata = client->dev.platform_data;
	struct snddev_wm8993 *wm8993;
	int status;

	pr_debug("%s\n", __func__);

	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C) == 0)
	{
		dev_err(&client->dev, "can't talk I2C?\n");
		return -ENODEV;
	}

	if (pdata->wm8993_setup != NULL)
	{
		status = pdata->wm8993_setup(&client->dev);
		if (status < 0)
		{
			pr_err("wm8993_probe: wm8993  setup power failed\n");
			return status;
		}
	}

	wm8993 = &wm8993_modules;
	wm8993->client = client;
	strlcpy(wm8993->client->name, id->name, sizeof(wm8993->client->name));
	mutex_init(&wm8993->xfer_lock);
	mutex_init(&wm8993->path_lock);
	mutex_init(&wm8993->lock);

	status = misc_register(&wm8993_control_device);
	if (status)
	{
		pr_err("wm8993_probe: wm8993_control_device register failed\n");
		return status;
	}

#if 0 // 20110305 by ssgun - test code
	add_child(0x1A, "wm8993_codec", id->driver_data, NULL, 0);
#endif

	return 0;
}

static int __devexit wm8993_remove(struct i2c_client *client)
{
	struct wm8993_platform_data *pdata;

	pr_debug("%s\n", __func__);

	pdata = client->dev.platform_data;

	i2c_unregister_device(wm8993_modules.client);
	wm8993_modules.client = NULL;

	if (pdata->wm8993_shutdown != NULL)
		pdata->wm8993_shutdown(&client->dev);

	misc_deregister(&wm8993_control_device);

	return 0;
}

static struct i2c_device_id wm8993_id_table[] = {
	{"wm8993", 0x0},
	{}
};
MODULE_DEVICE_TABLE(i2c, wm8993_id_table);

static struct i2c_driver wm8993_driver = {
		.driver			= {
			.owner		=	THIS_MODULE,
			.name		= 	"wm8993",
		},
		.id_table		=	wm8993_id_table,
		.probe			=	wm8993_probe,
		.remove			=	__devexit_p(wm8993_remove),
};

#define SPK_SW_CTRL_0 \
	GPIO_CFG(WM8993_SPK_SW_GPIO, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA)

static int __init wm8993_codec_init(void)
{
	int rc;

	pr_debug("%s\n", __func__);

	rc = gpio_tlmm_config(SPK_SW_CTRL_0, GPIO_CFG_ENABLE);
	if (rc)
	{
		pr_err("%s] gpio  config failed: %d\n", __func__, rc);
		goto fail;
	}

	rc = i2c_add_driver(&wm8993_driver);
	return rc;

fail:
	return -ENODEV;
}
module_init(wm8993_codec_init);

static void __exit wm8993_codec_exit(void)
{
	pr_debug("%s\n", __func__);

	i2c_del_driver(&wm8993_driver);
}
module_exit(wm8993_codec_exit);

MODULE_DESCRIPTION("KTTECH's WM8993 Codec Sound Device driver");
MODULE_VERSION("1.0");
MODULE_AUTHOR("Gun Song <ssgun@kttech.co.kr>");
