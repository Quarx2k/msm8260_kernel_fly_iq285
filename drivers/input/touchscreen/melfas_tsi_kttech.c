/*
 * linux/drivers/input/touchscreen/melfas_tsi_kttech.c
 *
 * KT Tech S100 Platform Touch Screen Driver
 *
 * Copyright (C) 2010 KT Tech, Inc.
 * Written by Jhoonkim <xxxxxx@kttech.co.kr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 */
 
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include <linux/i2c-gpio.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/sched.h>

#include <linux/melfas_tsi_kttech.h>
#include <asm/irq.h>

#include <mach/board.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <mach/gpio.h>
#include <mach/vreg.h>
#include <asm/delay.h>
#include <mach/irqs.h>

#include <linux/err.h>
#include <linux/pmic8058-othc.h>
#include <linux/regulator/consumer.h>
#include <mach/pmic.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/wakelock.h>
#include <linux/earlysuspend.h>
#include <linux/suspend.h>
#endif

/* Debug Information */
//#define DEBUG 1
//#define DEBUG_TS_INFO 1

#ifdef DEBUG
#undef dev_dbg
#define dev_dbg(fmt, args...)		dev_info(fmt, ##args)
#endif

#ifdef DEBUG_TS_INFO
#define dev_dbg_ts(fmt...)	dev_info(fmt)
#else
#define dev_dbg_ts(fmt...)	
#endif

#define MELFAS_TOUCH_IRQ_GPIO 66
#define MELFAS_TOUCH_ENABLE_GPIO 33

/* Melfas Touchscreen I2C Protocol */
#define MELFAS_VENDOR						0x2116
#define MELFAS_X_RESOLUTION					480
#define MELFAS_Y_RESOLUTION					800

#define INPUT_TYPE_OFFSET					0
#define INPUT_TYPE_NONTOUCH					(0 << INPUT_TYPE_OFFSET)
#define INPUT_TYPE_SINGLE					(1 << INPUT_TYPE_OFFSET)
#define INPUT_TYPE_DUAL						(2 << INPUT_TYPE_OFFSET)
#define INPUT_TYPE_PALM						(3 << INPUT_TYPE_OFFSET)

#define MELFAS_I2C_RETRY_TIMES	2
#define MELFAS_I2C_NAME "melfas-tsi-ts"
#define MODE_CHANGE_REG_SIZE 2
#define MODE_CHANGE_REG 0x1

#define MELFAS_MODE_CHANGE

enum melfas_ts_burst_read_offset {
	READ_INPUT_INFO,			// 0
	READ_X_Y_POS_UPPER,			// 1
	READ_X_POS_LOWER,			// 2
	READ_Y_POS_LOWER,			// 3
	READ_X2_Y2_POS_UPPER,		// 4
	READ_X2_POS_LOWER,			// 5
	READ_Y2_POS_LOWER,			// 6
	READ_Z_POSITION,			// 7
	READ_BLOCK_SIZE,			// 8
};

/* External Function Reference. */
extern int get_hw_version(void);
extern void msm_i2c_gpio_config(int iface, int config_type);

/* Defined for use k_thread */ 
DECLARE_WAIT_QUEUE_HEAD(idle_wait);
struct task_struct *kidle_task;
unsigned int	thread_start;
unsigned int i2c_delay = 160;
static int melfas_irq;

/* Malfas Device Private Data */
struct melfas_ts_info
{
	struct input_dev      *dev;
	struct early_suspend  early_suspend;
  
	/* For Single Touch Point */
	long  xp, xp_old;
	long  yp, yp_old;
	long  zp, zp_old;	/* Pressure */
  
	/* For Multi Touch Point */
	long  xp2, xp2_old;
	long  yp2, yp2_old;  
  
	unsigned char	distance;
	unsigned int	key_val;
	unsigned char	touch_ts_info;
	unsigned char	touch_ts_key_info;
	unsigned char	touch_ts_info_tmp;
	unsigned int	pen_down;
	unsigned int	mode;
	
	int   shift;              // For count i2c resolution bit shift
	char  phys[32];           // For indicate name of touch screen devices

	unsigned char  fw_version;
	unsigned char  hw_version;

	int   (*power)(int on);
};

/* Touch Screen Private Data
 *
 * static struct melfas_priv->melfas_ts_info
 */
 
static struct melfas_priv
{
	struct delayed_work   task_work;
	struct i2c_client     *i2c_client;
	struct i2c_driver     melfas_i2c_driver;
	struct melfas_ts_info *ts;

	unsigned int interval;
} *p_melfas_priv;

static inline void *melfas_priv_get_i2c_client(const struct melfas_priv *priv)
{
	return priv->i2c_client;
}

static inline void *melfas_priv_get_melfas_ts_dev(const struct melfas_priv *priv)
{
	return priv->ts;
}

#ifdef CONFIG_HAS_WAKELOCK
extern suspend_state_t get_suspend_state(void);
#endif

	/* Burst Read Protocol data layout (16bit)
	 * |----|----|----|----|(touch_ts_info_tmp)
	 *  X,Y Upper   Info    
	 * |----|----|----|----|(i2c_1st_data)
	 *    Y Pos     X Pos
	 * |----|----|----|----|(i2c_2nd_data)
	 *   X2 Pos  X2,Y2 Upper
	 * |----|----|----|----|(i2c_3rd_data)
	 *    Z Pos     Y2 Pos
	 */

/* I2C Data Read Block
 * Usage : 	
 *  ret = melfas_i2c_read_block(ts->client, MELFAS_I2C_CMD_INPUT_INFORMATION, buffer, READ_BLOCK_SIZE);
 *	if (ret < 0) {
 *	printk(KERN_ERR"%s, err:%d\n", __func__, ret);
 *		return ret;
 *	}
 */
	
static int melfas_i2c_read_block(struct i2c_client *client, uint8_t addr, uint8_t *data, int length)
{
	int retry;
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &addr,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = data,
		}
	};

	for (retry = 0; retry <= MELFAS_I2C_RETRY_TIMES; retry++) {
		if (i2c_transfer(client->adapter, msgs, 1) == 1) {
					udelay(i2c_delay);
			if (i2c_transfer(client->adapter, &msgs[1], 1) == 1)
				break;
		}
		mdelay(10);
	}
	if (retry > MELFAS_I2C_RETRY_TIMES) {
		dev_err(&client->dev, "!!!! i2c_read_block retry over %d\n",
			MELFAS_I2C_RETRY_TIMES);
		return -EIO;
	}
	return 0;
}

/* Touch Screen Interrupt Handler */
static irqreturn_t melfas_touch_isr(int irq, void *dev_id)
{
    if(thread_start == 0)
    {
      return IRQ_HANDLED;
    }  
    
	wake_up_interruptible(&idle_wait);

	return IRQ_HANDLED;	
}

/* Kernel Thread, Touch Screen Read Data Position on Interrupt */
int thread_touch_report_position(void *kthread)
{
	unsigned int ret = 0;
	unsigned int ret_key = 0;
	unsigned char buffer[READ_BLOCK_SIZE];

	struct i2c_client *client = melfas_priv_get_i2c_client(p_melfas_priv);
	struct melfas_ts_info *ts = melfas_priv_get_melfas_ts_dev(p_melfas_priv); 	
		
	printk("!!!!!!!! Touch Report Kernel Thread Starting.\n");	
	
	/*thread start*/
	thread_start = 1;
	do {
		if ((gpio_get_value(MELFAS_TOUCH_IRQ_GPIO))) {
	 		/* Pen-Up Thread Sleep */
			interruptible_sleep_on(&idle_wait); 
		}

		ret = melfas_i2c_read_block(client, MELFAS_I2C_CMD_INPUT_INFORMATION, buffer, READ_BLOCK_SIZE);
		if (ret < 0) {
			dev_dbg_ts(KERN_ERR "%s, err:%d\n", __func__, ret);
			return ret;
 		}
    
		if(ts->mode == 0) {
            if((buffer[READ_INPUT_INFO] & 0x07) == INPUT_TYPE_DUAL)
            {
    			ret = interruptible_sleep_on_timeout(&idle_wait, (HZ/70));
    			if( ret ) {
    					dev_dbg(&client->dev, "%s : Homing routine Fatal kernel error or EXIT to run.\n", __func__);	
      			}
            }            
            else if((buffer[READ_INPUT_INFO] & 0x07) == INPUT_TYPE_SINGLE)
            {
    			ret = interruptible_sleep_on_timeout(&idle_wait, (HZ/40));
    			if( ret ) {
    					dev_dbg(&client->dev, "%s : Homing routine Fatal kernel error or EXIT to run.\n", __func__);	
      			}
            }
            else
            {
    			ret = interruptible_sleep_on_timeout(&idle_wait, (HZ/500));
    			if( ret ) {
    					dev_dbg(&client->dev, "%s : Homing routine Fatal kernel error or EXIT to run.\n", __func__);	
      			}
            }          
		}
		else if(ts->mode == 1){
            if((buffer[READ_INPUT_INFO] & 0x07) == INPUT_TYPE_NONTOUCH)
            {
    			ret = interruptible_sleep_on_timeout(&idle_wait, (HZ/500));
    			if( ret ) {
    					dev_dbg(&client->dev, "%s : Homing routine Fatal kernel error or EXIT to run.\n", __func__);	
      			}
            }
            else
            {
    			ret = interruptible_sleep_on_timeout(&idle_wait, (HZ/80)); 					
    			if( ret ) {
    					dev_dbg(&client->dev, "%s : Homing routine Fatal kernel error or EXIT to run.\n", __func__);	
    		    }
            }
		}
  		else if(ts->mode == 2){
            if((buffer[READ_INPUT_INFO] & 0x07) == INPUT_TYPE_NONTOUCH)
            {
    			ret = interruptible_sleep_on_timeout(&idle_wait, (HZ/500));
    			if( ret ) {
    					dev_dbg(&client->dev, "%s : Homing routine Fatal kernel error or EXIT to run.\n", __func__);	
      			}
            }
            else
            {              
    			ret = interruptible_sleep_on_timeout(&idle_wait, (HZ/100)); 					
    			if( ret ) {
    					dev_dbg(&client->dev, "%s : Homing routine Fatal kernel error or EXIT to run.\n", __func__);	
    		    }
            }
		}
		
		ts->touch_ts_info = buffer[READ_INPUT_INFO] & 0x07; 
		ts->touch_ts_key_info = buffer[READ_INPUT_INFO] & 0x80;
		
 		switch (buffer[READ_INPUT_INFO] & 0x07) {
			case INPUT_TYPE_NONTOUCH:
              	//printk("!!!! Touch Type = Released \n");
				/* Pen-up */
				input_report_abs(ts->dev, ABS_PRESSURE, 0);
				input_report_key(ts->dev, BTN_TOUCH, 0);
				input_report_abs(ts->dev, ABS_MT_TOUCH_MAJOR, 0);
				input_mt_sync(ts->dev);
				input_sync(ts->dev);
			break;
				
			case INPUT_TYPE_SINGLE:
				ts->xp = ((buffer[READ_X_Y_POS_UPPER] & 0xF0) << 4) | buffer[READ_X_POS_LOWER];
				ts->yp = ((buffer[READ_X_Y_POS_UPPER] & 0x0F) << 8) | buffer[READ_Y_POS_LOWER];
				ts->zp = buffer[READ_Z_POSITION]; 
			
				//printk("!!!! Touch Type = Single Point :: x =%ld, y = %ld, z = %ld \n", ts->xp,ts->yp,ts->zp);

				input_report_abs(ts->dev, ABS_X, ts->xp);
				input_report_abs(ts->dev, ABS_Y, ts->yp);
				input_report_abs(ts->dev, ABS_PRESSURE, ts->zp);
				input_report_key(ts->dev, BTN_TOUCH, 1);

				input_report_abs(ts->dev, ABS_MT_TOUCH_MAJOR, ts->zp);            
				input_report_abs(ts->dev, ABS_MT_POSITION_X, ts->xp);
				input_report_abs(ts->dev, ABS_MT_POSITION_Y, ts->yp);
				input_mt_sync(ts->dev);
				input_sync(ts->dev);	
			break;
			
			case INPUT_TYPE_DUAL:
				ts->xp 	= ((buffer[READ_X_Y_POS_UPPER] & 0xF0) << 4) | buffer[READ_X_POS_LOWER];
				ts->yp 	= ((buffer[READ_X_Y_POS_UPPER] & 0x0F) << 8) | buffer[READ_Y_POS_LOWER];
				ts->xp2 = ((buffer[READ_X2_Y2_POS_UPPER] & 0xF0) << 4) | buffer[READ_X2_POS_LOWER];
				ts->yp2 = ((buffer[READ_X2_Y2_POS_UPPER] & 0x0F) << 8) | buffer[READ_Y2_POS_LOWER];
				ts->zp	= buffer[READ_Z_POSITION];
       
				input_report_abs(ts->dev, ABS_MT_POSITION_X, ts->xp);
				input_report_abs(ts->dev, ABS_MT_POSITION_Y, ts->yp);
				input_report_abs(ts->dev, ABS_MT_TOUCH_MAJOR, ts->zp);
				input_mt_sync(ts->dev);

				input_report_abs(ts->dev, ABS_MT_POSITION_X, ts->xp2);
				input_report_abs(ts->dev, ABS_MT_POSITION_Y, ts->yp2);
				input_report_abs(ts->dev, ABS_MT_TOUCH_MAJOR, ts->zp);

				input_mt_sync(ts->dev);
				input_sync(ts->dev);	
				//printk("!!!! Touch Type = Multi \n");	
			break;
			
			case INPUT_TYPE_PALM:
				/* TODO */
			break;
			
			default:
				printk(KERN_ERR "Unknown Touchscreen Input Type 0x%x\n", buffer[READ_INPUT_INFO]);
			break;
		} 		
    
		if ((ts->touch_ts_key_info)){
 			ts->key_val = ((buffer[READ_INPUT_INFO] & 0x70) >> 4);
 			dev_dbg_ts("KEY_Pressed : %x\n", ts->key_val);
 			switch (ts->key_val) {
 				case 0x4:
					ret_key = KEY_SEARCH;
				break;
				
				case 0x3:
					ret_key = KEY_BACK;
				break;

				case 0x2:
	  			ret_key = KEY_HOME;
				break;

				case 0x1:
					ret_key = KEY_MENU ;
				break;

	 			default:
 					dev_dbg_ts(KERN_ERR "Unknown Key Input Type 0x%x\n", buffer[READ_INPUT_INFO]);
 				break;
 			}
 			input_report_key(ts->dev, ret_key, 1);
			input_sync(ts->dev);
 		}
 		else {
 			if(ret_key != 0x0) {
				input_report_key(ts->dev, ret_key, 0);
				input_sync(ts->dev);
			}
			ret_key = 0x0;
		}
		
	} while (!kthread_should_stop());
	
	return 0;
}

static int init_touch_gpio(void)
{
    int rc =0;
  
	if (gpio_request(MELFAS_TOUCH_ENABLE_GPIO, "ts_enable") < 0)
		goto gpio_failed;

	if (gpio_request(MELFAS_TOUCH_IRQ_GPIO, "ts_int") < 0)
		goto gpio_failed;

    rc = gpio_direction_output(MELFAS_TOUCH_ENABLE_GPIO, 1);
  	if (rc)
    { 
		pr_err("!!!!!!! %s: gpio = ts_enable failed (%d)\n",
				__func__, rc);
    	return rc;
   }

    rc = gpio_direction_input(MELFAS_TOUCH_IRQ_GPIO);
  	if (rc)
    { 
		pr_err("!!!!!!! %s: gpio = ts_enable failed (%d)\n",
				__func__, rc);
    	return rc;
   }

  pr_err("!!!!!!!! %s: gpio = DONE (%d)\n",
  		__func__, rc);
  return 0;

  gpio_failed:	
  	return -EINVAL;
}

static struct regulator *melfas_reg_lvs3; //1.8V pull up
static struct regulator *melfas_reg_l4; //2.80v main power

static int melfas_power(int on)
{
	int rc = 0;
  
	if(on) {
        melfas_reg_lvs3 = regulator_get(NULL, "8901_lvs3");
        melfas_reg_l4 = regulator_get(NULL, "8901_l4");

		if (IS_ERR(melfas_reg_l4) || IS_ERR(melfas_reg_lvs3)) {
    		pr_err("%s: regulator_get(l4) failed (%d)\n",
    				__func__, rc);
			return PTR_ERR(melfas_reg_l4);
		}
        //set voltage level
    	rc = regulator_set_voltage(melfas_reg_l4, 2800000, 2800000);
    	if (rc)
        { 
    		pr_err("%s: regulator_set_voltage(l4) failed (%d)\n",
    				__func__, rc);
  			regulator_put(melfas_reg_l4);
			return rc;
       }

        //enable output
    	rc = regulator_enable(melfas_reg_l4);
    	if (rc)
        { 
    		pr_err("%s: regulator_enable(l4) failed (%d)\n", __func__, rc);
  			regulator_put(melfas_reg_l4);
			return rc;
       }

    	rc = regulator_enable(melfas_reg_lvs3);
    	if (rc)
        { 
    		pr_err("%s: regulator_enable(lvs3) failed (%d)\n", __func__, rc);
  			regulator_put(melfas_reg_lvs3);
			return rc;
       }

        rc = gpio_direction_output(MELFAS_TOUCH_ENABLE_GPIO, 1);
		gpio_set_value(MELFAS_TOUCH_ENABLE_GPIO, 1);
  		gpio_free(MELFAS_TOUCH_ENABLE_GPIO);
  	}
  	else {
      
    	if (!melfas_reg_l4)
    		return rc;

    	if (!melfas_reg_lvs3)
    		return rc;

    	rc = regulator_disable(melfas_reg_l4);
    	if (rc)
        { 
    		pr_err("%s: regulator_disable(l4) failed (%d)\n",
    				__func__, rc);
          	regulator_put(melfas_reg_l4);
			return rc;
       }
    	regulator_put(melfas_reg_l4);

    	melfas_reg_l4 = NULL;

    	rc = regulator_disable(melfas_reg_lvs3);
    	if (rc)
        { 
    		pr_err("%s: regulator_disable(lvs3) failed (%d)\n",
    				__func__, rc);
          	regulator_put(melfas_reg_lvs3);
			return rc;
       }
    	regulator_put(melfas_reg_lvs3);

    	melfas_reg_lvs3 = NULL;

        rc = gpio_direction_output(MELFAS_TOUCH_ENABLE_GPIO, 0);
		gpio_set_value(MELFAS_TOUCH_ENABLE_GPIO, 0);        
		gpio_free(MELFAS_TOUCH_ENABLE_GPIO);
	}

    return 0;
}

static int __devexit melfas_i2c_remove(struct i2c_client *client)
{
	struct melfas_ts_info *ts = melfas_priv_get_melfas_ts_dev(p_melfas_priv);
  	
	unregister_early_suspend(&ts->early_suspend);
	free_irq(melfas_irq, NULL);
    
	kfree(p_melfas_priv);
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void melfas_i2c_early_suspend(struct early_suspend *h)
{
	struct melfas_ts_info *ts = melfas_priv_get_melfas_ts_dev(p_melfas_priv);
	struct melfas_ts_info *melfas_ts_info_early;

	melfas_ts_info_early = container_of(h, struct melfas_ts_info, early_suspend);

	disable_irq_nosync(melfas_irq);

	if (ts->power) {
		ts->power(false);
	}
}

static void melfas_i2c_late_resume(struct early_suspend *h)
{
	struct melfas_ts_info *ts = melfas_priv_get_melfas_ts_dev(p_melfas_priv);
	struct melfas_ts_info *melfas_ts_info_early;

	melfas_ts_info_early = container_of(h, struct melfas_ts_info, early_suspend);

	if (ts->power) {
		ts->power(true);
		mdelay(20);
	}
	enable_irq(melfas_irq);
}
#endif

#ifdef MELFAS_MODE_CHANGE
struct melfas_ts_data *ts_debug;
static int melfas_i2c_write_block(struct i2c_client *client, uint8_t *data, int length)
{
	int retry;
	struct i2c_msg msgs[] = {
		{
		 .addr = client->addr,
		 .flags = 0,
		 .len = length,
		 .buf = data,
		 },
	};
	for (retry = 0; retry <= MELFAS_I2C_RETRY_TIMES; retry++) {
		if (i2c_transfer(client->adapter, msgs, 1) > 0)
			break;
		else
			mdelay(10);
	}
	if (retry > MELFAS_I2C_RETRY_TIMES) {
		dev_err(&client->dev, "i2c_read_block retry over %d\n",
			MELFAS_I2C_RETRY_TIMES);
		return -EIO;
	}
	return 0;
}

static ssize_t melfas_vendor_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct i2c_client *client = melfas_priv_get_i2c_client(p_melfas_priv);

	u8 buffer[MODE_CHANGE_REG_SIZE-1];
	melfas_i2c_read_block(client, MODE_CHANGE_REG, buffer, MODE_CHANGE_REG_SIZE-1);

	dev_dbg_ts(KERN_ERR "MELFAS Vendor Show %02x\n",buffer[0]);
	memcpy(buf, buffer, MODE_CHANGE_REG_SIZE-1);
	ret = MODE_CHANGE_REG_SIZE-1;
	
	return ret;
}

static ssize_t melfas_vendor_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	struct i2c_client *client = melfas_priv_get_i2c_client(p_melfas_priv);
	struct melfas_ts_info *ts = melfas_priv_get_melfas_ts_dev(p_melfas_priv); 	
	
	char buffer[MODE_CHANGE_REG_SIZE];
	u8 buffer1[MODE_CHANGE_REG_SIZE-1];

	buffer[0]= MODE_CHANGE_REG;
	buffer[1] = *buf;

	ts->mode = *buf;

	//write mode
	dev_dbg_ts("*** Melfas set mode : Resolution Mode === %d \n", ts->mode);
	if(ts->mode != 2)
    	melfas_i2c_write_block(client, buffer, 2);

	udelay(50);

	//read mode
	melfas_i2c_read_block(client, MODE_CHANGE_REG, buffer1, MODE_CHANGE_REG_SIZE-1);
	dev_dbg_ts("*** Melfas read  mode: Resolution Mode === %02x\n",buffer1[0]);
    
	ret = MODE_CHANGE_REG_SIZE;

	return ret;
}
#else
static ssize_t melfas_vendor_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct i2c_client *client = melfas_priv_get_i2c_client(p_melfas_priv);
	
	sprintf(buf, "%s\n", MELFAS_I2C_NAME);
	ret = strlen(buf) + 1;
	return ret;
}
#endif/* MELFAS_MODE_CHANGE */

static DEVICE_ATTR(vendor, 0666, melfas_vendor_show, melfas_vendor_store);
static struct kobject *android_touch_kobj = NULL;

static int malfas_touch_sysfs_init(void)
{
	int ret;
	android_touch_kobj = kobject_create_and_add("android_touch", NULL);
	if (android_touch_kobj == NULL) {
		ret = -ENOMEM;
		return ret;
	}
	ret = sysfs_create_file(android_touch_kobj, &dev_attr_vendor.attr);
	if (ret) {
		kobject_del(android_touch_kobj);
		return ret;
	}
	return 0;
}

/* Touch Screen Driver Initialize */
static int __devinit melfas_i2c_probe(struct i2c_client *client, const struct i2c_device_id *i2c_id)
{
	struct melfas_ts_info *ts;
	int ret, err, rc;
	unsigned char i2c_buffer[2];

	/* init touch gpio */
	if ((rc = init_touch_gpio()) < 0) {
   		dev_err(&client->dev, "!!!!!! melfas_i2c_probe: Power setting FAIL \n");
		return rc;
	}

	if(!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
   {
		dev_err(&client->dev, "!!!!!! melfas_i2c_probe: i2c_check_functionality FAIL \n");
   }

	/* Temporary - for calculate resonposible performance */
	p_melfas_priv =  kzalloc(sizeof(struct melfas_priv), GFP_KERNEL);
	p_melfas_priv->interval = msecs_to_jiffies(30);

	ts = kzalloc(sizeof(struct melfas_ts_info), GFP_KERNEL);
	p_melfas_priv->ts = ts;

	ts->dev = input_allocate_device();
	if (!ts->dev)
	{
		dev_err(&client->dev, "Failed to allocate input device.\n");
		err = -ENOMEM;
		
		return err;
	}
	
	/* Initialize Keybits */
	set_bit(EV_SYN, ts->dev->evbit);
	set_bit(EV_KEY, ts->dev->evbit);
	set_bit(EV_ABS, ts->dev->evbit);
	set_bit(BTN_TOUCH, ts->dev->keybit);

	input_set_abs_params(ts->dev, ABS_X, 0, MELFAS_X_RESOLUTION, 0, 0);
	input_set_abs_params(ts->dev, ABS_Y, 0, MELFAS_Y_RESOLUTION, 0, 0);
	input_set_abs_params(ts->dev, ABS_PRESSURE, 0, 255, 0, 0);

	input_set_abs_params(ts->dev, ABS_MT_POSITION_X, 0, MELFAS_X_RESOLUTION, 0, 0);
	input_set_abs_params(ts->dev, ABS_MT_POSITION_Y, 0, MELFAS_Y_RESOLUTION, 0, 0);
	input_set_abs_params(ts->dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	
	set_bit(EV_KEY, ts->dev->evbit);
	set_bit(KEY_MENU, ts->dev->keybit);
	set_bit(KEY_HOME, ts->dev->keybit);
	set_bit(KEY_BACK, ts->dev->keybit);
	set_bit(KEY_SEARCH, ts->dev->keybit);

	sprintf(ts->phys, "input(ts)");

	ts->dev->name = client->name;
	ts->dev->phys = ts->phys;
	ts->dev->id.bustype = BUS_HOST;
	ts->dev->dev.parent = &client->dev;
	input_set_drvdata(ts->dev, ts);

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = melfas_i2c_early_suspend;
	ts->early_suspend.resume = melfas_i2c_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

	p_melfas_priv->i2c_client = client;

	/* Allocate Touch Screen Power */
	ts->power = melfas_power;
  
	/* Touchscreeen Power ON Request*/
	if(ts->power(true) != 0)
    { 
		dev_err(&client->dev, "*** Melfas Touch Screen Driver Power Up Failed.\n");
    	kfree(p_melfas_priv);
		return -EIO;
    }

	ret = melfas_i2c_read_block(client, MELFAS_I2C_CMD_FIRMWARE_VER, i2c_buffer, 2);
	if (ret < 0) {
		dev_err(&client->dev, "!!!!!! Melfas firmware check fail: I2C problem!\n");
    	kfree(p_melfas_priv);
		return -EIO;
	}
	ts->fw_version = i2c_buffer[0];
	ts->hw_version = i2c_buffer[1];

	printk("!!!!! S100 Touch Driver F/W Info: F/W Ver. 0x%x, H/W Rev. 0x%x\n", 
		ts->fw_version, ts->hw_version);
	
	/* Register Input device data */
	ts->dev->id.vendor = MELFAS_VENDOR;
	ts->dev->id.product = ts->hw_version;
	ts->dev->id.version = ts->fw_version;

    melfas_irq = MSM_GPIO_TO_INT(MELFAS_TOUCH_IRQ_GPIO);
	ret = request_irq(melfas_irq, melfas_touch_isr, IRQF_TRIGGER_FALLING, client->name, NULL);
	if (ret) {
		dev_err(&client->dev, "!!!!!! request_irq failed (IRQ_TOUCH)!\n");
		ret = -EIO;
	}
	
	/* All went ok, so register to the input system */
	ret = input_register_device(ts->dev);
	
	kidle_task = kthread_run(thread_touch_report_position, NULL, "ktouchd");
	
	if( IS_ERR(kidle_task) ) {
		dev_err(&client->dev, "S100 Touch Screen Driver Initialize Failed.\n");
		return -EIO;
	}

	printk("!!!!!melfas_i2c_probe: DONE\n");

	malfas_touch_sysfs_init();
	return 0;
}

/* Register I2C device access via i2c-gpio */
static const struct i2c_device_id melfas_ids[] = {
        { MELFAS_I2C_NAME, 0 },
        { },
};
MODULE_DEVICE_TABLE(i2c, melfas_ids);

static struct i2c_driver melfas_i2c_driver = {
	.probe		= melfas_i2c_probe,
	.remove		= __devexit_p(melfas_i2c_remove),
	.driver 	=
    		{
    		.name = MELFAS_I2C_NAME,
    		},
	.id_table 	= melfas_ids,
};


/* Platform Device Driver Initialize(!) */
static int __init melfas_i2c_init(void)
{
	return i2c_add_driver(&melfas_i2c_driver);
}

static void __exit melfas_i2c_exit(void)
{
	i2c_del_driver(&melfas_i2c_driver);
  	kthread_stop(kidle_task);
	return;
}

module_init(melfas_i2c_init)
module_exit(melfas_i2c_exit)

MODULE_DESCRIPTION("Melfas Touchscreen Driver");
MODULE_AUTHOR("KT tech Inc");
MODULE_VERSION("1.2");
MODULE_LICENSE("GPL");
