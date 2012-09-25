/*
 * capella.c - Proximity/Ambient light sensor
 *
 * Copyright (C) 2011 01 21 KT Tech
 * Kim Il Myung <ilmyung@xxxxxxxxxxx>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

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
#include <mach/board.h>
#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/capella_kttech.h>
#include <linux/alps_kttech.h>
#include <linux/earlysuspend.h>
#include <linux/err.h>
#include <linux/pmic8058-othc.h>
#include <linux/regulator/consumer.h>
#include <mach/pmic.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/wakelock.h>
#include <linux/suspend.h>
#endif

//#define CAPELLA_DEBUG_MSG

#define CAPELLA_IRQ_GPIO 124
#define CAPELLA_I2C_SCL 115
#define CAPELLA_I2C_SDA 116

#define CAPELLA_I2C_ADAPTER_ID 5
#define CAPELLA_ALS_I2C_DEVICE_N 0x01
#define CAPELLA_PS_I2C_DEVICE_N 0x02

#define CM3663_initial_add   0x22 
#define CM3663_initial_cmd  0x10 
#define CM3663_PS_cmd  0xB0
#define CM3663_PS_cmd_val  0x01
#define CM3663_PS_data  0xB1 
#define CM3663_PS_THD_cmd  0xB2 
#define CM3663_ALS_data_lsb  0x23 
#define CM3663_ALS_data_msb  0x21 
#define CM3663_ALS_cmd  0x20 
#define CM3663_ALS_cmd_val  0x03
#define check_interrupt_add  0x19 
#define check_interrupt_clear 0x18

#define CAPELLA_MAX_LUX 27
#define CAPELLA_ALS_POLL_INTERVAL_MAX	2000 /* 2 secs */
#define CAPELLA_ALS_POLL_INTERVAL_MIN	1000 /* 1 sec */
#define CAPELLA_PS_POLL_INTERVAL_MAX	500 /* 500 msecs */
#define CAPELLA_PS_POLL_INTERVAL_MIN	300 /* 300 msecs */
#define CAPELLA_SENSORS_ACTIVE_LIGHT	0x01
#define CAPELLA_SENSORS_ACTIVE_PROXIMITY	0x02
#define CAPELLA_I2C_RETRY 2

#define CAPELLA_SLEEP  1
#define CAPELLA_ACTIVE 0

#define CAPELLA_LUX_FILTERING_CNT 1 /* 2*500ms = about 1 sec */
#define CAPELLA_LUX_FILTERING_TIMEOUT 32 /* offset: 2 -> if unexpeted value is over two during correct the lux value, timeout will work. */
#define CAPELLA_LUX_OFFSET_ES1 850
#define CAPELLA_LUX_OFFSET_ES2 850
#define CAPELLA_LUX_OFFSET_PP 1430

struct capella_chip {
	struct device *dev;
	struct work_struct work;
	struct mutex lock;
	
	/* Proximity */
	int enable;
	int proximity_raw_data;
	int proximity_prev_val;
	int proximity_val;
	/* Ambient Light */
	int level;
	int light_lux;
	int light_sensor_output;
	/* PS IRQ */
	int ps_irq;
	/* Spin lock */
	spinlock_t spin_lock;
};

static struct capella_chip *g_capella_chip;
static struct platform_device *pdev;

static struct i2c_driver capella_als_driver;
static struct i2c_client *capella_als_client = NULL;

static struct i2c_driver capella_ps_driver;
static struct i2c_client *capella_ps_client = NULL;

static struct input_polled_dev *ps_poll_dev;
static struct input_polled_dev *als_poll_dev;

struct wake_lock capella_wakelock;

#ifdef CONFIG_HAS_EARLYSUSPEND
struct early_suspend capella_e_suspend;
#endif

static int capellaActiveMask = 0;
static int power_state = 0;
static int prox_thd = 0;
static int prox_thd_offset = 0;
static int prox_val_offset = 0;
static int capella_ps_suspended = 0;
static int capella_als_suspended = 0;
static int capella_lux_offset_factor = 0;

// For Touch Screen : By JhoonKim
extern int qt602240_get_proximity_enable_state;	

static int capella_lux_table[CAPELLA_MAX_LUX] = {
#if 0
 20, 50, 100, 150, 200, 250, 300, 350, 400, 450, 
 500, 600, 700, 800, 1000, 1200, 1500, 2000, 3000, 4500, 
 6000, 8000, 11000, 16000, 26000, 46000, 76000};
#else	// By JhoonKim : Modified LUX Math. Table
 4, 10, 20, 30, 40, 50, 60, 70, 85, 100, 150, 200, 250, 300, 400, 500, 600, 700, 800, 1000, 1200, 1500, 2000, 3000, 4000, 5000
};
#endif

static void CAPELLA_I2C_PORT_CONFIG(void)
{
	printk("CAPELLA_I2C_PORT_CONFIG\n");
	gpio_tlmm_config(GPIO_CFG(CAPELLA_I2C_SCL, 2, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_8MA), GPIO_CFG_ENABLE);
	gpio_tlmm_config(GPIO_CFG(CAPELLA_I2C_SDA, 2, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_8MA), GPIO_CFG_ENABLE);
}

static void CAPELLA_I2C_PORT_DECONFIG(void)
{
	printk("CAPELLA_I2C_PORT_DECONFIG\n");
	gpio_tlmm_config(GPIO_CFG(CAPELLA_I2C_SCL, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
	gpio_tlmm_config(GPIO_CFG(CAPELLA_I2C_SDA, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
	gpio_set_value(CAPELLA_I2C_SCL, 0);
	gpio_set_value(CAPELLA_I2C_SDA, 0);
}

static int CAPELLA_ALS_I2C_RxData(unsigned short addr, char *rxData, int length)
{
	int retry;
	int ret = 0;

	struct i2c_msg msgs[] = {
		{
			.addr = addr >> 1,
			.flags = I2C_M_RD,
			.len = length,
			.buf = rxData,
		},
	};

	for (retry = 0 ; retry < CAPELLA_I2C_RETRY ; retry++) {
		ret = i2c_transfer(capella_als_client->adapter, msgs, 1);
		if (ret > 0)
			break;
		else {
			CAPELLA_I2C_PORT_CONFIG();
			msleep(10);
		}
	}

	if (retry >= CAPELLA_I2C_RETRY) {
		printk(KERN_ERR "%s: retry over %d : error[%d]\n",__func__,CAPELLA_I2C_RETRY, ret );
		return ret;
	} else
		return 0;
}

static int CAPELLA_ALS_I2C_TxData(unsigned short addr, char *txData, int length)
{
	int retry;
	int ret =0;
	struct i2c_msg msg[] = {
		{
			.addr = addr >> 1,
			.flags = 0,
			.len = length,
			.buf = txData,
		},
	};

	for (retry = 0 ; retry < CAPELLA_I2C_RETRY ; retry++) {
		ret = i2c_transfer(capella_als_client->adapter, msg, 1);
		if(ret > 0)
			break;
		else {
			CAPELLA_I2C_PORT_CONFIG();
			msleep(10);
		}
	}

	if (retry >= CAPELLA_I2C_RETRY) {
		printk(KERN_ERR "%s: retry over %d : error[%d]\n", __func__,CAPELLA_I2C_RETRY, ret);
		return ret;
	} else
		return 0;
}

static int CAPELLA_PS_I2C_RxData(unsigned short addr, char *rxData, int length)
{
	int retry;
	int ret = 0;

	struct i2c_msg msgs[] = {
		{
			.addr = addr >> 1,
			.flags = I2C_M_RD,
			.len = length,
			.buf = rxData,
		},
	};

	for (retry = 0 ; retry < CAPELLA_I2C_RETRY ; retry++) {
		ret = i2c_transfer(capella_ps_client->adapter, msgs, 1);
		if (ret > 0)
			break;
		else
			msleep(10);
	}

	if (retry >= CAPELLA_I2C_RETRY) {
		printk(KERN_ERR "%s: retry over %d : error[%d]\n",__func__, CAPELLA_I2C_RETRY,  ret);
		CAPELLA_I2C_PORT_CONFIG();
		return ret;
	} else
		return 0;
}

static int CAPELLA_PS_I2C_TxData(unsigned short addr, char *txData, int length)
{
	int retry;
	int ret =0;
	struct i2c_msg msg[] = {
		{
			.addr = addr >> 1,
			.flags = 0,
			.len = length,
			.buf = txData,
		},
	};

	for (retry = 0 ; retry < CAPELLA_I2C_RETRY ; retry++) {
		ret = i2c_transfer(capella_ps_client->adapter, msg, 1);
		if(ret > 0)
			break;
		else
			msleep(10);
	}

	if (retry >= CAPELLA_I2C_RETRY) {
		printk(KERN_ERR "%s: retry over  %d: error[%d]\n", __func__, CAPELLA_I2C_RETRY, ret);
		CAPELLA_I2C_PORT_CONFIG();
		return ret;
	} else
		return 0;
}

static void capella_init_als_sensor(void)
{
	char buffer[1];
	int ret = 0;

	// buffer[0] = 0x10; // interrupt mode
	buffer[0] = 0x20; // non interrupt mode
	ret = CAPELLA_ALS_I2C_TxData(CM3663_initial_add, buffer, 1);
	if(ret < 0)
		printk("capella_init_als_sensor : Error [%d]\n", ret);

	//tunnig factor
	buffer[0] = CM3663_ALS_cmd_val; // WORD mode

	ret = CAPELLA_ALS_I2C_TxData(CM3663_ALS_cmd, buffer, 1);
	if(ret < 0)
		printk("capella_init_als_sensor : Error [%d]\n", ret);

#if 0
	//clear INT
	ret = CAPELLA_ALS_I2C_RxData(check_interrupt_clear, buffer, 1);
	if(ret < 0)
		printk("capella_init_als_sensor : Error [%d]\n", ret);
#endif	
}

/* set  operation mode 0 = normal, 1 = sleep*/
static int capella_set_mode_als_sensor(int mode)
{
	char buffer[1];
	int ret = 0;

	if(mode == CAPELLA_SLEEP)
	{
		buffer[0] = (CM3663_ALS_cmd_val & 0xFE) | 0x01;
		ret = CAPELLA_ALS_I2C_TxData(CM3663_ALS_cmd, buffer, 1);
	}
	else
	{
		buffer[0] = (CM3663_ALS_cmd_val & 0xFE) | 0x00;
		ret = CAPELLA_ALS_I2C_TxData(CM3663_ALS_cmd, buffer, 1);
	}

	if(ret < 0)
	{
		printk("capella_set_mode_als_sensor : I2C write Error [%d]\n", ret);
		return -1;
	}

	return ret;
}


static void capella_init_ps_sensor(void)
{
	char buffer[1];
	int ret = 0;

	buffer[0] = prox_thd;
	ret = CAPELLA_PS_I2C_TxData(CM3663_PS_THD_cmd, buffer, 1);
	if(ret < 0)
		printk("capella_init_ps_sensor : Error [%d]\n", ret);

	//tunnig factor
	buffer[0] = CM3663_PS_cmd_val;
	ret = CAPELLA_PS_I2C_TxData(CM3663_PS_cmd, buffer, 1);
	if(ret < 0)
		printk("capella_init_ps_sensor : Error [%d]\n", ret);

#if 0
	//clear INT
	ret = CAPELLA_PS_I2C_RxData(check_interrupt_clear, buffer, 1);
	if(ret < 0)
		printk("capella_init_ps_sensor : Error [%d]\n", ret);
#endif
}

/* set  operation mode 0 = normal, 1 = sleep*/
static int capella_set_mode_ps_sensor(int mode)
{
	char buffer[1];
	int ret = 0;

	if(mode == CAPELLA_SLEEP)
	{
		buffer[0] = (CM3663_PS_cmd_val & 0xFE) | 0x01;
		ret = CAPELLA_PS_I2C_TxData(CM3663_PS_cmd, buffer, 1);
		g_capella_chip->proximity_prev_val = 0xFF;
	}
	else
	{
		buffer[0] = (CM3663_PS_cmd_val & 0xFE) | 0x00;
		ret = CAPELLA_PS_I2C_TxData(CM3663_PS_cmd, buffer, 1);
		g_capella_chip->proximity_prev_val = 0xFF;
	}

	if(ret < 0)
	{
		printk("capella_set_mode_ps_sensor : I2C write Error [%d]\n", ret);
		return -1;
	}

	return ret;
}

static int capella_read_light_data(void)
{
	char buffer[1];
	int ret = 0;
	int als_data;

	// read als
	ret = CAPELLA_ALS_I2C_RxData(CM3663_ALS_data_msb,buffer, 1);
	if(ret < 0)
	{
		printk("capella_get_lux : Error [%d]\n", ret);
		return ret;
	}
	als_data = buffer[0]; 
	als_data = als_data << 8; 

	ret = CAPELLA_ALS_I2C_RxData(CM3663_ALS_data_lsb,buffer, 1);
	if(ret < 0)
	{
		printk("capella_get_lux : Error [%d]\n", ret);
		return ret;
	}
	als_data = als_data | buffer[0];
	return als_data;
}

static int capella_get_light_data(void)
{
	int i = 0;
	int light_data = 0;

	//convert the sensor ouput to lux output
	light_data = capella_read_light_data();

	if(light_data < 0)
	{
		if(light_data == -ETIMEDOUT)
		{
			capella_power_reset();
			alps_power_reset();
		}
		return light_data;
	}
	g_capella_chip->light_sensor_output = light_data; 
	g_capella_chip->light_lux = g_capella_chip->light_sensor_output * capella_lux_offset_factor / 100;

	for (i = 0 ; i < CAPELLA_MAX_LUX ; i++) {
		if (capella_lux_table[i] > g_capella_chip->light_lux) {
			g_capella_chip->light_lux = capella_lux_table[i];
			break;
		}
	}

	return 0;
}


// read proximity_val
static int capella_read_proximity_data(void)
{
	char buffer[1];
	int ret = 0;

	buffer[0] = 0;
	ret = CAPELLA_PS_I2C_RxData(CM3663_PS_data, buffer, 1);
	if(ret < 0)
	{
		printk("capella_read_proximity_data : Error [%d]\n", ret);
		return ret;
	}
	return buffer[0];
}

static int capella_get_proximity_data(void)
{
	int prox_data = 0;

	prox_data = capella_read_proximity_data();

	if(prox_data < 0)
	{
		if(prox_data == -ETIMEDOUT)
		{
			capella_power_reset();
			alps_power_reset();
		}
		return prox_data;
	}
	g_capella_chip->proximity_raw_data = prox_data;

	if(g_capella_chip->proximity_raw_data >= prox_thd + prox_thd_offset)
		g_capella_chip->proximity_val = 0;
	else if(g_capella_chip->proximity_raw_data <= prox_thd - prox_thd_offset)
		g_capella_chip->proximity_val = 1;

	return 0;
}

static void  capella_als_report(struct input_polled_dev *dev)
{
	struct input_dev *input = dev->input;

	if(power_state == 0 || capella_als_suspended == 1)
		return;

	if(capellaActiveMask & CAPELLA_SENSORS_ACTIVE_LIGHT)
	{
		if(capella_get_light_data() != 0)
			return;

		input_report_abs(input,  ABS_MISC, g_capella_chip->light_lux);
		input_sync(input);
	}	
}

static void  capella_ps_report(struct input_polled_dev *dev)
{
	struct input_dev *input = dev->input;

	if(power_state == 0 || capella_ps_suspended == 1)
		return;

	if(capellaActiveMask & CAPELLA_SENSORS_ACTIVE_PROXIMITY)
	{
		if(capella_get_proximity_data() != 0)
		{
			g_capella_chip->proximity_prev_val = 0xFF;
			return;
		}

		if(g_capella_chip->proximity_prev_val != g_capella_chip->proximity_val)
		{
			input_report_abs(input, ABS_DISTANCE, g_capella_chip->proximity_val );
			input_sync(input);
		}
		g_capella_chip->proximity_prev_val = g_capella_chip->proximity_val;
	}
}

static struct regulator *capella_8058_lvs0; //1.8V pull up
static struct regulator *capella_8058_l2; //2.80v main power
static struct regulator *capella_8901_l3; //3.0v main power

static int capella_power_control(int on)
{
	int rc = 0;

	if(on) {

		if(power_state)
		{
			pr_err("%s: already power on\n", __func__);
			return 0;
		}  

		capella_8058_lvs0 = regulator_get(NULL, "8058_lvs0");
		capella_8058_l2 = regulator_get(NULL, "8058_l2");
		capella_8901_l3 = regulator_get(NULL, "8901_l3");

		if (IS_ERR(capella_8058_l2) || IS_ERR(capella_8058_lvs0) || IS_ERR(capella_8901_l3)) {
			pr_err("%s: regulator_get(8058_l2) failed (%d)\n", __func__, rc);
			return PTR_ERR(capella_8058_l2);
		}

		CAPELLA_I2C_PORT_CONFIG();

		//set voltage level
		rc = regulator_set_voltage(capella_8058_l2, 2700000, 2700000);
		if (rc)
		{ 
			pr_err("%s: regulator_set_voltage(capella_8058_l2) failed (%d)\n", __func__, rc);
			regulator_put(capella_8058_l2);
			return rc;
		}

		//set voltage level
		rc = regulator_set_voltage(capella_8901_l3,3000000, 3000000);
		if (rc)
		{ 
			pr_err("%s: regulator_set_voltage(capella_8901_l3) failed (%d)\n", __func__, rc);
			regulator_put(capella_8901_l3);
			return rc;
		}

		//enable output
		rc = regulator_enable(capella_8058_l2);
		if (rc)
		{ 
			pr_err("%s: regulator_enable(capella_8058_l2) failed (%d)\n", __func__, rc);
			regulator_put(capella_8058_l2);
			return rc;
		}

		rc = regulator_enable(capella_8058_lvs0);
		if (rc)
		{ 
			pr_err("%s: regulator_enable(lvs0) failed (%d)\n", __func__, rc);
			regulator_put(capella_8058_lvs0);
			return rc;
		}

		//enable output
		rc = regulator_enable(capella_8901_l3);
		if (rc)
		{ 
			pr_err("%s: regulator_enable(capella_8901_l3) failed (%d)\n", __func__, rc);
			regulator_put(capella_8901_l3);
			return rc;
		}

		power_state = 1;
	}
	else {		
		if (capella_8901_l3)
		{
			rc = regulator_force_disable(capella_8901_l3);

			if (rc)
			{ 
				pr_err("%s: regulator_disable(l4) failed (%d)\n", __func__, rc);
				regulator_put(capella_8901_l3);
				return rc;
			}
			regulator_put(capella_8901_l3);

			capella_8901_l3 = NULL;
		}

		if (capella_8058_l2)
		{
			rc = regulator_force_disable(capella_8058_l2);

			if (rc)
			{ 
				pr_err("%s: regulator_disable(8901_l2) failed (%d)\n", __func__, rc);
				regulator_put(capella_8058_l2);
				return rc;
			}
			regulator_put(capella_8058_l2);

			capella_8058_l2 = NULL;
		}

		if (capella_8058_lvs0)
		{
			rc = regulator_force_disable(capella_8058_lvs0);

			if (rc)
			{ 
				pr_err("%s: regulator_disable(8901_l2) failed (%d)\n", __func__, rc);
				regulator_put(capella_8058_lvs0);
				return rc;
			}
			regulator_put(capella_8058_lvs0);
		
			capella_8058_lvs0 = NULL;
		}

		CAPELLA_I2C_PORT_DECONFIG();

		power_state = 0;	
	}

	return 0;
}

static void capella_init_i2c(void)
{
	int i = 0;
	unsigned long irq_flags;

	spin_lock_irqsave(&g_capella_chip->spin_lock, irq_flags);
	gpio_tlmm_config(GPIO_CFG(CAPELLA_I2C_SCL, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
	gpio_tlmm_config(GPIO_CFG(CAPELLA_I2C_SDA, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
	/* Start condition  + clock cycle 1 */	
	udelay(5);
	gpio_set_value(CAPELLA_I2C_SDA, 0);
	udelay(5);
	gpio_set_value(CAPELLA_I2C_SCL, 0);
	udelay(5);
	gpio_set_value(CAPELLA_I2C_SCL, 1);
	udelay(5);
	gpio_set_value(CAPELLA_I2C_SDA, 1);
	for(i = 0 ; i < 19 ; i++)
	{
		if(i == 18)
			gpio_set_value(CAPELLA_I2C_SDA, 0);

		gpio_set_value(CAPELLA_I2C_SCL, 0);
		udelay(5);
		gpio_set_value(CAPELLA_I2C_SCL, 1);
		udelay(5);
	}
	
	gpio_set_value(CAPELLA_I2C_SDA, 1);

	spin_unlock_irqrestore(&g_capella_chip->spin_lock, irq_flags);
}

void capella_power_reset(void)
{
	printk(KERN_ERR "capella power reset !!!\n");

	capella_power_control(0);
	capella_power_control(1);

	/* resolve I2C problem */
	capella_init_i2c();
	CAPELLA_I2C_PORT_CONFIG();

	capella_init_ps_sensor();
	capella_init_als_sensor();

	if(capellaActiveMask & CAPELLA_SENSORS_ACTIVE_PROXIMITY)
		capella_set_mode_ps_sensor(CAPELLA_ACTIVE);
	else
		capella_set_mode_ps_sensor(CAPELLA_SLEEP);

	if(capellaActiveMask & CAPELLA_SENSORS_ACTIVE_LIGHT)
		capella_set_mode_als_sensor(CAPELLA_ACTIVE);
	else
		capella_set_mode_als_sensor(CAPELLA_SLEEP);
}

EXPORT_SYMBOL(capella_power_reset);

static void capella_power_on(void)
{
#ifdef CAPELLA_DEBUG_MSG
	printk("\n capella_power_on \n");
#endif
	capella_power_control(1);
}

static int capella_open(struct inode *inode, struct file *file)
{
#ifdef CAPELLA_DEBUG_MSG
	printk(KERN_ERR " capella_open: \n");
#endif
	return 0;
}

static int capella_release(struct inode *inode, struct file *file)
{
#ifdef CAPELLA_DEBUG_MSG
	printk("capella_release !!!!\n");
#endif
	return 0;
}

static int capella_ioctl(struct inode *inode, struct file *file, unsigned int cmd,  unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	short delay;
	char cal_data;
	int is_power;
	int ret = 0;

	switch (cmd) {

		case CAPELLA_IOCTL_LIGHT_ENABLE :
#ifdef CAPELLA_DEBUG_MSG
			printk("capella: CAPELLA_IOCTL_LIGHT_ENABLE\n");
#endif
			capella_set_mode_als_sensor(CAPELLA_ACTIVE);
			capellaActiveMask |= CAPELLA_SENSORS_ACTIVE_LIGHT;
			break;

		case CAPELLA_IOCTL_LIGHT_DISABLE :
#ifdef CAPELLA_DEBUG_MSG
			printk("capella: CAPELLA_IOCTL_LIGHT_DISABLE\n");
#endif
			capella_set_mode_als_sensor(CAPELLA_SLEEP);
			capellaActiveMask &=~CAPELLA_SENSORS_ACTIVE_LIGHT;
			break;

		case CAPELLA_IOCTL_PROXIMITY_ENABLE :
#ifdef CAPELLA_DEBUG_MSG
			printk("capella: CAPELLA_IOCTL_PROXIMITY_ENABLE\n");
#endif
			capella_set_mode_ps_sensor(CAPELLA_ACTIVE);
			if(!(capellaActiveMask & CAPELLA_SENSORS_ACTIVE_PROXIMITY))
			{
				ret = enable_irq_wake(g_capella_chip->ps_irq);
				if (ret) {
					printk("capella: enable_irq_wake failed\n");
				}
			}
			capellaActiveMask |= CAPELLA_SENSORS_ACTIVE_PROXIMITY;
			
			// For Touch Screen : By JhoonKim
			qt602240_get_proximity_enable_state = 1;
			break;

		case CAPELLA_IOCTL_PROXIMITY_DISABLE :
#ifdef CAPELLA_DEBUG_MSG
			printk("capella: CAPELLA_IOCTL_PROXIMITY_DISABLE\n");
#endif
			if(capellaActiveMask & CAPELLA_SENSORS_ACTIVE_PROXIMITY)
			{
				ret = disable_irq_wake(g_capella_chip->ps_irq);
				if (ret) {
					printk("capella: disable_irq_wake failed\n");
				}
			}
			capella_set_mode_ps_sensor(CAPELLA_SLEEP);
			capellaActiveMask &=~CAPELLA_SENSORS_ACTIVE_PROXIMITY;

			// For Touch Screen : By JhoonKim
			qt602240_get_proximity_enable_state = 0;
			break;

		case CAPELLA_IOCTL_LIGHT_SET_DELAY:
			if (copy_from_user(&delay, argp, sizeof(delay)))
				return -EFAULT;

			als_poll_dev->poll_interval = (unsigned int)delay;

			if(als_poll_dev->poll_interval	> CAPELLA_ALS_POLL_INTERVAL_MAX)
				als_poll_dev->poll_interval  = CAPELLA_ALS_POLL_INTERVAL_MAX;

			if(als_poll_dev->poll_interval	< CAPELLA_ALS_POLL_INTERVAL_MIN)
				als_poll_dev->poll_interval  = CAPELLA_ALS_POLL_INTERVAL_MIN;

#ifdef CAPELLA_DEBUG_MSG
			printk(KERN_ERR "CAPELLA_IOCTL_LIGHT_SET_DELAY: als delay[%d]\n", als_poll_dev->poll_interval);
#endif	  
			break;

		case CAPELLA_IOCTL_PROX_SET_DELAY:
			if (copy_from_user(&delay, argp, sizeof(delay)))
				return -EFAULT;

			ps_poll_dev->poll_interval = (unsigned int)delay;

			if(ps_poll_dev->poll_interval	> CAPELLA_PS_POLL_INTERVAL_MAX)
				ps_poll_dev->poll_interval	= CAPELLA_PS_POLL_INTERVAL_MAX;

			if(ps_poll_dev->poll_interval	< CAPELLA_PS_POLL_INTERVAL_MIN)
				ps_poll_dev->poll_interval	= CAPELLA_PS_POLL_INTERVAL_MIN;

#ifdef CAPELLA_DEBUG_MSG
			printk(KERN_ERR "CAPELLA_IOCTL_PROX_SET_DELAY: ps delay[%d]\n", ps_poll_dev->poll_interval);
#endif	  
			break;

		case CAPELLA_IOCTL_IS_POWER:

			is_power = capellaActiveMask;
			if( copy_to_user(argp, &is_power, sizeof(is_power)) != 0 )
				return -EFAULT;
			break;

		case CAPELLA_IOCTL_SET_PROX_CAL:
			if (copy_from_user(&cal_data, argp, sizeof(cal_data)))
				return -EFAULT;
			prox_val_offset = cal_data;
			printk(KERN_ERR "CAPELLA_IOCTL_SET_PROX_CAL: prox_val_offset[%d]\n", prox_val_offset);
			prox_thd = 4 + prox_val_offset;
			capella_init_ps_sensor();
			break;

		default:
			//printk("capella_ioctl : default [%d]\n", cmd);
			break;
	}

	return 0;
}

static ssize_t capella_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	if(cnt == 4) /* Read Prox only */
	{
		if(capellaActiveMask & CAPELLA_SENSORS_ACTIVE_PROXIMITY)
		{
			int prox_raw_data = capella_read_proximity_data();

			if (copy_to_user((void *)ubuf, &prox_raw_data, 4))
				return -EFAULT;
		}
		
	}
	else if(cnt == 8)
	{
		if(capellaActiveMask & CAPELLA_SENSORS_ACTIVE_LIGHT)
		{
			int light_raw_data = capella_read_light_data() * capella_lux_offset_factor / 100;

			if (copy_to_user((void *)ubuf, &light_raw_data, 4))
				return -EFAULT;
		}	
	}
	else
		return -EFAULT;

	return 4;
}


static int capella_als_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		dev_err(&client->adapter->dev, "!!client not i2c capable\n");
		return -ENOMEM;
	}

	capella_als_client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!capella_als_client) {
		dev_err(&client->adapter->dev, "failed to allocate\n");
		return -ENOMEM;
	}

	i2c_set_clientdata(client, capella_als_client);

	capella_als_client = client;

	dev_info(&client->adapter->dev, "detected ps sensor\n");

	g_capella_chip->level = 0;
	g_capella_chip->light_lux = 0;

	return 0;
}

static const struct i2c_device_id CAPELLA_als_id[] = {
	{ CAPELLA_ALS_I2C_NAME, 0 },
	{ }
};

static struct i2c_driver capella_als_driver = {
	.probe = capella_als_probe,
	.id_table	= CAPELLA_als_id,
	.driver = {
		.name = CAPELLA_ALS_I2C_NAME,
	},
};

int capella_als_open(void)
{
	struct i2c_board_info i2c_info;
	struct i2c_adapter *adapter;

	int rc = i2c_add_driver(&capella_als_driver);

	if (rc != 0) {
		printk("!!!can't add i2c driver\n");
		rc = -ENOTSUPP;
		return rc;
	}

	memset(&i2c_info, 0, sizeof(struct i2c_board_info));
	i2c_info.addr = CAPELLA_ALS_I2C_DEVICE_N;
	strlcpy(i2c_info.type, CAPELLA_ALS_I2C_NAME , I2C_NAME_SIZE);

	adapter = i2c_get_adapter(CAPELLA_I2C_ADAPTER_ID);
	if (!adapter) {
		printk("!!! can't get i2c adapter %d\n", CAPELLA_I2C_ADAPTER_ID);
		rc = -ENOTSUPP;
		goto probe_done;
	}

	capella_als_client = i2c_new_device(adapter, &i2c_info);
	capella_als_client->adapter->timeout = 0;
	capella_als_client->adapter->retries = 0;

	i2c_put_adapter(adapter);
	if (!capella_als_client) {
		printk("!!!can't add i2c device at 0x%x\n",(unsigned int)i2c_info.addr);
		rc = -ENOTSUPP;
	}
	return 0;

	probe_done: 
		return rc;
}

static int capella_ps_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		dev_err(&client->adapter->dev, "!!client not i2c capable\n");
		return -ENOMEM;
	}

	capella_ps_client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!capella_ps_client) {
		dev_err(&client->adapter->dev, "!! failed to allocate\n");
		return -ENOMEM;
	}

	i2c_set_clientdata(client, capella_ps_client);

	capella_ps_client = client;

	dev_info(&client->adapter->dev, "!! detected ps sensor\n");

	g_capella_chip->enable = 1;
	g_capella_chip->proximity_val =1;
	g_capella_chip->proximity_prev_val = 0xFF;

	return 0;
}

static const struct i2c_device_id CAPELLA_ps_id[] = {
	{ CAPELLA_PS_I2C_NAME, 0 },
	{ }
};

static struct i2c_driver capella_ps_driver = {
	.probe = capella_ps_probe,
	.id_table	= CAPELLA_ps_id,
	.driver = {
		.name = CAPELLA_PS_I2C_NAME,
	},
};

int capella_ps_open(void)
{
	struct i2c_board_info i2c_info;
	struct i2c_adapter *adapter;

	int rc = i2c_add_driver(&capella_ps_driver);

	if (rc != 0) {
		printk("!!!can't add i2c driver\n");
		rc = -ENOTSUPP;
		return rc;
	}
	memset(&i2c_info, 0, sizeof(struct i2c_board_info));
	i2c_info.addr = CAPELLA_PS_I2C_DEVICE_N;
	strlcpy(i2c_info.type, CAPELLA_PS_I2C_NAME , I2C_NAME_SIZE);

	adapter = i2c_get_adapter(CAPELLA_I2C_ADAPTER_ID);

	if (!adapter) {
		printk("!!! can't get i2c adapter %d\n", CAPELLA_I2C_ADAPTER_ID);
		rc = -ENOTSUPP;
		goto probe_done;
	}

	capella_ps_client = i2c_new_device(adapter, &i2c_info);
	capella_ps_client->adapter->timeout = 0;
	capella_ps_client->adapter->retries = 0;

	i2c_put_adapter(adapter);
	if (!capella_ps_client) {
		printk("can't add i2c device at 0x%x\n",(unsigned int)i2c_info.addr);
		rc = -ENOTSUPP;
	}
	return 0;

	probe_done: 
		return rc;
}

static struct file_operations capella_fops = {
	.owner = THIS_MODULE,
	.open = capella_open,
	.release = capella_release,
	.ioctl = capella_ioctl,
	.read = capella_read,
};


static struct miscdevice capella_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = CAPELLA_DEVICE_FILE_NAME,
	.fops = &capella_fops,
};

static void capella_irq_work(struct work_struct *work)
{	
	mutex_lock(&g_capella_chip->lock);
	if(capellaActiveMask & CAPELLA_SENSORS_ACTIVE_PROXIMITY)
	{
		wake_lock_timeout(&capella_wakelock, 3*HZ);
		capella_ps_report(ps_poll_dev);
	}
	enable_irq(g_capella_chip->ps_irq);
	mutex_unlock(&g_capella_chip->lock);
}

static irqreturn_t capella_irq(int irq, void *data)
{
	struct capella_chip *chip = data;

	if (!work_pending(&chip->work)) {
		disable_irq_nosync(g_capella_chip->ps_irq);
		schedule_work(&chip->work);
	} 
	else {
		printk("capella_irq : work pending\n");
	}
	
	return IRQ_HANDLED;
}

static int capella_register_irq(struct capella_chip *chip)
{
	int ret=0;

	if(!g_capella_chip)
	{
		printk("capella_register_irq ERROR : g_capella_chip is NULL\n");
		return -1;
	}
	
	if(g_capella_chip->ps_irq != 0)
		return ret;

	if (gpio_tlmm_config(GPIO_CFG(CAPELLA_IRQ_GPIO, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_4MA), GPIO_CFG_ENABLE)) {
		printk(KERN_ERR "%s: Err: Config CAPELLA_IRQ_GPIO\n", __func__);
	}
	
	INIT_WORK(&g_capella_chip->work, capella_irq_work);

	wake_lock_init(&capella_wakelock, WAKE_LOCK_SUSPEND, "capella_irq");

	mutex_init(&g_capella_chip->lock);

	g_capella_chip->ps_irq = MSM_GPIO_TO_INT(CAPELLA_IRQ_GPIO);

	ret = request_irq(g_capella_chip->ps_irq, capella_irq, (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING), CAPELLA_PROX_DEVICE_INPUT_NAME, g_capella_chip);
	if (ret) {
		printk("can't get IRQ %d, ret %d\n", g_capella_chip->ps_irq, ret);
		goto error_irq;
	}

	ret = enable_irq_wake(g_capella_chip->ps_irq);
	if (ret) {
		printk("capella: enable_irq_wake failed\n");
		goto error_irq;
	}
	
	ret = disable_irq_wake(g_capella_chip->ps_irq);
	if (ret) {
		printk("capella: disable_irq_wake failed\n");
		goto error_irq;
	}

	return 0;

	error_irq:
		free_irq(g_capella_chip->ps_irq, 0);
		return ret;
}

static int capella_probe(struct platform_device *dev)
{
	int ret;

	if(capella_ps_open()) 
	{
		return -1;
	} 

	if(capella_als_open())
	{
		return -1;
	}  

	spin_lock_init(&g_capella_chip->spin_lock);

	ret = misc_register(&capella_device);
	if (ret) {
		printk(KERN_ERR "!!! capella probe: misc failed\n");
	}

	capella_power_on();

	/* resolve I2C problem */
	capella_init_i2c();
	CAPELLA_I2C_PORT_CONFIG();

	capella_init_ps_sensor();
	capella_init_als_sensor();

	capella_register_irq(g_capella_chip);
	device_init_wakeup(&dev->dev, 1);

	return 0;
}

static int capella_remove(struct platform_device *dev)
{
	i2c_del_driver(&capella_als_driver);
	i2c_del_driver(&capella_ps_driver);
	device_init_wakeup(&dev->dev, 0);
	cancel_work_sync(&g_capella_chip->work);
	wake_lock_destroy(&capella_wakelock);
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void capella_early_suspend(struct early_suspend *h)
{
	if(!(capellaActiveMask & CAPELLA_SENSORS_ACTIVE_PROXIMITY))
		capella_ps_suspended = 1;

	if(!(capellaActiveMask & CAPELLA_SENSORS_ACTIVE_LIGHT))
		capella_als_suspended = 1;
}

static void capella_early_resume(struct early_suspend *h)
{
	capella_ps_suspended = 0;
	capella_als_suspended = 0;
}
#endif

static struct platform_driver capella_drvier = {
	.probe = capella_probe,
	.remove = capella_remove,
	.driver	= {
	.name = "capella",
	.owner = THIS_MODULE,
	},
};

static int __init capella_init(void)
{
	int ret;
	int hw_ver;

	hw_ver = get_kttech_hw_version();
	prox_thd_offset = 0;
	
	if(hw_ver <= O3_ES1)
	{
		prox_thd = 12;
		capella_lux_offset_factor = CAPELLA_LUX_OFFSET_ES1;
	}
	else if(hw_ver == O3_ES2)
	{
		prox_thd = 9;
		capella_lux_offset_factor = CAPELLA_LUX_OFFSET_ES2;
	}
	else if(hw_ver >= O3_PP1)
	{
		prox_thd = 9;
		prox_thd_offset = 1;
		capella_lux_offset_factor = CAPELLA_LUX_OFFSET_PP;
	}
	
	g_capella_chip = kzalloc(sizeof(struct capella_chip), GFP_KERNEL);
	if (!g_capella_chip)
		return -ENOMEM;

	als_poll_dev = input_allocate_polled_device();
	ps_poll_dev = input_allocate_polled_device();
	if (!als_poll_dev) {
		ret = -ENOMEM;
		goto out_region;
	}
	if (!ps_poll_dev) {
		ret = -ENOMEM;
		goto out_region;
	}

	als_poll_dev->private = g_capella_chip;
	als_poll_dev->poll = capella_als_report;
	als_poll_dev->poll_interval = CAPELLA_ALS_POLL_INTERVAL_MIN;

	als_poll_dev->input->name = CAPELLA_ALS_DEVICE_INPUT_NAME;

	ret = input_register_polled_device(als_poll_dev);
	if (ret) {
		printk(KERN_ERR "!!!input_register_polled_devicer als failed\n");
		goto exit_alloc_data_failed;
	}

	ps_poll_dev->private = g_capella_chip;
	ps_poll_dev->poll = capella_ps_report;
	ps_poll_dev->poll_interval = CAPELLA_PS_POLL_INTERVAL_MIN;

	ps_poll_dev->input->name = CAPELLA_PROX_DEVICE_INPUT_NAME;


	ret = input_register_polled_device(ps_poll_dev);
	if (ret) {
		printk(KERN_ERR "!!!input_register_polled_devicer ps failed\n");
		goto exit_alloc_data_failed;
	}

	set_bit(EV_ABS, als_poll_dev->input->evbit);
	set_bit(EV_ABS, ps_poll_dev->input->evbit);
	input_set_abs_params(als_poll_dev->input, ABS_MISC, 0, 45000, 0, 0); //0~45000
	input_set_abs_params(ps_poll_dev->input, ABS_DISTANCE, 0, 1, 0, 0); //0~1

	capellaActiveMask = 0;

	ret = platform_driver_register(&capella_drvier);
	if (ret)
		goto out_region;

	pdev = platform_device_register_simple("capella", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		goto out_driver;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	capella_e_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 40;
	capella_e_suspend.suspend = capella_early_suspend;
	capella_e_suspend.resume= capella_early_resume;
	register_early_suspend(&capella_e_suspend);
#endif

	g_capella_chip->proximity_val = 1;
	g_capella_chip->proximity_prev_val = 0xFF;
	capellaActiveMask =0;

	printk("!!!capella_init :DONE\n");
	return 0;

	exit_alloc_data_failed:
		printk("!!!capella_init : fail 1\n");
		input_free_polled_device(als_poll_dev);
		input_free_polled_device(ps_poll_dev);
		kfree(g_capella_chip);  
	out_driver:
		printk("!!! capella_init : fail 2\n");
		platform_driver_unregister(&capella_drvier);
	out_region:
		printk("!!! capella_init : fail 3\n");
		return ret;
}

static void __exit capella_exit(void)
{
	input_unregister_polled_device(als_poll_dev);
	input_unregister_polled_device(ps_poll_dev);
	input_free_polled_device(als_poll_dev);
	input_free_polled_device(ps_poll_dev);
	platform_device_unregister(pdev);
	platform_driver_unregister(&capella_drvier);
}

module_init(capella_init);
module_exit(capella_exit);

MODULE_AUTHOR("Kim Il Myung <ilmyung@xxxxxxxxxxx>");
MODULE_DESCRIPTION("CAPELLA Proximity/Ambient Light Sensor driver");
MODULE_LICENSE("GPL");
