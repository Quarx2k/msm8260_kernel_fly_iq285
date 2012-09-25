/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of Code Aurora Forum, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef __MACH_QDSP6V2_SNDDEV_WM8993_H__
#define __MACH_QDSP6V2_SNDDEV_WM8993_H__

#include <linux/i2c.h>
#include <linux/mfd/msm-adie-codec.h>
#include <mach/qdsp6v2/audio_amp_ctl.h>

// WM8993  Register Control
#define AMP_REGISTER_MAX		100
#define AMP_REGISTER_DELAY		0xFE // WM8993 Delay
#define AMP_REGISTER_END		0xFF // WM8993 END

#define WM8993_RESET_REG		0x00
#define WM8993_RESET_VALUE		0x8993
#define WM8993_SPK_SW_GPIO		105

#ifdef CONFIG_KTTECH_SOUND_TUNE
#define REG_MEMORY	static
#define REG_COUNT	AMP_REGISTER_MAX
#else
#define REG_MEMORY	static const     /* Normally, tables are in ROM  */
#define REG_COUNT
#endif


// WM8993  Device Information
struct snddev_wm8993{
	struct i2c_client *client;
	struct i2c_msg xfer_msg[2];
	struct mutex xfer_lock; // for register
	int mod_id;
	struct mutex path_lock; // for path
	struct mutex lock; // for ioctl
	uint32_t cal_type; // voip, video, etc(voice) call
	uint32_t eq_type;
	uint32_t mute_enabled;
	uint32_t voip_micgain;
};

// WM8993  Platform Data
struct wm8993_platform_data {
	int (*wm8993_setup) (struct device *dev);
	void (*wm8993_shutdown) (struct device *dev);
};

// WM8993  Register Format
typedef struct {
	u8 reg;
	u16 value;	
}wm8993_register_type;

//////////////////////////////////////////////////////////////////////
// WM8993 Function Prototype
void wm8993_init(void);
void wm8993_exit(void);

int wm8993_read(u8 reg, unsigned short *value);
int wm8993_write(u8 reg, unsigned short value);

void wm8993_set(int type, int value);
void wm8993_get(int type, int * value);

void wm8993_set_caltype(AMP_CAL_TYPE_E cal);
AMP_CAL_TYPE_E wm8993_get_caltype(void);

void wm8993_set_register(wm8993_register_type *amp_regs);

void wm8993_enable(AMP_PATH_TYPE_E path);
void wm8993_disable(AMP_PATH_TYPE_E path);

#ifdef CONFIG_KTTECH_SOUND_TUNE
void wm8993_tuning(void *data, size_t size);
#endif
void wm8993_reset(void);

#endif // __MACH_QDSP6V2_SNDDEV_WM8993_H__
