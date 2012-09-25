/* Copyright (c) 2011, KTTech. All rights reserved.
 *
 * YDA165 Sound Amp Driver
 */

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <mach/qdsp6v2/snddev_yda165.h>

static AMP_PATH_TYPE_E m_curr_path = -1;
struct snddev_yda165 yda165_modules;

#define YDA165_RESET_REG		0x80
#define YDA165_RESET_VALUE		0x80
#define YDA165_SPK_SW_GPIO		105

REG_MEMORY yda165_register_type amp_headset_stereo_path[REG_COUNT] = {
	{ 0x80 , 0x01 },
	{ 0x81 , 0x58 },
	{ 0x82 , 0xDD },
	{ 0x83 , 0x26 },
	{ 0x84 , 0x02 },
	{ 0x85 , 0x00 },
	{ 0x86 , 0x13 },
	{ 0x87 , 0x02 },  // 12 -> 22
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY yda165_register_type amp_speaker_stereo_path[REG_COUNT] = {
	{ 0x80 , 0x01 },
	{ 0x81 , 0x58 },
	{ 0x82 , 0xDD },
	{ 0x83 , 0x26 },
	{ 0x84 , 0x32 },
	{ 0x85 , 0x1A },
	{ 0x86 , 0x00 },
	{ 0x87 , 0x10 },
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY yda165_register_type amp_speaker_and_speaker_stereo_path[REG_COUNT] = {
	{ 0x80 , 0x01 },
	{ 0x81 , 0x58 },
	{ 0x82 , 0xDD },
	{ 0x83 , 0x26 },
	{ 0x84 , 0x32 },
	{ 0x85 , 0x1A },
	{ 0x86 , 0x13 },
	{ 0x87 , 0x22 },  // 12 -> 22
	{ AMP_REGISTER_END   ,  0 },
};

REG_MEMORY yda165_register_type amp_none_path[REG_COUNT] = {
	{ 0x80 , 0x81 },
	{ 0x81 , 0x1B },
	{ 0x82 , 0xFD },
	{ 0x83 , 0x6A },
	{ 0x84 , 0x22 },
	{ 0x85 , 0x40 },
	{ 0x86 , 0x00 },
	{ 0x87 , 0x00 },
	{ 0x88 , 0x00 },
	{ AMP_REGISTER_END   ,  0 },
};

static const yda165_register_type *amp_sequence_path[AMP_PATH_MAX] ={
	amp_none_path,                        // AMP_PATH_NONE
	NULL,                                 // AMP_PATH_HANDSET
	amp_headset_stereo_path,              // AMP_PATH_HEADSET
	amp_speaker_stereo_path,              // AMP_PATH_SPEAKER
	amp_speaker_and_speaker_stereo_path,  // AMP_PATH_HEADSET_SPEAKER
	NULL,                                 // AMP_PATH_MAINMIC
	NULL,                                 // AMP_PATH_EARMIC
	amp_headset_stereo_path,              // AMP_PATH_HEADSET_NOMIC
};

/**
 * yda165_write - Sets register in YDA165
 * @param yda165: yda165 structure pointer passed by client
 * @param reg: register address
 * @param value: buffer values to be written
 * @param num_bytes: n bytes to write
 *
 * @returns result of the operation.
 */
static int yda165_write(struct snddev_yda165 *yda165, u8 reg, u8 *value,
							unsigned num_bytes)
{
	int ret, i;
	struct i2c_msg *msg;
	u8 data[num_bytes + 1];
	u8 mask_value[num_bytes];

	yda165 = &yda165_modules;

	mutex_lock(&yda165->xfer_lock);

	for (i = 0; i < num_bytes; i++)
		mask_value[i] = value[num_bytes-1-i];

	msg = &yda165->xfer_msg[0];
	msg->addr = yda165->client->addr;
	msg->flags = 0;
	msg->len = num_bytes + 1;
	msg->buf = data;
	data[0] = reg;
	memcpy(data+1, mask_value, num_bytes);

	ret = i2c_transfer(yda165->client->adapter, yda165->xfer_msg, 1);
	if (ret != 1) /* Try again if the write fails */
		ret = i2c_transfer(yda165->client->adapter, yda165->xfer_msg, 1);

	mutex_unlock(&yda165->xfer_lock);

	return ret;
}

#ifdef YDA165_I2C_TEST
/**
 * yda165_read - Reads registers in YDA165
 * @param yda165: yda165 structure pointer passed by client
 * @param reg: register address
 * @param value: i2c read of the register to be stored
 * @param num_bytes: n bytes to read.
 * @param mask: bit mask concerning its register
 *
 * @returns result of the operation.
*/
static int yda165_read(struct snddev_yda165 *yda165, u8 reg, u8 *value, unsigned num_bytes)
{
	int ret, i;
	u8 data[num_bytes];
	struct i2c_msg *msg;

	yda165 = &yda165_modules;

	mutex_lock(&yda165->xfer_lock);

	msg = &yda165->xfer_msg[0];
	msg->addr = yda165->client->addr;
	msg->len = 1;
	msg->flags = 0;
	msg->buf = &reg;

	msg = &yda165->xfer_msg[1];
	msg->addr = yda165->client->addr;
	msg->len = num_bytes;
	msg->flags = I2C_M_RD;
	msg->buf = data;

	ret = i2c_transfer(yda165->client->adapter, yda165->xfer_msg, 2);
	if (ret != 2) 	/* Try again if read fails first time */
		ret = i2c_transfer(yda165->client->adapter, yda165->xfer_msg, 2);
	if (ret == 2)
	{
		for (i = 0; i < num_bytes; i++) {
			value[i] = data[num_bytes-1-i];
		}
	}

	mutex_unlock(&yda165->xfer_lock);

	return ret;
}
#endif

#ifdef CONFIG_KTTECH_SOUND_TUNE
static void yda165_apply_register ( void *data, size_t size )
{
#ifdef YDA165_DEGUB_MSG
	int i = 0;
#endif
	int nCMDCount = 0;
	yda165_register_type *pFirstData = (yda165_register_type*)data;
	yda165_register_type *pCurData = (yda165_register_type*)data;
	AMP_PATH_TYPE_E path = (AMP_PATH_TYPE_E)pFirstData->reg;
	yda165_register_type *amp_regs = (yda165_register_type *)amp_sequence_path[path];

	if (amp_regs == NULL)
	{
		APM_INFO("not support path on yda165 path = %d\n", path);
		return;
	}

	nCMDCount = size / sizeof(yda165_register_type);  
	APM_INFO("Path = %d, Register Count = %d\n", path, nCMDCount);

#ifdef YDA165_DEGUB_MSG	
	for (i = 0 ; i < nCMDCount ; i ++)
	{
		APM_INFO("CMD = [0X%.2x] , [0X%.2x] \n" , pCurData->reg , pCurData->value);
		pCurData = pCurData + 1;
 	}
#endif

	pCurData = pFirstData + 1;
	memcpy(amp_regs, pCurData , size - sizeof(yda165_register_type));
}

void yda165_tuning(void *data, size_t size)
{
	if ( data == NULL || size == 0 || size > (sizeof(yda165_register_type) * AMP_REGISTER_MAX))
	{
		APM_INFO("invalid prarameters data = %d, size = %d \n", (int)data, size);
		return;	
	}

	yda165_apply_register(data, size);

	// on sequence이고 설정할 codec path 사용중인 경우 인 경우
	if (m_curr_path != -1)
	{
		yda165_enable(m_curr_path);
	}
}
EXPORT_SYMBOL(yda165_tuning);
#endif

void yda165_set_register(yda165_register_type *amp_regs)
{
	uint32_t loop = 0;
	struct snddev_yda165 *yda165 = &yda165_modules;

	while (amp_regs[loop].reg != AMP_REGISTER_END)
	{
		if (amp_regs[loop].reg == AMP_REGISTER_DELAY)
		{
			msleep(amp_regs[loop].value);
		}
		else
		{
			yda165_write(yda165, amp_regs[loop].reg , (u8 *)&amp_regs[loop].value, 1);
#ifdef YDA165_DEGUB_MSG				
			APM_INFO("reg 0x%x , value 0x%x",  amp_regs[loop].reg, amp_regs[loop].value);
#endif
		}
		loop++;
	}

	return;
}
EXPORT_SYMBOL(yda165_set_register);

void yda165_reset(void)
{
	const yda165_register_type *amp_regs = amp_sequence_path[AMP_PATH_NONE];

	if (amp_regs == NULL)
	{
		APM_INFO("Reset Register is Null!!!\n");
		return;
	}
	APM_INFO("Reset Register\n");

	yda165_set_register((yda165_register_type *)amp_regs);
}
EXPORT_SYMBOL(yda165_reset);

void yda165_enable_amplifier(void)
{
	struct snddev_yda165 *yda165 = &yda165_modules;
	u8 addr, value;

#if 1 // 20110708 by ssgun - check amp status
	if(atomic_read(&yda165->amp_enabled) == 1)
	{
		APM_INFO("The device(%d) is already open\n", m_curr_path);
		return;
	}
#endif

	// Set SP_AMIX/SP_BMIX/HP_AMIX/HP_BMIX register (0x87) to "0xxx".
	addr = 0x87;
	if (m_curr_path == AMP_PATH_HEADSET)
	{
		value = 0x02;
	}
	else if (m_curr_path == AMP_PATH_SPEAKER)
	{
		value = 0x20;
	}
	else if (m_curr_path == AMP_PATH_HEADSET_SPEAKER)
	{
		value = 0x22;
	}
	else
	{
		APM_INFO("Enable Amplifier - Unknown Device\n");
		return;
	}
	APM_INFO("Enable Amplifier - Register(0x87,0x%x)\n", value);

#if 1 // 20110708 by ssgun - check amp status
	atomic_set(&yda165->amp_enabled, 1);
#endif
	yda165_write(yda165, addr, &value, 1);
	return;
}
EXPORT_SYMBOL(yda165_enable_amplifier);

void yda165_disable_amplifier(void)
{
	struct snddev_yda165 *yda165 = &yda165_modules;
	u8 addr, value;

#if 1 // 20110708 by ssgun - check amp status
	if(atomic_read(&yda165->amp_enabled) == 0)
	{
		APM_INFO("The device(%d) is already closed\n", m_curr_path);
		return;
	}
#endif

	if(m_curr_path != AMP_PATH_HEADSET
		&& m_curr_path != AMP_PATH_SPEAKER
		&& m_curr_path != AMP_PATH_HEADSET_SPEAKER)
	{
		APM_INFO("Disable Amplifier - Unknown Device\n");
		return;
	}

	// Set SP_AMIX/SP_BMIX/HP_AMIX/HP_BMIX register (0x87) to "0".
	addr = 0x87;
	value = 0x00;
	APM_INFO("Disable Amplifier - Register(0x87,0x00)\n");

#if 1 // 20110708 by ssgun - check amp status
	atomic_set(&yda165->amp_enabled, 0);
#endif
	yda165_write(yda165, addr, &value, 1);
	return;
}
EXPORT_SYMBOL(yda165_disable_amplifier);

/**
 * yda165_enable - Enable path in YDA165
 * @param path: amp path
 *
 * @returns void
*/
void yda165_enable(AMP_PATH_TYPE_E path)
{
	const yda165_register_type *amp_regs = amp_sequence_path[path];
	struct snddev_yda165 *yda165 = &yda165_modules;

	if (path == AMP_PATH_MAINMIC || path == AMP_PATH_EARMIC)
	{
		APM_INFO("not support path on yda165 tx's path = %d -> %d\n", m_curr_path, path);
		return;
	}
	APM_INFO("Enable path = %d -> %d\n", m_curr_path, path);

	if (amp_regs == NULL || path <= AMP_PATH_NONE || path >= AMP_PATH_MAX)
	{
		APM_INFO("not support path on yda165 path = %d\n", path);
		goto spk_sw;
	}

	mutex_lock(&yda165->path_lock);
	if (m_curr_path == path)
	{
		mutex_unlock(&yda165->path_lock);
		APM_INFO("Is the same as the previous path. %d vs %d\n", m_curr_path, path);
		return;
	}
	m_curr_path = path;
	mutex_unlock(&yda165->path_lock);

	yda165_set_register((yda165_register_type *)amp_regs);

#if 1 // 20110708 by ssgun - check amp status
	if(path == AMP_PATH_HEADSET
		|| path == AMP_PATH_SPEAKER
		|| path == AMP_PATH_HEADSET_SPEAKER)
	{
		APM_INFO("Enable Amplifier : %d\n", path);
		atomic_set(&yda165->amp_enabled, 1);
	}
#endif

spk_sw:
	if (path == AMP_PATH_HANDSET)
	{
		gpio_set_value(YDA165_SPK_SW_GPIO, 1);
	}

	return;
}
EXPORT_SYMBOL(yda165_enable);

/**
 * yda165_disable - Disable path in YDA165
 * @param path: amp path
 *
 * @returns void
*/
void yda165_disable(AMP_PATH_TYPE_E path)
{
	struct snddev_yda165 *yda165 = &yda165_modules;
	u8 addr, value;

	if (path == AMP_PATH_MAINMIC || path == AMP_PATH_EARMIC)
	{
		APM_INFO("not support path on yda165 tx's path = %d -> %d\n", m_curr_path, path);
		return;
	}
	APM_INFO("Disable path = %d -> %d\n", m_curr_path, path);

	mutex_lock(&yda165->path_lock);
	m_curr_path = -1;
	mutex_unlock(&yda165->path_lock);

	gpio_set_value(YDA165_SPK_SW_GPIO, 0);

	// Set SRST register (0x80) to "1".
	// This causes all the registers to be set to default values.
	addr = 0x80;
	value = 0x80;
	yda165_write(yda165, addr, &value, 1);

#if 1 // Set SP_AMIX/SP_BMIX/HP_AMIX/HP_BMIX register (0x87) to "0".
	addr = 0x87;
	value = 0x00;
	yda165_write(yda165, addr, &value, 1);
#endif

#if 1 // 20110708 by ssgun - check amp status
	if(path == AMP_PATH_HEADSET
		|| path == AMP_PATH_SPEAKER
		|| path == AMP_PATH_HEADSET_SPEAKER)
	{
		APM_INFO("Disable Amplifier : %d\n", path);
		atomic_set(&yda165->amp_enabled, 0);
	}
#endif

	return;
}
EXPORT_SYMBOL(yda165_disable);

static int yda165_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct yda165_platform_data *pdata = client->dev.platform_data;
	struct snddev_yda165 *yda165;
	int status;

	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C) == 0)
	{
		dev_err(&client->dev, "can't talk I2C?\n");
		return -EIO;
	}

	if (pdata->yda165_setup != NULL)
	{
		status = pdata->yda165_setup(&client->dev);
		if (status < 0)
		{
			return status;
		}
	}

	yda165 = &yda165_modules;
	yda165->client = client;
	strlcpy(yda165->client->name, id->name, sizeof(yda165->client->name));
	mutex_init(&yda165->xfer_lock);
	mutex_init(&yda165->path_lock);
#if 1 // 20110708 by ssgun - check amp status
	atomic_set(&yda165->amp_enabled, 0);
#endif

#ifdef YDA165_I2C_TEST
	{
		u8 buf = 0xff;
		yda165_read(yda165, YDA165_RESET_REG, &buf, 1);
	}
#endif

	return 0;
}

static int __devexit yda165_remove(struct i2c_client *client)
{
	struct yda165_platform_data *pdata;

	pdata = client->dev.platform_data;
	yda165_modules.client = NULL;

	if (pdata->yda165_shutdown != NULL)
		pdata->yda165_shutdown(&client->dev);

	return 0;
}

static struct i2c_device_id yda165_id_table[] = {
	{"yda165", 0x0},
	{}
};
MODULE_DEVICE_TABLE(i2c, yda165_id_table);

static struct i2c_driver yda165_driver = {
		.driver			= {
			.owner		=	THIS_MODULE,
			.name		= 	"yda165",
		},
		.id_table		=	yda165_id_table,
		.probe			=	yda165_probe,
		.remove			=	__devexit_p(yda165_remove),
};

// 20110416 by ssgun - GPIO_CFG_NO_PULL -> GPIO_CFG_PULL_DOWN
#define SPK_SW_CTRL_0 \
	GPIO_CFG(YDA165_SPK_SW_GPIO, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA)

void yda165_init(void)
{
	int rc;

	rc = gpio_tlmm_config(SPK_SW_CTRL_0, GPIO_CFG_ENABLE);
	if (rc)
	{
		APM_ERR("Audio gpio  config failed: %d\n", rc);
		goto fail;
	}

	gpio_set_value(YDA165_SPK_SW_GPIO, 0);
	i2c_add_driver(&yda165_driver);

fail:
    return;
}
EXPORT_SYMBOL(yda165_init);

void yda165_exit(void)
{
	i2c_del_driver(&yda165_driver);
}
EXPORT_SYMBOL(yda165_exit);
