/*
 * aat2862_bl_ctrl.c - Backlignt control chip
 *
 * Copyright (C) 2010 KT Tech
 * 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <mach/gpio.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <mach/pmic.h>
#include <asm/atomic.h>
#include "msm_fb.h"
#include "mipi_novatek_bl.h"

#define BL_DIMING_DATA_CNT	4
#define BL_DELAY_1		4 //2
#define BL_DELAY_2		(BL_DELAY_1 << 1)

#define BL_ON			(31)

//EasyScale mode setting
#define EASYSCALE_MODE_SET()		\
{ 					\
	gpio_set_value(BL_ON, 1);	\
	udelay(120);			\
	gpio_set_value(BL_ON, 0); 	\
	udelay(280);			\
	gpio_set_value(BL_ON, 1);	\
}

// Device Address : 0x72
#define TPS61161_DEVICE_ID()		\
{ 					\
	LOW_BIT();			\
	HIGH_BIT();			\
	HIGH_BIT();			\
	HIGH_BIT();			\
	LOW_BIT();			\
	LOW_BIT();			\
	HIGH_BIT();			\
	LOW_BIT();			\
}

#define HIGH_BIT()			\
{					\
	gpio_set_value(BL_ON, 0);	\
	udelay(BL_DELAY_1);		\
	gpio_set_value(BL_ON, 1);	\
	udelay(BL_DELAY_2);		\
}

#define LOW_BIT() 			\
{ 					\
	gpio_set_value(BL_ON, 0);	\
	udelay(BL_DELAY_2);		\
	gpio_set_value(BL_ON, 1);	\
	udelay(BL_DELAY_1);		\
}

#define T_START()			\
{					\
	gpio_set_value(BL_ON, 1);	\
	udelay(BL_DELAY_2);		\
}

#define T_EOS()   			\
{ 					\
	gpio_set_value(BL_ON, 0);	\
	udelay(BL_DELAY_2);		\
}

#define STATIC_HIGH()			\
{					\
	gpio_set_value(BL_ON, 1);	\
}

atomic_t enter_easyscale_set;
DEFINE_MUTEX(tps6116_bl);
spinlock_t tps61161_spin_lock;

static void tps61161_set_device_id(void)
{
	TPS61161_DEVICE_ID(); // Device Address : 0x72
	T_EOS();
}

void tps61161_set_bl_native(int lcd_backlight_level)
{
	int i, bit_coding;
	unsigned long flags;
	
	//printk(KERN_CRIT "tps61161_set_bl_native : %d", lcd_backlight_level);
	
	mutex_lock(&tps6116_bl);
	
	if(lcd_backlight_level == 0) {
		gpio_set_value(BL_ON, 0);
	 	msleep(1);
		atomic_set(&enter_easyscale_set, 1);
	}
	else {
		if ((atomic_read(&enter_easyscale_set)) == 1) {
			spin_lock_irqsave(&tps61161_spin_lock, flags);
			EASYSCALE_MODE_SET();
			spin_unlock_irqrestore(&tps61161_spin_lock, flags);
	          	msleep(1);
			atomic_set(&enter_easyscale_set, 0);
		}

		spin_lock_irqsave(&tps61161_spin_lock, flags);

		T_START();

		tps61161_set_device_id();

		T_EOS();	

		T_START();

		LOW_BIT();  // 0 Request for Ack
		LOW_BIT();  // 0 Address bit 1
		LOW_BIT();  // 0 Address bit 0

		for(i = BL_DIMING_DATA_CNT; i >= 0; i--)
		{
			bit_coding = ((lcd_backlight_level >> i) & 0x1); // (lcd_backlight_level & (2 ^ i));

			if(bit_coding) {
				HIGH_BIT(); // Data High
			} else {
				LOW_BIT();  // Data Low
			}
		}

		T_EOS();
		STATIC_HIGH();

		spin_unlock_irqrestore(&tps61161_spin_lock, flags);	

		msleep(1);
	}
	mutex_unlock(&tps6116_bl);
}

EXPORT_SYMBOL(tps61161_set_bl_native);

void tps61161_set_bl(struct msm_fb_data_type *mfd)
{
	tps61161_set_bl_native(mfd->bl_level);
}

EXPORT_SYMBOL(tps61161_set_bl);

static int __init tps61161_init(void)
{
	int rc = 0;

	rc += gpio_request(BL_ON, "tps61161_init");

	mutex_init(&tps6116_bl);
	spin_lock_init(&tps61161_spin_lock);
	atomic_set(&enter_easyscale_set, 0);

#if 0 // KTTECH Remove For FTM
	rc += gpio_tlmm_config(GPIO_CFG(BL_ON, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA), GPIO_CFG_ENABLE);	
	rc += gpio_direction_output(BL_ON, 1);
#endif

	if(!rc) 
		printk("Sharp QHD LCD Backlight Controller Initialized.\n");
		
	return rc;
}

static void __exit tps61161_exit(void)
{
	gpio_free(BL_ON);
}

module_init(tps61161_init);
module_exit(tps61161_exit);

