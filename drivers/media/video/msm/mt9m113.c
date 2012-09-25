#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <media/msm_camera.h>
#include <mach/gpio.h>
#include <mach/camera.h>

#include "mt9m113.h"

/* Micron MT9M113 Registers and their values */
/*
 *  Camera i2c data, clock
 */ 
#define  MT9M113_I2C_NAME         "mt9m113"

/* 
 * Camera image sensor slave address
 */
#define  MT9M113_I2C_WR_ID     0x78  // Sensor 스펙 보고 맞출 것

#define  MT9M113_DEFAULT_CLOCK_RATE  24000000

/* Sensor Core Registers */
#define  REG_MT9M113_MODEL_ID 0x0000
#define  MT9M113_MODEL_ID     0x2480

#define SENSOR_DEBUG 0

#undef CDBG
#define CDBG(fmt, args...) printk(KERN_INFO "driver.mt9m113: " fmt, ##args) 

struct mt9m113_work {
	struct work_struct work;
};


static struct  mt9m113_work *mt9m113_sensorw;
static struct  i2c_client *mt9m113_client;

struct mt9m113_ctrl {
	const struct msm_camera_sensor_info *sensordata;
};


static struct mt9m113_ctrl *mt9m113_ctrl;

static DECLARE_WAIT_QUEUE_HEAD(mt9m113_wait_queue);
DEFINE_MUTEX(mt9m113_mut);

static int16_t mt9m113_effect = CAMERA_EFFECT_OFF;
static bool CSI_CONFIG;

/*=============================================================
	EXTERNAL DECLARATIONS
==============================================================*/
extern struct mt9m113_reg mt9m113_regs;


/*=============================================================*/
static int32_t mt9m113_i2c_txdata(unsigned short saddr,
	unsigned char *txdata, int length)
{
	struct i2c_msg msg[] = {
		{
			.addr = saddr,
			.flags = 0,
			.len = length,
			.buf = txdata,
		},
	};

#if 0
//#if SENSOR_DEBUG
	if (length == 2)
		CDBG("msm_io_i2c_w: 0x%04x 0x%04x\n",
			*(u16 *) txdata, *(u16 *) (txdata + 2));
	else if (length == 4)
		CDBG("msm_io_i2c_w: 0x%04x\n", *(u16 *) txdata);
	else
		CDBG("msm_io_i2c_w: length = %d\n", length);
#endif
	if (i2c_transfer(mt9m113_client->adapter, msg, 1) < 0) {
		CDBG("mt9m113_i2c_txdata failed\n");
		return -EIO;
	}

	return 0;
}

static int32_t mt9m113_i2c_write(unsigned short saddr,
	unsigned short waddr, unsigned short wdata, enum mt9m113_width width)
{
	int32_t rc = -EIO;
	unsigned char buf[4];

	memset(buf, 0, sizeof(buf));
	switch (width) {
	case WORD_LEN: {
		buf[0] = (waddr & 0xFF00)>>8;
		buf[1] = (waddr & 0x00FF);
		buf[2] = (wdata & 0xFF00)>>8;
		buf[3] = (wdata & 0x00FF);

#if SENSOR_DEBUG
		CDBG("mt9m113_i2c_write: waddr= 0x%04x wdata=0x%04x\n", waddr, wdata);
#endif
		rc = mt9m113_i2c_txdata(saddr, buf, 4);
	}
		break;

	case BYTE_LEN: {
		buf[0] = waddr;
		buf[1] = wdata;
		rc = mt9m113_i2c_txdata(saddr, buf, 2);
	}
		break;

	default:
		break;
	}

	if (rc < 0)
		CDBG(
		"i2c_write failed, addr = 0x%x, val = 0x%x!\n",
		waddr, wdata);

	return rc;
}

static int32_t mt9m113_i2c_write_table(
	struct mt9m113_i2c_reg_conf const *reg_conf_tbl,
	int num_of_items_in_table)
{
	int i;
	int32_t rc = -EIO;

	for (i = 0; i < num_of_items_in_table; i++) {
		rc = mt9m113_i2c_write(mt9m113_client->addr,
			reg_conf_tbl->waddr, reg_conf_tbl->wdata,
			reg_conf_tbl->width);
		if (rc < 0)
			break;
		if (reg_conf_tbl->mdelay_time != 0)
			mdelay(reg_conf_tbl->mdelay_time);
		reg_conf_tbl++;
	}

	return rc;
}

static int mt9m113_i2c_rxdata(unsigned short saddr,
	unsigned char *rxdata, int length)
{
	struct i2c_msg msgs[] = {
	{
		.addr   = saddr,
		.flags = 0,
		.len   = 2,
		.buf   = rxdata,
	},
	{
		.addr   = saddr,
		.flags = I2C_M_RD,
		.len   = length,
		.buf   = rxdata,
	},
	};

#if SENSOR_DEBUG
	if (length == 2)
		CDBG("msm_io_i2c_r: 0x%04x 0x%04x\n",
			*(u16 *) rxdata, *(u16 *) (rxdata + 2));
	else if (length == 4)
		CDBG("msm_io_i2c_r: 0x%04x\n", *(u16 *) rxdata);
	else
		CDBG("msm_io_i2c_r: length = %d\n", length);
#endif

	if (i2c_transfer(mt9m113_client->adapter, msgs, 2) < 0) {
		CDBG("mt9m113_i2c_rxdata failed!\n");
		return -EIO;
	}

	return 0;
}

static int32_t mt9m113_i2c_read(unsigned short   saddr,
	unsigned short raddr, unsigned short *rdata, enum mt9m113_width width)
	
{
	int32_t rc = 0;
	unsigned char buf[4];

  //CDBG("%s: line=%d\n", __func__, __LINE__);

	if (!rdata)
		return -EIO;

	memset(buf, 0, sizeof(buf));

	switch (width) {
	case WORD_LEN: {
		buf[0] = (raddr & 0xFF00)>>8;
		buf[1] = (raddr & 0x00FF);

		rc = mt9m113_i2c_rxdata(saddr, buf, 2);
		if (rc < 0)
			return rc;

		*rdata = buf[0] << 8 | buf[1];
	}
		break;

	default:
		break;
	}

	if (rc < 0)
		CDBG("mt9m113_i2c_read addr = 0x%x failed!\n", saddr);

	return rc;
}

static int32_t mt9m113_seq_probe(void)
{
	int32_t rc = -EINVAL;
	int32_t i = 0;
	unsigned short readData;

	do {
		msleep(40);
		mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA103, WORD_LEN);    
		mt9m113_i2c_read(mt9m113_client->addr, 0x0990, &readData, WORD_LEN);
		i++;
		if (readData==0)
			rc = 0;
	} while (rc != 0 && i < 20);
	//CDBG("%s: readData(0x%x), i(%d)\n", __func__, readData, i);
	rc = mt9m113_i2c_write(mt9m113_client->addr, 0x001A, 0x0010, WORD_LEN); //{0x098C, 0xA103, WORD_LEN, 0  },  // MCU_ADDRESS [SEQ_CMD] 
	return rc;
}

static int32_t mt9m113_stop_stream(void)
{
	int32_t rc = 0;

	CDBG("%s\n", __func__);

	rc = mt9m113_i2c_write(mt9m113_client->addr, 0x001C, 0x0001, WORD_LEN); // MCU_BOOT_MODE         
	rc = mt9m113_i2c_write(mt9m113_client->addr, 0x001C, 0x0000, WORD_LEN); // MCU_BOOT_MODE         
	mdelay(10);
	rc = mt9m113_i2c_write(mt9m113_client->addr, 0x001A, 0x0011, WORD_LEN); // RESET_AND_MISC_CONTROL
	rc = mt9m113_i2c_write(mt9m113_client->addr, 0x001A, 0x0010, WORD_LEN); // RESET_AND_MISC_CONTROL
	rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0016, 0x00FF, WORD_LEN); // CLOCKS_CONTROL  
	rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0018, 0x0028, WORD_LEN); // STANDBY_CONTROL

	return rc;
}

static int32_t mt9m113_start_stream(void)
{
	int32_t rc = 0;
	rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA103, WORD_LEN); //{0x098C,0xA103, WORD_LEN,0  }, // MCU_ADDRESS [SEQ_CMD] 
	rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0006, WORD_LEN); //{0x0990,0x0006, WORD_LEN,300}, // MCU_DATA_0            
	rc = mt9m113_seq_probe();

	rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA103, WORD_LEN); //{0x098C,0xA103, WORD_LEN,0  }, // MCU_ADDRESS [SEQ_CMD] 
	rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0005, WORD_LEN); //{0x0990,0x0005, WORD_LEN,300}, // MCU_DATA_0            	
	rc = mt9m113_seq_probe();

	CDBG("%s\n", __func__);
	return rc;
}


static int32_t mt9m113_sensor_setting(int update_type, int rt)
{
	int32_t rc = 0;
	unsigned short readData = 0;	
	struct msm_camera_csi_params mt9m113_csi_params;

	switch (update_type) {
	case REG_INIT:
		CDBG("%s: REG_INIT \n", __func__);
		CSI_CONFIG = 0;
		break;

	case UPDATE_PERIODIC:
		CDBG("%s: UPDATE_PERIODIC \n", __func__);

		if (rt == RES_PREVIEW) {
			if(!CSI_CONFIG) {
					rc = mt9m113_stop_stream();
					msleep(15);

					mt9m113_csi_params.data_format = CSI_8BIT;
					mt9m113_csi_params.lane_cnt = 1;
					mt9m113_csi_params.lane_assign = 0xe4;
					mt9m113_csi_params.dpcm_scheme = 0;
					mt9m113_csi_params.settle_cnt = 20;

					rc = msm_camio_csi_config(&mt9m113_csi_params);
					msleep(50);			

					CDBG("%s: write reg_table \n", __func__);
					rc = mt9m113_i2c_write_table(&mt9m113_regs.regtbl[0],
										mt9m113_regs.regtbl_size);

					rc = mt9m113_start_stream();
					CSI_CONFIG = 1;
			}			


			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA104, WORD_LEN); 	 // MCU_ADDRESS [SEQ_STATE] 
			rc = mt9m113_i2c_read(mt9m113_client->addr, 0x0990, &readData, WORD_LEN);
			if (readData != 3) {
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA103, WORD_LEN); //{0x098C, 0xA103, WORD_LEN, 0  },  // MCU_ADDRESS [SEQ_CMD] 
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0001, WORD_LEN); //{0x0990, 0x0002, WORD_LEN, 300},  // MCU_DATA_0
				rc = mt9m113_seq_probe();
			}

			//CDBG("CRITICAL1>>> 0x3402=0x%4x, rc(%d)\n", readData, rc);
			return rc;

		}
		else if(rt == RES_CAPTURE) {
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA115, WORD_LEN); //{0x098C, 0xA115, WORD_LEN, 0  },  // MCU_ADDRESS [SEQ_CAP_MODE] 
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0002, WORD_LEN); //{0x0990, 0x0072, WORD_LEN, 0  },  // MCU_DATA_0            
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA103, WORD_LEN); //{0x098C, 0xA103, WORD_LEN, 0  },  // MCU_ADDRESS [SEQ_CMD] 
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0002, WORD_LEN); //{0x0990, 0x0002, WORD_LEN, 300},  // MCU_DATA_0
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA102, WORD_LEN); //{0x098C, 0xA103, WORD_LEN, 0  },  // MCU_ADDRESS [SEQ_MODE] 
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x000F, WORD_LEN); //{0x0990, 0x0002, WORD_LEN, 300},  // MCU_DATA_0
			rc = mt9m113_seq_probe();
			return rc;
		}
		break;
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}

static int32_t mt9m113_power_down(void)
{
	CDBG("%s: e\n", __func__);
#ifdef CONFIG_KTTECH_MT9M113 // jykim
	if (mt9m113_ctrl->sensordata->cam_shutdown != NULL)
	  mt9m113_ctrl->sensordata->cam_shutdown(&mt9m113_client->dev, 1); // Front camera
#endif

	//CDBG("%s: x\n", __func__);
	return 0;
}

static int mt9m113_probe_init_done(const struct msm_camera_sensor_info *data)
{
	CDBG("%s: e\n", __func__);

	gpio_set_value_cansleep(data->sensor_reset, 0);
	gpio_free(data->sensor_reset);

	//CDBG("%s: x\n", __func__);
	return 0;
}


static int mt9m113_set_brightness(int brightness)
{
	long rc = 0;

	//CDBG("%s: brightness=%d == e\n", __func__, brightness);

	switch (brightness) {

		case 0: { // Brightness -5
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA24F, WORD_LEN);  // MCU_ADDRESS
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0016, WORD_LEN);  // AE_BASETARGET
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA207, WORD_LEN);  // MCU_ADDRESS
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0006, WORD_LEN);  // AE_GATE
		}
		break;

		case 1: { // Brightness -3
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA24F, WORD_LEN);  // MCU_ADDRESS
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0022, WORD_LEN);  // AE_BASETARGET
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA207, WORD_LEN);  // MCU_ADDRESS
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0006, WORD_LEN);  // AE_GATE
		}
		break;

		case 2: { // Brightness -2
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA24F, WORD_LEN);  // MCU_ADDRESS
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0030, WORD_LEN);  // AE_BASETARGET
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA207, WORD_LEN);  // MCU_ADDRESS
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0008, WORD_LEN);  // AE_GATE

		}

		case 3: { // Brightness 0
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA24F, WORD_LEN);  // MCU_ADDRESS
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0040, WORD_LEN);  // AE_BASETARGET
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA207, WORD_LEN);  // MCU_ADDRESS
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x000A, WORD_LEN);  // AE_GATE

		}
		break;

		case 4: { // Brightness +2
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA24F, WORD_LEN);  // MCU_ADDRESS
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0054, WORD_LEN);  // AE_BASETARGET
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA207, WORD_LEN);  // MCU_ADDRESS
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x000A, WORD_LEN);  // AE_GATE

		}
		break;

		case 5: { // Brightness +3
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA24F, WORD_LEN);  // MCU_ADDRESS
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0068, WORD_LEN);  // AE_BASETARGET
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA207, WORD_LEN);  // MCU_ADDRESS
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x000A, WORD_LEN);  // AE_GATE

		}
		break;

		case 6: { // Brightness +5
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA24F, WORD_LEN);  // MCU_ADDRESS
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x007C, WORD_LEN);  // AE_BASETARGET
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA207, WORD_LEN);  // MCU_ADDRESS
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x000A, WORD_LEN);  // AE_GATE
		}
		break;

		default: {
		}
	}


	//CDBG("%s: x\n", __func__);
	return rc;

}

static int mt9m113_set_wb(int wb)
{
	long rc = 0;

	//CDBG("%s: wb=%d === e\n", __func__, wb);

	switch (wb) {

		case 1: { // CAMERA_WB_AUTO
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34A, WORD_LEN); // MCU_ADDRESS [AWB_GAIN_MIN]        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0059, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34B, WORD_LEN); // MCU_ADDRESS [AWB_GAIN_MAX]        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x00A6, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34C, WORD_LEN); // MCU_ADDRESS [AWB_GAINMIN_B]       
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0059, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34D, WORD_LEN); // MCU_ADDRESS [AWB_GAINMAX_B]       
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x00A6, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA351, WORD_LEN); // MCU_ADDRESS [AWB_CCM_POSITION_MIN]
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0000, WORD_LEN); // MCU_DATA_0
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA352, WORD_LEN); // MCU_ADDRESS [AWB_CCM_POSITION_MAX]
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x007F, WORD_LEN); // MCU_DATA_0
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA353, WORD_LEN); // MCU_ADDRESS [AWB_CCM_POSITION]    
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0040, WORD_LEN); // MCU_DATA_0                        
		}
		break;

		case 5: { // CAMERA_WB_DAYLIGHT
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34A, WORD_LEN); // MCU_ADDRESS [AWB_GAIN_MIN]        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0088, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34B, WORD_LEN); // MCU_ADDRESS [AWB_GAIN_MAX]        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0098, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34C, WORD_LEN); // MCU_ADDRESS [AWB_GAINMIN_B]       
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x007C, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34D, WORD_LEN); // MCU_ADDRESS [AWB_GAINMAX_B]       
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0084, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA351, WORD_LEN); // MCU_ADDRESS [AWB_CCM_POSITION_MIN]
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x007D, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA352, WORD_LEN); // MCU_ADDRESS [AWB_CCM_POSITION_MAX]
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x007F, WORD_LEN); // MCU_DATA_0		  
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA353, WORD_LEN); // MCU_ADDRESS [AWB_CCM_POSITION]    
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x007E, WORD_LEN); // MCU_DATA_0                        
		}
		break;

		case 6: { // CAMERA_WB_CLOUDY_DAYLIGHT
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34A, WORD_LEN); // MCU_ADDRESS [AWB_GAIN_MIN]        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0088, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34B, WORD_LEN); // MCU_ADDRESS [AWB_GAIN_MAX]        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0098, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34C, WORD_LEN); // MCU_ADDRESS [AWB_GAINMIN_B]       
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x007C, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34D, WORD_LEN); // MCU_ADDRESS [AWB_GAINMAX_B]       
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0084, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA351, WORD_LEN); // MCU_ADDRESS [AWB_CCM_POSITION_MIN]
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0068, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA352, WORD_LEN); // MCU_ADDRESS [AWB_CCM_POSITION_MAX]
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x006A, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA353, WORD_LEN); // MCU_ADDRESS [AWB_CCM_POSITION]		 
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0069, WORD_LEN); // MCU_DATA_0                        
		}
		break;

		case 4: { // CAMERA_WB_FLUORESCENT
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34A, WORD_LEN); // MCU_ADDRESS [AWB_GAIN_MIN]        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0088, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34B, WORD_LEN); // MCU_ADDRESS [AWB_GAIN_MAX]        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0098, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34C, WORD_LEN); // MCU_ADDRESS [AWB_GAINMIN_B]       
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x007C, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34D, WORD_LEN); // MCU_ADDRESS [AWB_GAINMAX_B]       
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0084, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA351, WORD_LEN); // MCU_ADDRESS [AWB_CCM_POSITION_MIN]
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0036, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA352, WORD_LEN); // MCU_ADDRESS [AWB_CCM_POSITION_MAX]
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0038, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA353, WORD_LEN); // MCU_ADDRESS [AWB_CCM_POSITION]		 
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0037, WORD_LEN); // MCU_DATA_0                        
		}
		break;

		case 3: { // CAMERA_WB_INCANDESCENT
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34A, WORD_LEN); // MCU_ADDRESS [AWB_GAIN_MIN]        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0088, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34B, WORD_LEN); // MCU_ADDRESS [AWB_GAIN_MAX]        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0098, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34C, WORD_LEN); // MCU_ADDRESS [AWB_GAINMIN_B]       
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x007C, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA34D, WORD_LEN); // MCU_ADDRESS [AWB_GAINMAX_B]       
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0084, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA351, WORD_LEN); // MCU_ADDRESS [AWB_CCM_POSITION_MIN]
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0000, WORD_LEN); // MCU_DATA_0		  
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA352, WORD_LEN); // MCU_ADDRESS [AWB_CCM_POSITION_MAX]
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0004, WORD_LEN); // MCU_DATA_0                        
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA353, WORD_LEN); // MCU_ADDRESS [AWB_CCM_POSITION]    
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0002, WORD_LEN); // MCU_DATA_0                        
		}
		break;

		default: {
		}
	}


	//CDBG("%s: x\n", __func__);
	return rc;

}

static long mt9m113_set_effect(int mode, int effect)
{
	long rc = 0;
	unsigned short readData = 0;

	//CDBG("%s: mode=%d, effect=%d  == e\n", __func__, mode, effect);

	switch (effect) {
		case CAMERA_EFFECT_OFF: { // 0
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0x2759, WORD_LEN); 	 // MCU_ADDRESS [SEQ_STATE] 
			rc = mt9m113_i2c_read(mt9m113_client->addr, 0x0990, &readData, WORD_LEN);
			if (readData != 0x6440) {
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0x2759, WORD_LEN); // MCU_ADDRESS [MODE_SPEC_EFFECTS_A]         
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x6440, WORD_LEN); // MCU_DATA_0         
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0x275B, WORD_LEN); // MCU_ADDRESS [MODE_SPEC_EFFECTS_B]
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x6440, WORD_LEN); // MCU_DATA_0
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA103, WORD_LEN); // MCU_ADDRESS [SEQ_CMD] 
				rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0005, WORD_LEN); // MCU_DATA_0
				rc = mt9m113_seq_probe();
			}
		}
		break;

		case CAMERA_EFFECT_MONO: { // 1
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0x2759, WORD_LEN); // MCU_ADDRESS [MODE_SPEC_EFFECTS_A]		 
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x6441, WORD_LEN); // MCU_DATA_0		  
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0x275B, WORD_LEN); // MCU_ADDRESS [MODE_SPEC_EFFECTS_B]
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x6441, WORD_LEN); // MCU_DATA_0
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA103, WORD_LEN); // MCU_ADDRESS [SEQ_CMD] 
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0005, WORD_LEN); // MCU_DATA_0	
			rc = mt9m113_seq_probe();
		}
		break;

		case CAMERA_EFFECT_NEGATIVE: { // 2
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0x2759, WORD_LEN); // MCU_ADDRESS [MODE_SPEC_EFFECTS_A]		 
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x6443, WORD_LEN); // MCU_DATA_0		  
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0x275B, WORD_LEN); // MCU_ADDRESS [MODE_SPEC_EFFECTS_B]
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x6443, WORD_LEN); // MCU_DATA_0
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA103, WORD_LEN); // MCU_ADDRESS [SEQ_CMD] 
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0005, WORD_LEN); // MCU_DATA_0
			rc = mt9m113_seq_probe();
		}
		break;

		case CAMERA_EFFECT_SOLARIZE: { // 3
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0x2759, WORD_LEN); // MCU_ADDRESS [MODE_SPEC_EFFECTS_A]		 
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x6045, WORD_LEN); // MCU_DATA_0		  
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0x275B, WORD_LEN); // MCU_ADDRESS [MODE_SPEC_EFFECTS_B]
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x6045, WORD_LEN); // MCU_DATA_0
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA103, WORD_LEN); // MCU_ADDRESS [SEQ_CMD] 
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0005, WORD_LEN); // MCU_DATA_0
			rc = mt9m113_seq_probe();
		}
		break;

		case CAMERA_EFFECT_SEPIA: { // 7
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0x2759, WORD_LEN); // MCU_ADDRESS [MODE_SPEC_EFFECTS_A]		 
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x6442, WORD_LEN); // MCU_DATA_0		  
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0x275B, WORD_LEN); // MCU_ADDRESS [MODE_SPEC_EFFECTS_B]
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x6442, WORD_LEN); // MCU_DATA_0
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x098C, 0xA103, WORD_LEN); // MCU_ADDRESS [SEQ_CMD] 
			rc = mt9m113_i2c_write(mt9m113_client->addr, 0x0990, 0x0005, WORD_LEN); // MCU_DATA_0
			rc = mt9m113_seq_probe();
		}
		break;

		default: {
		}
	}
	mt9m113_effect = effect;

	//CDBG("%s: x\n", __func__);
	return rc;
}

static int32_t mt9m113_video_config(int mode)
{
	int32_t rc = 0;
	int rt;

	/* change sensor resolution if needed */
	rt = RES_PREVIEW;

	if (mt9m113_sensor_setting(UPDATE_PERIODIC, rt) < 0)
		return rc;

	return rc;
}

static int32_t mt9m113_snapshot_config(int mode)
{
	int32_t rc = 0;	
	int rt;
	rt = RES_CAPTURE;

	if (mt9m113_sensor_setting(UPDATE_PERIODIC, rt) < 0)
		return rc;

	return rc;
}

static long mt9m113_set_sensor_mode(int mode)
{
	int rc = 0;

	CDBG("%s: e ================   mode=%d\n", __func__, mode);

	switch (mode) {
	case SENSOR_PREVIEW_MODE:
		CDBG("%s: SENSOR_PREVIEW_MODE \n", __func__);
		rc = mt9m113_video_config(mode);
		break;

	case SENSOR_SNAPSHOT_MODE:
	case SENSOR_RAW_SNAPSHOT_MODE:
		CDBG("%s: SENSOR_SNAPSHOT_MODE or SENSOR_RAW_SNAPSHOT_MODE\n", __func__);
		rc = mt9m113_snapshot_config(mode);
		break;

	default:
		return -EINVAL;
	}

	CDBG("%s: =================== x\n", __func__);
	return 0;
}

static int mt9m113_probe_init_sensor(const struct msm_camera_sensor_info *data)
{
	uint16_t model_id = 0;
	int rc = 0;

#ifdef CONFIG_KTTECH_MT9M113 // jykim
	CDBG("%s: e line=%d\n", __func__, __LINE__);
	data->cam_setup(&mt9m113_client->dev, 1);
#endif

	//CDBG("%s: %d\n", __func__, __LINE__);
	//CDBG("sensor_reset = %d\n", rc);
	gpio_direction_output(data->sensor_reset, 0);
	gpio_set_value_cansleep(data->sensor_reset, 0);
	msleep(10);
	gpio_set_value_cansleep(data->sensor_reset, 1);
	msleep(10);

	/* Micron suggested Power up block End */
	/* Read the Model ID of the sensor */
	rc = mt9m113_i2c_read(mt9m113_client->addr,
		REG_MT9M113_MODEL_ID, &model_id, WORD_LEN);
	if (rc < 0)
		goto init_probe_fail;

	CDBG("mt9m113 model_id = 0x%x\n", model_id);

	/* Check if it matches it with the value in Datasheet */
	if (model_id != MT9M113_MODEL_ID) {
		rc = -EINVAL;
		goto init_probe_fail;
	}

	CDBG("%s: x \n", __func__);
	goto init_probe_done;

init_probe_fail:
	CDBG(" mt9m113_probe_init_sensor fails\n");
	gpio_direction_output(data->sensor_reset, 0);
	mt9m113_probe_init_done(data);

init_probe_done:
	CDBG(" mt9m113_probe_init_sensor finishes\n");
	return rc;
}

int mt9m113_sensor_open_init(const struct msm_camera_sensor_info *data)
{
	int rc = 0;

  CDBG("%s: e\n", __func__);

	mt9m113_ctrl = kzalloc(sizeof(struct mt9m113_ctrl), GFP_KERNEL);
	if (!mt9m113_ctrl) {
		CDBG("mt9m113_init failed!\n");
		rc = -ENOMEM;
		goto init_done;
	}

	if (data)
		mt9m113_ctrl->sensordata = data;

	msm_camio_clk_rate_set(MT9M113_DEFAULT_CLOCK_RATE);
	rc = mt9m113_probe_init_sensor(data);
	if (rc < 0) {
		CDBG("mt9m113_sensor_init failed!\n");
		goto init_fail;
	}

	rc = mt9m113_sensor_setting(REG_INIT, RES_PREVIEW);
  
	if (rc < 0) {
		goto init_fail;
	} else
		goto init_done;


init_done:
	CDBG("%s: x init_done\n", __func__);
	return rc;

init_fail:
	CDBG("%s: init_fail\n", __func__);
	gpio_direction_output(data->sensor_reset, 0);
	mt9m113_probe_init_done(data);
	
	return rc;
}

static int mt9m113_init_client(struct i2c_client *client)
{
  CDBG("%s: line=%d\n", __func__, __LINE__);

	/* Initialize the MSM_CAMI2C Chip */
	init_waitqueue_head(&mt9m113_wait_queue);
	return 0;
}

int mt9m113_sensor_config(void __user *argp)
{
	struct sensor_cfg_data cfg_data;
	long   rc = 0;

  //CDBG("%s: line=%d\n", __func__, __LINE__);

	if (copy_from_user(&cfg_data,
			(void *)argp,
			sizeof(struct sensor_cfg_data)))
		return -EFAULT;

  /* down(&mt9m113_sem); */

  //CDBG("mt9m113_ioctl, cfgtype = %d, mode = %d\n", cfg_data.cfgtype, cfg_data.mode);

		switch (cfg_data.cfgtype) {
		case CFG_SET_MODE:
			rc = mt9m113_set_sensor_mode(
						cfg_data.mode);
			break;

		case CFG_SET_EFFECT:
			rc = mt9m113_set_effect(cfg_data.mode, cfg_data.cfg.effect);
			break;
			
		case CFG_SET_BRIGHTNESS:
			rc = mt9m113_set_brightness(cfg_data.cfg.brightness);
			break;
			
		case CFG_SET_WB:
			rc = mt9m113_set_wb(cfg_data.cfg.wb);
			break;

		case CFG_GET_AF_MAX_STEPS:
		default:
			rc = -EINVAL;
			break;
		}

	/* up(&mt9m113_sem); */

	return rc;
}

int mt9m113_sensor_release(void)
{
	int rc = 0;
	CDBG("%s: line=%d\n", __func__, __LINE__);

	mutex_lock(&mt9m113_mut);

	mt9m113_power_down();
#ifndef CONFIG_KTTECH_MT9M113 // jykim remove warning
	gpio_set_value_cansleep(mt9m113_ctrl->sensordata->sensor_reset, 0);
	msleep(5);
	gpio_free(mt9m113_ctrl->sensordata->sensor_reset);
#endif

	kfree(mt9m113_ctrl);
	mt9m113_ctrl = NULL;

	mutex_unlock(&mt9m113_mut);

	return rc;
}

static int mt9m113_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int rc = 0;

  CDBG("%s: line=%d\n", __func__, __LINE__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		rc = -ENOTSUPP;
		goto probe_failure;
	}

	mt9m113_sensorw =
		kzalloc(sizeof(struct mt9m113_work), GFP_KERNEL);

	if (!mt9m113_sensorw) {
		rc = -ENOMEM;
		goto probe_failure;
	}

	i2c_set_clientdata(client, mt9m113_sensorw);
	mt9m113_init_client(client);
	mt9m113_client = client;
	mt9m113_client->addr = mt9m113_client->addr >> 1;

	CDBG("mt9m113_probe succeeded!\n");

	return 0;

probe_failure:
	kfree(mt9m113_sensorw);
	mt9m113_sensorw = NULL;
	CDBG("mt9m113_probe failed!\n");
	return rc;
}

static const struct i2c_device_id mt9m113_i2c_id[] = {
	{ "mt9m113", 0},
	{ },
};

static struct i2c_driver mt9m113_i2c_driver = {
	.id_table = mt9m113_i2c_id,
	.probe  = mt9m113_i2c_probe,
	.remove = __exit_p(mt9m113_i2c_remove),
	.driver = {
		.name = "mt9m113",
	},
};

static int mt9m113_sensor_probe(const struct msm_camera_sensor_info *info,
				struct msm_sensor_ctrl *s)
{
	int rc = i2c_add_driver(&mt9m113_i2c_driver);
	if (rc < 0 
#ifdef CONFIG_KTTECH_MT9M113 // jykim
	|| mt9m113_client == NULL
#endif
	) {
		rc = -ENOTSUPP;
		goto probe_fail;
	}

#ifndef CONFIG_KTTECH_MT9M113 // jykim
	msm_camio_clk_rate_set(MT9M113_DEFAULT_CLOCK_RATE);
	rc = mt9m113_probe_init_sensor(info);
	if (rc < 0)
		goto probe_fail;
#endif
	s->s_init = mt9m113_sensor_open_init;
	s->s_release = mt9m113_sensor_release;
	s->s_config  = mt9m113_sensor_config;
	s->s_camera_type = FRONT_CAMERA_2D;
	s->s_mount_angle = 180; // jykim x-y flip
 
	gpio_set_value_cansleep(info->sensor_reset, 0);

#ifndef CONFIG_KTTECH_MT9M113 // jykim
	mt9e013_probe_init_done(info);
#endif

 return rc;
 
probe_fail:
	CDBG("%s %s:%d\n", __FILE__, __func__, __LINE__);
	return rc;
}

static int __mt9m113_probe(struct platform_device *pdev)
{
  //CDBG("%s: line=%d\n", __func__, __LINE__);
	return msm_camera_drv_start(pdev, mt9m113_sensor_probe);
}

static struct platform_driver msm_camera_driver = {
	.probe = __mt9m113_probe,
	.driver = {
		.name = "msm_camera_mt9m113",
		.owner = THIS_MODULE,
	},
};

static int __init mt9m113_init(void)
{
  //CDBG("%s: line=%d\n", __func__, __LINE__);
	return platform_driver_register(&msm_camera_driver);
}

module_init(mt9m113_init);
