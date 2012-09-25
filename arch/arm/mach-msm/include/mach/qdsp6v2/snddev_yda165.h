/* Copyright (c) 2011, KTTech. All rights reserved.
 *
 * YDA165 Sound Amp Driver
 */
#ifndef __MACH_QDSP6V2_SNDDEV_YDA165_H__
#define __MACH_QDSP6V2_SNDDEV_YDA165_H__

#include <linux/mfd/msm-adie-codec.h>
#include <linux/i2c.h>

#include <mach/pmic.h>
#include <mach/qdsp6v2/audio_amp_ctl.h>

// YDA165  Register Control
#define AMP_REGISTER_MAX          50//10
#define AMP_REGISTER_DELAY      0xFE// YDA165 Delay
#define AMP_REGISTER_END        0xFF// YDA165 END 

#ifdef CONFIG_KTTECH_SOUND_TUNE
#define REG_MEMORY   static
#define REG_COUNT    AMP_REGISTER_MAX
#else
#define REG_MEMORY   static const     /* Normally, tables are in ROM  */
#define REG_COUNT
#endif /*CONFIG_KTTECH_SOUND_TUNE*/

// YDA165  Device Information
struct snddev_yda165 {
	struct i2c_client *client;
	struct i2c_msg xfer_msg[2];
	struct mutex xfer_lock;
	struct mutex path_lock;
#if 1 // 20110708 by ssgun - check amp status
	atomic_t amp_enabled;
#endif
};

// YDA165  Platform Data
struct yda165_platform_data {
	int (*yda165_setup) (struct device *dev);
	void (*yda165_shutdown) (struct device *dev);
};

// YDA165  Register Format
typedef struct {
	u8 reg;
	u8 value;	
} yda165_register_type;

//////////////////////////////////////////////////////////////////////
// YDA165 Function Prototype
void yda165_init(void);
void yda165_exit(void);
void yda165_enable(AMP_PATH_TYPE_E path);
void yda165_disable(AMP_PATH_TYPE_E path);
#ifdef CONFIG_KTTECH_SOUND_TUNE
void yda165_tuning(void *data, size_t size);
#endif /*CONFIG_KTTECH_SOUND_TUNE*/
void yda165_enable_amplifier(void);
void yda165_disable_amplifier(void);

#endif /*__MACH_QDSP6V2_SNDDEV_YDA165_H__*/
