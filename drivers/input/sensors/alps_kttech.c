/*
 * alps.c - magnetic/compass sensor
 *
 * Copyright (C) 2010 Alps Device
 * IKUYAMA Ryo <xxxxx@xxxxxxxxxxx>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include <asm/uaccess.h>
#include <linux/input-polldev.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/alps_kttech.h>
#include <linux/capella_kttech.h>
#include <linux/bma150_kttech.h>
#include <linux/pmic8058-othc.h>
#include <linux/regulator/consumer.h>
#include <mach/pmic.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/wakelock.h>
#include <linux/earlysuspend.h>
#include <linux/suspend.h>
#endif

//#define ALPS_SENSOR_DEBUG_MSG

// sensor IDs must be a power of two and
// must match values in SensorManager.java

#define EVENT_TYPE_ACCEL_X          ABS_X
#define EVENT_TYPE_ACCEL_Y          ABS_Y
#define EVENT_TYPE_ACCEL_Z          ABS_Z
#define EVENT_TYPE_ACCEL_STATUS     ABS_WHEEL

#define EVENT_TYPE_YAW              ABS_RX
#define EVENT_TYPE_PITCH            ABS_RY
#define EVENT_TYPE_ROLL             ABS_RZ
#define EVENT_TYPE_ORIENT_STATUS    ABS_RUDDER

#define EVENT_TYPE_MAGV_X           ABS_HAT0X
#define EVENT_TYPE_MAGV_Y           ABS_HAT0Y
#define EVENT_TYPE_MAGV_Z           ABS_BRAKE

// for i2c definition

/* Register Name for accsns */
#define ACC_XOUT_H		0x00
#define ACC_XOUT_L		0x01
#define ACC_YOUT_H		0x02
#define ACC_YOUT_L		0x03
#define ACC_ZOUT_H		0x04
#define ACC_ZOUT_L		0x05
#define ACC_FF_INT		0x06
#define ACC_FF_DELAY		0x07
#define ACC_MOT_INT		0x08
#define ACC_MOT_DELAY	0x09
#define ACC_CTRL_REGC	0x0a
#define ACC_CTRL_REGB	0x0b
#define ACC_CTRL_REGA	0x0c

#define ACC_DRIVER_NAME "accsns"
#define I2C_ACC_ADDR (0x19)

#define ACC_GPIO_INT	128

/* Register Name for hscd */
#define HSCD_XOUT_H		0x11
#define HSCD_XOUT_L		0x10
#define HSCD_YOUT_H		0x13
#define HSCD_YOUT_L		0x12
#define HSCD_ZOUT_H		0x15
#define HSCD_ZOUT_L		0x14

#define HSCD_CTRL1		0x1b

#define HSCD_DRIVER_NAME "hscd"
#define I2C_HSCD_ADDR (0x0c)

#define HSCD_GPIO_INT	131

// for input device definition

#define ALPS_POLL_INTERVAL_MAX	200	/* msecs */
#define ALPS_POLL_INTERVAL_MIN	50	/* msecs */

#define ALPS_INPUT_FUZZ	4	/* input event threshold */
#define ALPS_INPUT_FLAT	4

#define ALPS_SLEEP  1
#define ALPS_ACTIVE 0

// for I2C define
#define I2C_ADAPTER_ID 5
#define I2C_RETRIES		2
#define I2C_SCL 115
#define I2C_SDA 116


static struct i2c_driver accsns_driver;
static struct i2c_client *client_accsns = NULL;

static struct i2c_driver hscd_driver;
static struct i2c_client *client_hscd = NULL;


int acc_c_x,acc_c_y,acc_c_z;
int mag_c_x,mag_c_y,mag_c_z;
int cnt_mag = 0, cnt_acc = 0;

static DEFINE_MUTEX(alps_lock);

static struct platform_device *pdev;
static struct input_polled_dev *alps_idev;
static int hw_ver;

#ifdef CONFIG_HAS_EARLYSUSPEND
struct early_suspend e_suspend;
#endif
//////////////////////////////////////////////////////////////////////////////
static int g_nSensorsActiveMask = 0;
static int power_state = 0;
static int alps_suspended = 0;
static int mag_dev_mode = 0xFF;
static int acc_dev_mode = 0xFF;

// Sensors active flag
#define		ALPS_SENSORS_ACTIVE_ACCELEROMETER				0x01
#define		ALPS_SENSORS_ACTIVE_MAGNETIC_FIELD				0x02
#define		ALPS_SENSORS_ACTIVE_ORIENTATION					0x04
///////////////////////////////////////////////////////////////////////////////
// for i2c driver (accsns)

void alps_power_reset(void);

static void accsns_hscd_i2c_reset(void)
{
#ifdef ALPS_SENSOR_DEBUG_MSG
	printk("accsns_hscd_i2c_reset\n");
#endif
	gpio_tlmm_config(GPIO_CFG(I2C_SCL, 2, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_8MA), GPIO_CFG_ENABLE);
	gpio_tlmm_config(GPIO_CFG(I2C_SDA, 2, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_8MA), GPIO_CFG_ENABLE);
}

static int accsns_i2c_read(char *rxData, int length)
{

	int retry;
	int ret;

	struct i2c_msg msgs[] = {
		{
		 .addr = client_accsns->addr,
		 .flags = 0,
		 .len = 1,
		 .buf = rxData,
		},
		{
		 .addr = client_accsns->addr,
		 .flags = I2C_M_RD,
		 .len = length,
		 .buf = rxData,
		 },
	};

	for (retry = 0; retry <= I2C_RETRIES; retry++) {
		ret = i2c_transfer(client_accsns->adapter, msgs, 2);
		if (ret > 0)
			break;
		else
		{
			msleep(10);
		}
	}

	if (retry > I2C_RETRIES) {
		printk(KERN_ERR "%s: retry over %d\n",__func__, I2C_RETRIES);
		accsns_hscd_i2c_reset();
		return ret;
	}
	else
		return 0;

}

static int accsns_i2c_write(char *txData, int length)
{
	int retry;
	int ret;

	struct i2c_msg msg[] = {
		{
		 .addr = client_accsns->addr,
		 .flags = 0,
		 .len = length,
		 .buf = txData,
		 },
	};

	for (retry = 0; retry <= I2C_RETRIES; retry++) {
		ret = i2c_transfer(client_accsns->adapter, msg, 1);
		if(ret > 0)
			break;
		else
		{
			msleep(10);
		}
	}
	if (retry > I2C_RETRIES) {
		printk(KERN_ERR "%s: retry over %d\n", __func__, I2C_RETRIES);
		accsns_hscd_i2c_reset();
		return ret;
	}
	else
		return 0;
}

int accsns_get_acceleration_data(int *rbuf)
{
	char buffer[6];
	int ret;
	int temp;
	memset(buffer, 0, 6);
	buffer[0] = X_AXIS_LSB_REG;
	ret = accsns_i2c_read(buffer, 6);
	if(ret == -ETIMEDOUT)
	{
		capella_power_reset();
		alps_power_reset();
	}
	if (ret < 0)
		return 0;

	/* The BMA150 returns 10-bit values split over 2 bytes */
	rbuf[0] = ((buffer[0] & 0xC0) >> 6) | (buffer[1] << 2);
	rbuf[1] = ((buffer[2] & 0xC0) >> 6) | (buffer[3] << 2);
	rbuf[2] = ((buffer[4] & 0xC0) >> 6) | (buffer[5] << 2);
	/* convert 10-bit signed value to 32-bit */
	if (rbuf[0] & 0x200)
		rbuf[0] = rbuf[0] - 0x400;
	if (rbuf[1] & 0x200)
		rbuf[1] = rbuf[1] - 0x400;
	if (rbuf[2] & 0x200)
		rbuf[2] = rbuf[2] - 0x400;
	/* 0-based, units are 0.5 degree C */
	temp = buffer[6] - BMA150_TEMP_OFFSET;

////// 임시 코드 offset 편차 적용

	if(hw_ver <= O3_ES1)
	{
		rbuf[0] = rbuf[0] + 5;
		rbuf[1] = rbuf[1] + 17;
		rbuf[2] = rbuf[2] + 6;
	}
	else if(hw_ver == O3_ES2)
	{
		rbuf[0] = rbuf[0];
		rbuf[1] = rbuf[1] + 3;
		rbuf[2] = rbuf[2] + 4;
	}
	else
	{
		rbuf[0] = rbuf[0] - 21;
		rbuf[1] = rbuf[1] + 21;
		rbuf[2] = rbuf[2] + 4;
	}

	//printk("BMA_TransRBuff x[%d], y[%d], z[%d] \n", rbuf[0], rbuf[1], rbuf[2]);
	rbuf[0] = ((rbuf[0] & 0x0000FFFF) | (cnt_acc << 16));
	rbuf[1] = ((rbuf[1] & 0x0000FFFF) | (cnt_acc << 16));
	rbuf[2] = ((rbuf[2] & 0x0000FFFF) | (cnt_acc << 16));

//		printk("BMA_TransRBuff x[%d], y[%d], z[%d] \n", rbuf[0], rbuf[1], rbuf[2]);
//		printk("BMA_TransRBuff cnt[0x%04X], x[0x%08X], y[0x%08X], z[0x%08X] \n", cnt_acc, rbuf[0], rbuf[1], rbuf[2]);
	cnt_acc++;
	return 0;
}

static void accsns_register_init(void)
{
	char buffer[2];
	int ret, reg_bandw;
	buffer[0] = RANGE_BWIDTH_REG;
	buffer[1] = 0;
	ret = accsns_i2c_read(buffer, 2);

#ifdef ALPS_SENSOR_DEBUG_MSG	
	printk(KERN_ERR "BMA_Init ret(%d): [0x%x][0x%x]\n", ret, buffer[0],buffer[1]);
#endif
	
	reg_bandw = buffer[0];

	buffer[0] = RANGE_BWIDTH_REG;
	buffer[1] = (reg_bandw & BMA150_REG_WID_BANDW_MASK) | BMA150_BANDW_INIT; // Bandwidth = 375Hz
	
	ret = accsns_i2c_write(buffer, 2);
	return ;
}

/* set  operation mode 0 = normal, 1 = sleep*/
static int accsns_set_mode(int mode)
{
	char buffer[2];
	int ret;

	if(acc_dev_mode == mode)
		return 0;

	if(mode == BMA_MODE_SLEEP)
	{
		buffer[1] = 0x01;
	}
	else
		buffer[1] = 0x00;
	
	buffer[0] = SMB150_CTRL_REG;
	ret = accsns_i2c_write(buffer, 2);
	msleep(14);	

	if (ret < 0)
		return -1;

	if(mode == BMA_MODE_NORMAL)
	{
		accsns_register_init();
		msleep(14);
	}

	acc_dev_mode = mode;

	return ret;
}

static int accsns_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int d[3];

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->adapter->dev, "client not i2c capable\n");
		return -ENOMEM;
	}

	client_accsns = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!client_accsns) {
		dev_err(&client->adapter->dev, "failed to allocate memory for module data\n");
		return -ENOMEM;
	}

	i2c_set_clientdata(client, client_accsns);

	client_accsns = client;

	accsns_register_init();

	accsns_get_acceleration_data(d);

	return 0;
}


static const struct i2c_device_id bma150_id[] = {
	{ ACC_DRIVER_NAME, 0 },
	{ }
};


static struct i2c_driver accsns_driver = {
	.probe = accsns_probe,
	.id_table	= bma150_id,
	.driver = {
		   .name = ACC_DRIVER_NAME,
		   },
};

int accsns_open(void)
{
	 struct i2c_board_info i2c_info;
	 struct i2c_adapter *adapter;
	
	 int rc = i2c_add_driver(&accsns_driver);
	
	 printk("accsns_open init !!!!\n");
	  
	 if (rc != 0) {
	   printk("can't add i2c driver\n");
	   rc = -ENOTSUPP;
	   return rc;
	 }
	
	  memset(&i2c_info, 0, sizeof(struct i2c_board_info));
	  i2c_info.addr = BMA150_I2C_DEVICE_N;
	  strlcpy(i2c_info.type, ACC_DRIVER_NAME , I2C_NAME_SIZE);
	
	  adapter = i2c_get_adapter(I2C_ADAPTER_ID);
	
	  if (!adapter) {
		printk("can't get i2c adapter %d\n", I2C_ADAPTER_ID);
		rc = -ENOTSUPP;
		goto probe_done;
	  }
	  client_accsns = i2c_new_device(adapter, &i2c_info);
	  client_accsns->adapter->timeout = 0;
	  client_accsns->adapter->retries = 0;
	  
	  i2c_put_adapter(adapter);
	  if (!client_accsns) {
		printk("can't add i2c device at 0x%x\n",(unsigned int)i2c_info.addr);
		rc = -ENOTSUPP;
	  }

	probe_done: 
	return rc;
}

void accsns_close(void)
{
	printk("[ACC] close\n");
	i2c_del_driver(&accsns_driver);
}


static ssize_t accsns_position_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int x,y,z;
	int xyz[3];

	if(accsns_get_acceleration_data(xyz) == 0) {
		x = xyz[0] - acc_c_x;
		y = xyz[1] - acc_c_y;
		z = xyz[2] - acc_c_z;
	} else {
		x = 0;
		y = 0;
		z = 0;
	}
	return snprintf(buf, PAGE_SIZE, "(%d %d %d)\n",x,y,z);
}

static void accsns_poll(struct input_dev *idev)
{
	int xyz[3];

   if((g_nSensorsActiveMask & ALPS_SENSORS_ACTIVE_ORIENTATION)
	||(g_nSensorsActiveMask & ALPS_SENSORS_ACTIVE_ACCELEROMETER) )
   {
      if(accsns_get_acceleration_data(xyz) == 0) {
        input_report_abs(idev, EVENT_TYPE_ACCEL_X, xyz[0]);
        input_report_abs(idev, EVENT_TYPE_ACCEL_Y, xyz[1]);
        input_report_abs(idev, EVENT_TYPE_ACCEL_Z, xyz[2]);
        input_sync(idev);
		//printk("##### [ACCEL] x:%d y:%d z:%d\n",xyz[0],xyz[1],xyz[2]);        
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
// for i2c driver (hscd)

static int hscd_i2c_read(u8 reg,u8 *buf)
{
	int err;
	int tries = 0;

	struct i2c_msg msgs[] = {
		{
			.addr = client_hscd->addr,
			.flags = 0,
			.len = 1,
			.buf = buf,
		},
		{
    		.addr = client_hscd->addr,
    		 .flags = I2C_M_RD,
    		 .len = 1,
    		 .buf = buf,
		 },
	};

	*buf = reg;
	do {
		err = i2c_transfer(client_hscd->adapter, msgs, 2);
	} while ((err != 2) && (++tries <= I2C_RETRIES));

	if (err != 2) {
		dev_err(&client_hscd->adapter->dev, "read transfer error\n");
		accsns_hscd_i2c_reset();
	} else {
		err = 0;
	}

	return err;
}


//static int hscd_i2c_write(u8 reg,u8 *buf)
static int hscd_i2c_write(u8 reg,u8 val)
{
	u8 buf[2] = {0};
	int err;
	int tries = 0;

	struct i2c_msg msgs[] = {
		{
		 .addr = client_hscd->addr,
		 .flags = 0,
		 .len = 2,
		 .buf = buf,
		 },
	};

	buf[0] = reg;
	buf[1] = val;

	do {
		err = i2c_transfer(client_hscd->adapter, msgs, 1);
	} while ((err != 1) && (++tries <= I2C_RETRIES));

	if (err != 1) {
		dev_err(&client_hscd->adapter->dev, "write transfer error\n");
		accsns_hscd_i2c_reset();
	} else {
		err = 0;
	}

	return err;
}

int hscd_get_magnetic_field_data(int *xyz)
{
	int err = -1;
	/* Data bytes from hardware x, y, z */
	u8 mag_data_h;
	u8 mag_data_l;

	err = hscd_i2c_read(HSCD_XOUT_H, &mag_data_h);
	if (err < 0) return err;
	err = hscd_i2c_read(HSCD_XOUT_L, &mag_data_l);
	if (err < 0) return err;
	xyz[0] = (int) ((mag_data_h << 8) + (mag_data_l));
	xyz[0] = xyz[0] | (cnt_mag << 16);

	err = hscd_i2c_read(HSCD_YOUT_H, &mag_data_h);
	if (err < 0) return err;
	err = hscd_i2c_read(HSCD_YOUT_L, &mag_data_l);
	if (err < 0) return err;
	xyz[1] = (int) ((mag_data_h << 8) + (mag_data_l));
	xyz[1] = xyz[1] | (cnt_mag << 16);

	err = hscd_i2c_read(HSCD_ZOUT_H, &mag_data_h);
	if (err < 0) return err;
	err = hscd_i2c_read(HSCD_ZOUT_L, &mag_data_l);
	if (err < 0) return err;
	xyz[2] = (int) ((mag_data_h << 8) + (mag_data_l));
	xyz[2] = xyz[2] | (cnt_mag << 16);

	cnt_mag++;

	return err;
}

static int hscd_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
    {
		dev_err(&client->adapter->dev, "!!!!!!!!!!!! client not i2c capable\n");
		return -ENOMEM;
	}

	client_hscd = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!client_hscd) {
		dev_err(&client->adapter->dev, "failed to allocate memory for module data\n");
		return -ENOMEM;
	}

	i2c_set_clientdata(client, client_hscd);

	client_hscd = client;

	dev_info(&client->adapter->dev, "detected HSCD magnetic field sensor\n");

	return 0;
}

static const struct i2c_device_id ALPS_id[] = {
	{ HSCD_DRIVER_NAME, 0 },
	{ }
};


static struct i2c_driver hscd_driver = {
	.probe = hscd_probe,
	.id_table	= ALPS_id,
	.driver = {
		   .name = HSCD_DRIVER_NAME,
		   },
};


int hscd_open(void)
{
	 struct i2c_board_info i2c_info;
	 struct i2c_adapter *adapter;
	
	 int rc = i2c_add_driver(&hscd_driver);
	
	 printk("hscd_open Init !!!!\n");
	  
	 if (rc != 0) {
	   printk("can't add i2c driver\n");
	   rc = -ENOTSUPP;
	   return rc;
	 }
	
	  memset(&i2c_info, 0, sizeof(struct i2c_board_info));
	  i2c_info.addr = I2C_HSCD_ADDR;
	  strlcpy(i2c_info.type, HSCD_DRIVER_NAME , I2C_NAME_SIZE);
	
	  adapter = i2c_get_adapter(I2C_ADAPTER_ID);
	
	  if (!adapter) {
		printk("can't get i2c adapter %d\n", 8);
		rc = -ENOTSUPP;
		goto probe_done;
	  }
	  client_hscd = i2c_new_device(adapter, &i2c_info);
	  client_hscd->adapter->timeout = 0;
	  client_hscd->adapter->retries = 0;
	  
	  i2c_put_adapter(adapter);
	  if (!client_hscd) {
		printk("can't add i2c device at 0x%x\n",(unsigned int)i2c_info.addr);
		rc = -ENOTSUPP;
	  
	  }

	probe_done: 
	return rc;
}

void hscd_close(void)
{
	printk("[HSCD] close\n");
	i2c_del_driver(&hscd_driver);
}

static ssize_t hscd_position_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int x,y,z;
	int xyz[3];
	int ret;

	ret = hscd_get_magnetic_field_data(xyz);
	if(ret == 0) {
		x = xyz[0] - mag_c_x;
		y = xyz[1] - mag_c_y;
		z = xyz[2] - mag_c_z;
	} else {
		if(ret == -ETIMEDOUT)
		{
			capella_power_reset();
			alps_power_reset();
		}
		x = 0;
		y = 0;
		z = 0;
	}
	return snprintf(buf, PAGE_SIZE, "(%d %d %d)\n",x,y,z);
}

static ssize_t hscd_calibrate_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "(%d %d %d)\n",mag_c_x,mag_c_y,mag_c_z);
}

static ssize_t hscd_calibrate_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int xyz[3];

	if(hscd_get_magnetic_field_data(xyz) == 0) {
		mag_c_x = xyz[0];
		mag_c_y = xyz[1];
		mag_c_z = xyz[2];
	} else {
		mag_c_x = 0;
		mag_c_y = 0;
		mag_c_z = 0;
	}
	return count;
}

static void hscd_poll(struct input_dev *idev)
{
	int xyz[3];

 if((g_nSensorsActiveMask & ALPS_SENSORS_ACTIVE_ORIENTATION)
 ||(g_nSensorsActiveMask & ALPS_SENSORS_ACTIVE_MAGNETIC_FIELD) )
 {
	if(hscd_get_magnetic_field_data(xyz) == 0) {
		input_report_abs(idev, EVENT_TYPE_MAGV_X, xyz[0]);
		input_report_abs(idev, EVENT_TYPE_MAGV_Y, xyz[1]);
		input_report_abs(idev, EVENT_TYPE_MAGV_Z, xyz[2]);
		input_sync(idev);
		//printk("#### [MAGNETIC] x:%d y:%d z:%d\n",xyz[0],xyz[1],xyz[2]);
	}
  }
 
}

/* set  operation mode 0 = normal, 1 = sleep*/
static int hscd_set_mode(int mode)
{
	int ret;

	if(mag_dev_mode == mode)
		return 0;

	if(mode == ALPS_SLEEP)
	{
		ret =  hscd_i2c_write(HSCD_CTRL1, 0x00);				// set CTRL1.PC1 (to sleep mode)
	}
	else //active
	{ 
		ret =  hscd_i2c_write(HSCD_CTRL1, 0x80);				// set CTRL1.PC1 (to active mode)
	}

	if (ret < 0)
		return -1;

	mag_dev_mode = mode;

	return ret;
}

static struct regulator *alps_8058_lvs0; //1.8V pull up
static struct regulator *alps_8901_l3; //3.0v main power

static int alps_power_control(int on)
{
	int rc = 0;
    
	if(on) {

        if(power_state)
        {
    		pr_err("%s: already power on\n",
    				__func__);
            return 0;
        }  
        
        alps_8058_lvs0 = regulator_get(NULL, "8058_lvs0");
        alps_8901_l3 = regulator_get(NULL, "8901_l3");

		if (IS_ERR(alps_8901_l3) || IS_ERR(alps_8058_lvs0)) {
    		pr_err("%s: regulator_get(8901_l3) failed (%d)\n",
    				__func__, rc);
			return PTR_ERR(alps_8901_l3);
		}
        //set voltage level
    	rc = regulator_set_voltage(alps_8901_l3, 3000000, 3000000);
    	if (rc)
        { 
    		pr_err("%s: regulator_set_voltage(8901_l3) failed (%d)\n",
    				__func__, rc);
  			regulator_put(alps_8901_l3);
			return rc;
       }

        //enable output
    	rc = regulator_enable(alps_8901_l3);
    	if (rc)
        { 
    		pr_err("%s: regulator_enable(8901_l3) failed (%d)\n", __func__, rc);
  			regulator_put(alps_8901_l3);
			return rc;
       }

    	rc = regulator_enable(alps_8058_lvs0);
    	if (rc)
        { 
    		pr_err("%s: regulator_enable(lvs0) failed (%d)\n", __func__, rc);
  			regulator_put(alps_8058_lvs0);
			return rc;
       }
      power_state = 1;
  	}
  	else {
		if (alps_8901_l3)
		{
			rc = regulator_force_disable(alps_8901_l3);

			if(rc)
			{
				pr_err("%s: regulator_disable(8901_l3) failed (%d)\n",
						__func__, rc);
				regulator_put(alps_8901_l3);
				return rc;
			}
			regulator_put(alps_8901_l3);
			alps_8901_l3 = NULL;
		}

		if (alps_8058_lvs0)
		{
			rc = regulator_force_disable(alps_8058_lvs0);

			if(rc)
			{
				pr_err("%s: regulator_disable(8901_l3) failed (%d)\n",
						__func__, rc);
				regulator_put(alps_8058_lvs0);
				return rc;
			}

			regulator_put(alps_8058_lvs0);
			alps_8058_lvs0 = NULL;
		}
		power_state = 0;		
	}

    return 0;
}

void alps_power_reset(void)
{
	printk(KERN_ERR "alps power reset !!!\n");

	acc_dev_mode = 0xFF;
	mag_dev_mode = 0xFF;
	if((g_nSensorsActiveMask & ALPS_SENSORS_ACTIVE_MAGNETIC_FIELD) 
		|| (g_nSensorsActiveMask & ALPS_SENSORS_ACTIVE_ORIENTATION)) {
		hscd_set_mode(ALPS_ACTIVE);
	}
	else
		hscd_set_mode(ALPS_SLEEP);

	if((g_nSensorsActiveMask & ALPS_SENSORS_ACTIVE_ACCELEROMETER)
		|| (g_nSensorsActiveMask & ALPS_SENSORS_ACTIVE_ORIENTATION)) {
		accsns_set_mode(BMA_MODE_NORMAL);
	}
	else
		accsns_set_mode(BMA_MODE_SLEEP);
}

EXPORT_SYMBOL(alps_power_reset);

///////////////////////////////////////////////////////////////////////////////
// for input device

static ssize_t alps_position_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	size_t cnt = 0;
	mutex_lock(&alps_lock);
	cnt += accsns_position_show(dev,attr,buf);
	cnt += hscd_position_show(dev,attr,buf);
	mutex_unlock(&alps_lock);
	return cnt;
}

static ssize_t alps_calibrate_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	size_t cnt = 0;
	cnt += hscd_calibrate_show(dev,attr,buf);
	return cnt;
}

static ssize_t alps_calibrate_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	mutex_lock(&alps_lock);
	hscd_calibrate_store(dev,attr,buf,count);
	mutex_unlock(&alps_lock);
	return count;
}

static int ALPSIOCTL(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
    
	void __user *argp = (void __user *)arg;
	int nRet=0;
	short delay;
    int is_power;

	switch( cmd )
	{
		case ALPS_IOCTL_ACC_ENABLE :
#ifdef ALPS_SENSOR_DEBUG_MSG			
			printk("ALPS: ALPS_IOCTL_ACC_ENABLE\n");
#endif
			accsns_set_mode(BMA_MODE_NORMAL);
			g_nSensorsActiveMask |= ALPS_SENSORS_ACTIVE_ACCELEROMETER;
			break;

		case ALPS_IOCTL_ACC_DISABLE :
#ifdef ALPS_SENSOR_DEBUG_MSG			
			printk("ALPS: ALPS_IOCTL_ACC_DISABLE\n");
#endif
			if(!(g_nSensorsActiveMask & ALPS_SENSORS_ACTIVE_ORIENTATION))
			{
				accsns_set_mode(BMA_MODE_SLEEP);    
			}
			g_nSensorsActiveMask &=~ALPS_SENSORS_ACTIVE_ACCELEROMETER;
			break;

		case ALPS_IOCTL_MAG_ENABLE :
#ifdef ALPS_SENSOR_DEBUG_MSG			
			printk("ALPS: ALPS_IOCTL_MAG_ENABLE\n");
#endif
			hscd_set_mode(ALPS_ACTIVE);
			g_nSensorsActiveMask |= ALPS_SENSORS_ACTIVE_MAGNETIC_FIELD;
			break;

		case ALPS_IOCTL_MAG_DISABLE :
#ifdef ALPS_SENSOR_DEBUG_MSG			
			printk("ALPS: ALPS_IOCTL_MAG_DISABLE\n");
#endif
			if(!(g_nSensorsActiveMask & ALPS_SENSORS_ACTIVE_ORIENTATION))
			{
				hscd_set_mode(ALPS_SLEEP);
			}
			g_nSensorsActiveMask &= ~ALPS_SENSORS_ACTIVE_MAGNETIC_FIELD;
			break;

		case ALPS_IOCTL_COMPASS_ENABLE :
#ifdef ALPS_SENSOR_DEBUG_MSG			
			printk("ALPS: ALPS_IOCTL_COMPASS_ENABLE\n");
#endif
			accsns_set_mode(BMA_MODE_NORMAL);
			hscd_set_mode(ALPS_ACTIVE);
			g_nSensorsActiveMask |= ALPS_SENSORS_ACTIVE_ORIENTATION;
			break;

		case ALPS_IOCTL_COMPASS_DISABLE :
#ifdef ALPS_SENSOR_DEBUG_MSG			
			printk("ALPS: ALPS_IOCTL_COMPASS_DISABLE\n");
#endif
			if(!(g_nSensorsActiveMask & ALPS_SENSORS_ACTIVE_ACCELEROMETER))
			{
				accsns_set_mode(BMA_MODE_SLEEP);
			}
			if(!(g_nSensorsActiveMask & ALPS_SENSORS_ACTIVE_MAGNETIC_FIELD))
			{
				hscd_set_mode(ALPS_SLEEP);
			}
			g_nSensorsActiveMask &= ~ALPS_SENSORS_ACTIVE_ORIENTATION;
			break;

		case ALPS_IOCTL_SENSOR_SET_DELAY:
			if( copy_from_user(&delay, argp, sizeof(delay)) != 0 )
			{
				nRet = -EFAULT;
			}

			alps_idev->poll_interval = (unsigned int)delay;

			if(alps_idev->poll_interval  > ALPS_POLL_INTERVAL_MAX)
				alps_idev->poll_interval  = ALPS_POLL_INTERVAL_MAX;

			if(alps_idev->poll_interval  < ALPS_POLL_INTERVAL_MIN)
				alps_idev->poll_interval  = ALPS_POLL_INTERVAL_MIN;

			break;

		case ALPS_IOCTL_IS_POWER:
			is_power = g_nSensorsActiveMask;
			if( copy_to_user(argp, &is_power, sizeof(is_power)) != 0 )
			return -EFAULT;

			break;

	}

	return nRet;
}

static DEVICE_ATTR(position, 0444, alps_position_show, NULL);
static DEVICE_ATTR(calibrate, 0644, alps_calibrate_show,alps_calibrate_store);

static struct attribute *alps_attributes[] = {
	&dev_attr_position.attr,
	&dev_attr_calibrate.attr,
	NULL,
};

static struct attribute_group alps_attribute_group = {
	.attrs = alps_attributes,
};

static struct	file_operations alps_fops =
{
	.owner = THIS_MODULE,
	.ioctl = ALPSIOCTL,
};

static struct miscdevice alps_misc =
{
	.minor = MISC_DYNAMIC_MINOR,
	.name = "alps_misc",
	.fops = &alps_fops,
};

static int alps_probe(struct platform_device *dev)
{
	int nError = 0;

	if(accsns_open()) return -1;
	if(hscd_open()) return -1;

	// register control file
	nError = misc_register(&alps_misc);
	if( nError )
	{
		printk(KERN_ERR "alps_probe : Failed to register misc \n");
	}

	printk(KERN_INFO "alps: device successfully initialized.\n");

	hscd_set_mode(ALPS_SLEEP);
	accsns_set_mode(BMA_MODE_SLEEP);
	return 0;
}

static int alps_remove(struct platform_device *dev)
{
	accsns_close();
	hscd_close();

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void alps_early_suspend(struct early_suspend *h)
{
	hscd_set_mode(ALPS_SLEEP);
	accsns_set_mode(BMA_MODE_SLEEP);
	alps_suspended = 1;
}

static void alps_early_resume(struct early_suspend *h)
{
    accsns_hscd_i2c_reset();

	if((g_nSensorsActiveMask & ALPS_SENSORS_ACTIVE_MAGNETIC_FIELD) 
		|| (g_nSensorsActiveMask & ALPS_SENSORS_ACTIVE_ORIENTATION)) {
		hscd_set_mode(ALPS_ACTIVE);
	}
	else
		hscd_set_mode(ALPS_SLEEP);

	if((g_nSensorsActiveMask & ALPS_SENSORS_ACTIVE_ACCELEROMETER)
		|| (g_nSensorsActiveMask & ALPS_SENSORS_ACTIVE_ORIENTATION)) {
		accsns_set_mode(BMA_MODE_NORMAL);
	}
	else
		accsns_set_mode(BMA_MODE_SLEEP);

	alps_suspended = 0;
}
#endif

static struct platform_driver alps_driver = {
	.probe = alps_probe,
	.remove = alps_remove,
	.driver	= {
		.name = "alps",
		.owner = THIS_MODULE,
	},
};

static void alps_poll(struct input_polled_dev *dev)
{
	struct input_dev *idev = dev->input;

	if(power_state == 0 || alps_suspended == 1)
		return;

	mutex_lock(&alps_lock);
	accsns_poll(idev);
	hscd_poll(idev);
	mutex_unlock(&alps_lock);
}

static int __init alps_init(void)
{
	struct input_dev *idev;
	int ret;

	if (gpio_tlmm_config(GPIO_CFG(HSCD_GPIO_INT, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA), GPIO_CFG_ENABLE)) {
		printk(KERN_ERR "%s: Err: Config HSCD_GPIO_INT\n", __func__);
	}

	if (gpio_tlmm_config(GPIO_CFG(ACC_GPIO_INT, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA), GPIO_CFG_ENABLE)) {
		printk(KERN_ERR "%s: Err: Config ACC_GPIO_INT\n", __func__);
	}

    alps_power_control(1);

	hw_ver = get_kttech_hw_version();
	

	ret = platform_driver_register(&alps_driver);
	if (ret)
		goto out_region;
	printk(KERN_INFO "alps-init: platform_driver_register\n");

	pdev = platform_device_register_simple("alps", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		goto out_driver;
	}
	printk(KERN_INFO "alps-init: platform_device_register_simple\n");

	ret = sysfs_create_group(&pdev->dev.kobj, &alps_attribute_group);
	if (ret)
		goto out_device;
	printk(KERN_INFO "alps-init: sysfs_create_group\n");

	alps_idev = input_allocate_polled_device();
	if (!alps_idev) {
		ret = -ENOMEM;
		goto out_group;
	}
	printk(KERN_INFO "alps-init: input_allocate_polled_device\n");

	alps_idev->poll = alps_poll;
	alps_idev->poll_interval = ALPS_POLL_INTERVAL_MAX;

	/* initialize the input class */
	idev = alps_idev->input;
	idev->name = "alps";
	idev->phys = "alps/input0";
	idev->id.bustype = BUS_HOST;
	idev->dev.parent = &pdev->dev;
	idev->evbit[0] = BIT_MASK(EV_ABS);

	input_set_abs_params(idev, EVENT_TYPE_ACCEL_X, -256, 256, ALPS_INPUT_FUZZ, ALPS_INPUT_FLAT);
	input_set_abs_params(idev, EVENT_TYPE_ACCEL_Y, -256, 256, ALPS_INPUT_FUZZ, ALPS_INPUT_FLAT);
	input_set_abs_params(idev, EVENT_TYPE_ACCEL_Z, -256, 256, ALPS_INPUT_FUZZ, ALPS_INPUT_FLAT);

	input_set_abs_params(idev, EVENT_TYPE_MAGV_X, -256, 256, ALPS_INPUT_FUZZ, ALPS_INPUT_FLAT);
	input_set_abs_params(idev, EVENT_TYPE_MAGV_Y, -256, 256, ALPS_INPUT_FUZZ, ALPS_INPUT_FLAT);
	input_set_abs_params(idev, EVENT_TYPE_MAGV_Z, -256, 256, ALPS_INPUT_FUZZ, ALPS_INPUT_FLAT);

	ret = input_register_polled_device(alps_idev);
	if (ret)
		goto out_idev;
	printk(KERN_INFO "alps-init: input_register_polled_device\n");

#ifdef CONFIG_HAS_EARLYSUSPEND
	e_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 20;
   	e_suspend.suspend = alps_early_suspend;
   	e_suspend.resume= alps_early_resume;
	register_early_suspend(&e_suspend);
#endif

	g_nSensorsActiveMask = 0;
    
	return 0;

out_idev:
	input_free_polled_device(alps_idev);
	printk(KERN_INFO "alps-init: input_free_polled_device\n");
out_group:
	sysfs_remove_group(&pdev->dev.kobj, &alps_attribute_group);
	printk(KERN_INFO "alps-init: sysfs_remove_group\n");
out_device:
	platform_device_unregister(pdev);
	printk(KERN_INFO "alps-init: platform_device_unregister\n");
out_driver:
	platform_driver_unregister(&alps_driver);
	printk(KERN_INFO "alps-init: platform_driver_unregister\n");
out_region:
	return ret;
}

static void __exit alps_exit(void)
{
	input_unregister_polled_device(alps_idev);
	printk(KERN_INFO "alps-exit: input_unregister_polled_device\n");
	input_free_polled_device(alps_idev);
	printk(KERN_INFO "alps-exit: input_free_polled_device\n");
	sysfs_remove_group(&pdev->dev.kobj, &alps_attribute_group);
	printk(KERN_INFO "alps-exit: sysfs_remove_group\n");
	platform_device_unregister(pdev);
	printk(KERN_INFO "alps-exit: platform_device_unregister\n");
	platform_driver_unregister(&alps_driver);
	printk(KERN_INFO "alps-exit: platform_driver_unregister\n");
}

module_init(alps_init);
module_exit(alps_exit);

MODULE_AUTHOR("IKUYAMA Ryo");
MODULE_DESCRIPTION("Alps Device");
MODULE_LICENSE("GPL v2");
