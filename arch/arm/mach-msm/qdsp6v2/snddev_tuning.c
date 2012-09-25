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
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/msm_audio.h>

#if defined(CONFIG_KTTECH_SOUND_WM8993)
#include <mach/qdsp6v2/snddev_wm8993.h>
#elif defined(CONFIG_KTTECH_SOUND_YDA165)
#include <mach/qdsp6v2/snddev_yda165.h>
#elif defined(CONFIG_KTTECH_SND_CODEC_WM8993)
#include <mach/qdsp6v2/wm8993_kttech.h>
#else
"no sound device to tuning"
#endif
#include <mach/qdsp6v2/audio_dev_ctl.h>

static int tuning_open(struct inode *inode, struct file *file)
{
	pr_debug("%s: open\n", __func__);
	return 0;
}

static int tuning_ioctl(struct inode *inode, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	pr_debug("%s: ioctl\n", __func__);
	return 0;
}

static ssize_t tuning_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *ppos)
{
	int rc = 0;
	void *k_buffer;

	k_buffer = kmalloc(count, GFP_KERNEL);
	if (!k_buffer)
		return -ENOMEM;

	if (copy_from_user(k_buffer, buf, count)) {
		rc = -EFAULT;
		goto write_out_free;
	}

	// write register
#if defined(CONFIG_KTTECH_SOUND_WM8993) || defined(CONFIG_KTTECH_SND_CODEC_WM8993)
	wm8993_tuning(k_buffer, count);
#elif defined(CONFIG_KTTECH_SOUND_YDA165)
	yda165_tuning(k_buffer, count);
#endif /*CONFIG_KTTECH_SOUND_WM8993*/

	rc = count;
write_out_free:
	kfree(k_buffer);
	return rc;
}

static int tuning_release(struct inode *inode, struct file *file)
{
	pr_debug("%s: release\n", __func__);
	return 0;
}

static struct file_operations tuning_dev_fops = {
	.owner      = THIS_MODULE,
	.open		= tuning_open,
	.ioctl		= tuning_ioctl,
	.write		= tuning_write,
	.release	= tuning_release,
};

struct miscdevice tuning_control_device = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "snddev_tuning",
	.fops   = &tuning_dev_fops,
};

static int __init tuning_ctl_init(void) {
	return misc_register(&tuning_control_device);
}

device_initcall(tuning_ctl_init);

