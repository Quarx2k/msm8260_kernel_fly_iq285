/*
 * drivers/i2c/chips/bma150.c - bma150 G-sensor driver
 *
 *  Copyright (C) 2008 viral wang <viralwang@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/input.h>
#include <linux/bma150_kttech.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include<linux/earlysuspend.h>

#include <linux/board_kttech.h>


static struct i2c_client *bma150_client;

struct bma150_data {
	struct input_dev *input_dev;
	struct work_struct work;
	struct early_suspend early_suspend;
};

static struct bma150_data *g_pbma150_data;

static int BMA_I2C_RxData(char *rxData, int length)
{
	int retry;
	struct i2c_msg msgs[] = {
		{
		 .addr = bma150_client->addr,
		 .flags = 0,
		 .len = 1,
		 .buf = rxData,
		},
		{
		 .addr = bma150_client->addr,
		 .flags = I2C_M_RD,
		 .len = length,
		 .buf = rxData,
		 },
	};

	for (retry = 0; retry <= 100; retry++) {
		i2c_transfer(bma150_client->adapter, msgs, 1);
		mdelay(1);
		if (i2c_transfer(bma150_client->adapter, &msgs[1], 1) > 0)
			break;
		else
			mdelay(10);
	}
	if (retry > 100) {
		printk(KERN_ERR "%s: retry over 100\n",__func__);
		return -EIO;
	}	else
	return 0;


}

static int BMA_I2C_TxData(char *txData, int length)
{
	int retry;
	struct i2c_msg msg[] = {
		{
		 .addr = bma150_client->addr,
		 .flags = 0,
		 .len = length,
		 .buf = txData,
		 },
	};

	for (retry = 0; retry <= 100; retry++) {
		if(i2c_transfer(bma150_client->adapter, msg, 1) > 0)
			break;
		else
			mdelay(10);
	}
	if (retry > 100) {
		printk(KERN_ERR "%s: retry over 100\n", __func__);
		return -EIO;
	}	else
	return 0;
}
static int BMA_Init(void)
{
	char buffer[3];
	int ret, reg_bandw, reg_15;

	// read chip ID
	buffer[0] = CHIP_ID_REG;
	buffer[2] = 0;
	ret = BMA_I2C_RxData(buffer, 2);
	printk(KERN_ERR "BMA_Init ret(%d): CID[0x%x] Ver[0x%x]\n", ret, buffer[0],buffer[1]);
	if (ret < 0)
		return -1;
 	mdelay(14);

    // read reg_15
	buffer[0] = RANGE_BWIDTH_REG;
	buffer[2] = 0;
	ret = BMA_I2C_RxData(buffer, 2);
	printk(KERN_ERR "BMA_Init Reg15[0x%x][0x%x]\n", buffer[0],buffer[1]);
    mdelay(14);
	
	reg_bandw = buffer[0];
	reg_15    = buffer[1];
	buffer[0] = SMB150_CONF2_REG;
	buffer[1] = reg_15 | BMA150_REG_C15_EN_ADV_INT |		BMA150_REG_C15_LATCH_INT;
	ret = BMA_I2C_TxData(buffer, 2);
	if (ret < 0)
		return -1;
    mdelay(14);	

	buffer[0] = SMB150_CONF1_REG;
	buffer[1] = BMA150_REG_C0B_ANY_MOTION |		BMA150_REG_C0B_ENABLE_HG  |		BMA150_REG_C0B_ENABLE_LG;
	ret = BMA_I2C_TxData(buffer, 2);
	if (ret < 0)
		return -1;
    mdelay(14);	

	buffer[0] = MOTION_THRS_REG;
	buffer[1] = BMA150_ANY_MOTION_INIT;
	ret = BMA_I2C_TxData(buffer, 2);
	if (ret < 0)
		return -1;
    mdelay(14);	

	buffer[0] = RANGE_BWIDTH_REG;
	buffer[1] = (reg_bandw & BMA150_REG_WID_BANDW_MASK) |		BMA150_BANDW_INIT;
	ret = BMA_I2C_TxData(buffer, 2);
	if (ret < 0)
		return -1;


	
//	buffer[3] = buffer[1]| BMA150_REG_C15_EN_ADV_INT |	BMA150_REG_C15_LATCH_INT;;
//	buffer[2] = SMB150_CONF2_REG;
	
//	buffer[1] = (buffer[0]&0xe0);
//	buffer[0] = RANGE_BWIDTH_REG;
	return 0;

}

static int BMA_TransRBuff(int *rbuf)
{
	char buffer[7];
	int ret;
	int temp;
	memset(buffer, 0, 7);
	buffer[0] = X_AXIS_LSB_REG;
	ret = BMA_I2C_RxData(buffer, 7);
	if (ret < 0)
		return 0;
/*
	rbuf[0] = buffer[1]<<2|buffer[0]>>6;
	if (rbuf[0]&0x200)
		rbuf[0] -= 1<<10;
	rbuf[1] = buffer[3]<<2|buffer[2]>>6;
	if (rbuf[1]&0x200)
		rbuf[1] -= 1<<10;
	rbuf[2] = buffer[5]<<2|buffer[4]>>6;
	if (rbuf[2]&0x200)
		rbuf[2] -= 1<<10;
*/		
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
	rbuf[0] = rbuf[0] - 15;
	rbuf[1] = rbuf[1] + 25;
	rbuf[2] = rbuf[2] + 10;

	//printk("BMA_TransRBuff x[%d], y[%d], z[%d] temp[%d]\n", rbuf[0], rbuf[1], rbuf[2], temp);
	return 1;
}
/*
static int BMA_set_range(char range)
{
	char buffer[2];
	int ret;
	buffer[0] = RANGE_BWIDTH_REG;
	ret = BMA_I2C_RxData(buffer, 1);
	if (ret < 0)
		return -1;
	buffer[1] = (buffer[0]&0xe7)|range<<3;
	buffer[0] = RANGE_BWIDTH_REG;
	ret = BMA_I2C_TxData(buffer, 2);

	return ret;
}
*/
/*
static int BMA_get_range(void)
{
	char buffer;
	int ret;
	buffer = RANGE_BWIDTH_REG;
	ret = BMA_I2C_RxData(&buffer, 1);
	if (ret < 0)
		return -1;
	buffer = (buffer&0x18)>>3;
	return buffer;
}
*/
/*
static int BMA_reset_int(void)
{
	char buffer[2];
	int ret;
	buffer[0] = SMB150_CTRL_REG;
	ret = BMA_I2C_RxData(buffer, 1);
	if (ret < 0)
		return -1;
	buffer[1] = (buffer[0]&0xbf)|0x40;
	buffer[0] = SMB150_CTRL_REG;
	ret = BMA_I2C_TxData(buffer, 2);

	return ret;
}
*/
/* set  operation mode 0 = normal, 1 = sleep*/
static int BMA_set_mode(char mode)
{
	char buffer[2];
	int ret;

//	printk(KERN_ERR "    BMA_set_mode[%d]: \n", mode);

	buffer[0] = SMB150_CTRL_REG;
	ret = BMA_I2C_RxData(buffer, 1);
	if (ret < 0)
		return -1;
    mdelay(14);
	
	buffer[1] = (buffer[0]&0xfe)|mode;
	buffer[0] = SMB150_CTRL_REG;
	ret = BMA_I2C_TxData(buffer, 2);
    mdelay(14);	

	if(mode == BMA_MODE_NORMAL)
	{
		BMA_Init();
	    mdelay(14);
	}
	return ret;
}

static int BMA_GET_INT(void)
{
	int ret;
	ret = gpio_get_value(26);
	return ret;
}

static int bma_open(struct inode *inode, struct file *file)
{
    printk(KERN_ERR "    bma_open: \n");
	BMA_set_mode(BMA_MODE_NORMAL);
	return nonseekable_open(inode, file);
}

static int bma_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int bma_ioctl(struct inode *inode, struct file *file, unsigned int cmd,  unsigned long arg)
{

	void __user *argp = (void __user *)arg;

	char rwbuf[8];
	int R_buf[3];
	int ret = -1;
	short temp;


	switch (cmd) {
	case BMA_IOCTL_INIT:
		ret = BMA_Init();
		if (ret < 0)
			return ret;
		break;

	case BMA_IOCTL_ENABLE:
		BMA_set_mode(BMA_MODE_NORMAL);	
		break;

	case BMA_IOCTL_DISABLE:
        BMA_set_mode(BMA_MODE_SLEEP);				
			break;
		
	case BMA_IOCTL_READ:
		if (copy_from_user(&rwbuf, argp, sizeof(rwbuf)))
			return -EFAULT;
		
		if (rwbuf[0] < 1)
			return -EINVAL;
		ret = BMA_I2C_RxData(&rwbuf[1], rwbuf[0]);
		if (ret < 0)
			return ret;

		if (copy_to_user(argp, &rwbuf, sizeof(rwbuf)))
			return -EFAULT;
		
		break;
	case BMA_IOCTL_WRITE:
		if (copy_from_user(&rwbuf, argp, sizeof(rwbuf)))
			return -EFAULT;
		
		if (rwbuf[0] < 2)
			return -EINVAL;
		ret = BMA_I2C_TxData(&rwbuf[1], rwbuf[0]);
		if (ret < 0)
			return ret;
		break;
	case BMA_IOCTL_READ_ACCELERATION:
		ret = BMA_TransRBuff(&R_buf[0]);
		if (ret < 0)
			return ret;
		if (copy_to_user(argp, &R_buf, sizeof(R_buf)))
			return -EFAULT;
		
		break;
	case BMA_IOCTL_SET_MODE:
		if (copy_from_user(&rwbuf, argp, sizeof(rwbuf)))
			return -EFAULT;
		
		BMA_set_mode(rwbuf[0]);
		break;
	case BMA_IOCTL_GET_INT:
		temp = BMA_GET_INT();
		if (copy_to_user(argp, &temp, sizeof(temp)))
			return -EFAULT;
		
		break;
	default:
		return -ENOTTY;
	}


	return 0;
}

static void bma150_early_suspend(struct early_suspend *handler)
{
  BMA_set_mode(1);
}

static int bma150_suspend(struct i2c_client *client, pm_message_t mesg)
{
  return 0;

}

static int bma150_resume(struct i2c_client *client)
{
  return 0;

}

static void bma150_early_resume(struct early_suspend *handler)
{
  BMA_set_mode(0);
}

static struct file_operations bma_fops = {
	.owner = THIS_MODULE,
	.open = bma_open,
	.release = bma_release,
	.ioctl = bma_ioctl,
};

static struct miscdevice bma_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = BMA150_DEVICE_FILE_NAME,
	.fops = &bma_fops,
};


int bma150_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int err = 0;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	g_pbma150_data = kzalloc(sizeof(struct bma150_data), GFP_KERNEL);
	if (!g_pbma150_data) {
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

	i2c_set_clientdata(client, g_pbma150_data);

	bma150_client = client;


	err = misc_register(&bma_device);
	if (err) {
		printk(KERN_ERR "bma150_probe: device register failed\n");
		goto exit_misc_device_register_failed;
	}

	g_pbma150_data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	g_pbma150_data->early_suspend.suspend = bma150_early_suspend;
	g_pbma150_data->early_suspend.resume = bma150_early_resume;
	register_early_suspend(&g_pbma150_data->early_suspend);

	BMA_set_mode(BMA_MODE_NORMAL);

	return 0;

exit_misc_device_register_failed:
	kfree(g_pbma150_data);
exit_alloc_data_failed:
exit_check_functionality_failed:
	return err;
}

static const struct i2c_device_id bma150_id[] = {
	{ BMA150_I2C_NAME, 0 },
	{ }
};

static struct i2c_driver bma150_driver = {
	.probe = bma150_probe,
	.suspend = bma150_suspend,
	.resume = bma150_resume,
		
	.id_table	= bma150_id,
	.driver = {
		   .name = BMA150_I2C_NAME,
		   },
};

static int __init bma150_init(void)
{

 struct i2c_board_info i2c_info;
 struct i2c_adapter *adapter;

 int rc = i2c_add_driver(&bma150_driver);

 if (rc != 0) {
   printk("can't add i2c driver\n");
   rc = -ENOTSUPP;
   return rc;
 }

  memset(&i2c_info, 0, sizeof(struct i2c_board_info));
  i2c_info.addr = BMA150_I2C_DEVICE_N;
  strlcpy(i2c_info.type, BMA150_I2C_NAME , I2C_NAME_SIZE);

  adapter = i2c_get_adapter(SW_I2C_FIRST_BUS_ID);

  if (!adapter) {
    printk("can't get i2c adapter %d\n", 4);
    rc = -ENOTSUPP;
    goto probe_done;
  }

   bma150_client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
   if (!bma150_client) {
   	dev_err(&bma150_client->adapter->dev, "failed to allocate memory for module data\n");
   	return -ENOMEM;
   }
  
  bma150_client = i2c_new_device(adapter, &i2c_info);
  bma150_client->adapter->timeout = 0;
  bma150_client->adapter->retries = 0;
  
  i2c_put_adapter(adapter);
  if (!bma150_client) {
    printk("can't add i2c device at 0x%x\n",(unsigned int)i2c_info.addr);
    rc = -ENOTSUPP;
  
  }

probe_done:	
	return rc ;
}

static void __exit bma150_exit(void)
{
	i2c_del_driver(&bma150_driver);
}

module_init(bma150_init);
module_exit(bma150_exit);

MODULE_AUTHOR("viral wang <viral_wang@htc.com>");
MODULE_DESCRIPTION("BMA150 G-sensor driver");
MODULE_LICENSE("GPL");

