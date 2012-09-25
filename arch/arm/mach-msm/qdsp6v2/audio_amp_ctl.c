/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
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
/*
 * Sound Amp Driver Contrl
 */
#include <linux/kernel.h>

#include <mach/qdsp6v2/audio_amp_ctl.h>
#if defined(CONFIG_KTTECH_SOUND_WM8993)
#include <mach/qdsp6v2/snddev_wm8993.h>
#elif defined(CONFIG_KTTECH_SOUND_YDA165)
#include <mach/qdsp6v2/snddev_yda165.h>
#endif

static struct audio_amp_ops ops[AMP_MAX] = {	
	{
		.init = NULL,
		.exit = NULL,	
		.set = NULL,
		.get = NULL,
		.enable = NULL,		
		.disable = NULL,
	},
#if defined(CONFIG_KTTECH_SOUND_WM8993)
	{
		.init = wm8993_init,
		.exit = wm8993_exit,
		.set = wm8993_set,
		.get = wm8993_get,
		.enable = wm8993_enable,
		.disable = wm8993_disable,
	},
#elif defined(CONFIG_KTTECH_SOUND_YDA165)
	{
		.init = yda165_init,
		.exit = yda165_exit,
		.set = NULL,
		.get = NULL,
		.enable = yda165_enable,
		.disable = yda165_disable,
	},
#endif
};

static struct audio_amp_ops *amp_ops = &ops[AMP_NONE];

void audio_amp_init(AMP_DEVICE_E dev)
{
	if (dev >= AMP_MAX)
	{
		APM_INFO("[%d] invalid amp dev: %d is detected, so change to Null amp dev\n", __LINE__, dev );
		dev = AMP_NONE;
	}
	
	amp_ops = &ops[dev];
	if (amp_ops->init)
		amp_ops->init();
}
EXPORT_SYMBOL(audio_amp_init);

void audio_amp_exit(void)
{
	if (amp_ops->exit)
		amp_ops->exit();
}
EXPORT_SYMBOL(audio_amp_exit);

void audio_amp_set(int type, int value)
{
	if (amp_ops->set)
		amp_ops->set(type, value);
}
EXPORT_SYMBOL(audio_amp_set);

void audio_amp_get(int type, int * value)
{
	if (amp_ops->get)
		amp_ops->get(type, value);
}
EXPORT_SYMBOL(audio_amp_get);

void audio_amp_on(AMP_PATH_TYPE_E path)
{
	if (amp_ops->enable)
		amp_ops->enable(path);
}
EXPORT_SYMBOL(audio_amp_on);

void audio_amp_off(AMP_PATH_TYPE_E path)
{
	if (amp_ops->disable)
		amp_ops->disable(path);
}
EXPORT_SYMBOL(audio_amp_off);

