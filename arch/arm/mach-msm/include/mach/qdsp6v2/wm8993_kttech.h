#ifndef __MACH_QDSP6V2_SNDDEV_WM8993_CODEC_H__
#define __MACH_QDSP6V2_SNDDEV_WM8993_CODEC_H__

#include <linux/mfd/msm-adie-codec.h>
#include <linux/i2c.h>
#include <mach/qdsp6v2/audio_amp_ctl.h>

// WM8993  Register Control
#define AMP_REGISTER_MAX		120
#define AMP_REGISTER_DELAY		0xFE	// WM8993 Delay
#define AMP_REGISTER_END		0xFF	// WM8993 END 

#ifdef CONFIG_KTTECH_SOUND_TUNE
#define REG_MEMORY   static
#define REG_COUNT    AMP_REGISTER_MAX
#else
#define REG_MEMORY   static const     /* Normally, tables are in ROM  */
#define REG_COUNT
#endif

// WM8993  Register Format
typedef struct {
	u8 reg;
	u16 value;
} wm8993_register_type;

//////////////////////////////////////////////////////////////////////
// WM8993  Function Prototype
void wm8993_set_caltype(AMP_CAL_TYPE_E cal);
AMP_CAL_TYPE_E wm8993_get_caltype(void);

void wm8993_set_pathtype(AMP_PATH_TYPE_E path);
AMP_PATH_TYPE_E wm8993_get_pathtype(void);

void wm8993_set_register(wm8993_register_type *amp_regs);

void wm8993_enable(AMP_PATH_TYPE_E path);
void wm8993_disable(AMP_PATH_TYPE_E path);
#ifdef CONFIG_KTTECH_SOUND_TUNE
void wm8993_tuning( void *data, size_t size );
#endif
void wm8993_reset(AMP_PATH_TYPE_E path);

//#ifdef WM8993_WHITE_NOISE_ZERO
void wm8993_enable_amplifier(void);
void wm8993_disable_amplifier(void);
//#endif

#endif /*__MACH_QDSP6V2_SNDDEV_WM8993_CODEC_H__*/
