/*
 * Definitions for BMA150 G-sensor chip.
 */
#ifndef BMA150_H
#define BMA150_H

#include <linux/ioctl.h>

#define BMA150_I2C_DEVICE_N	     0x38


#define BMA150_I2C_NAME 	"BMA150_I2C"
#define BMA150_DEVICE_FILE_NAME "BMA150_ioctl"
#define BMA150_DEVICE_INPUT_NAME "BMA150_INPUT"



#define BMAIO				0x60

/* BMA150 register address */
#define CHIP_ID_REG			0x00
#define VERSION_REG			0x01
#define X_AXIS_LSB_REG		0x02
#define X_AXIS_MSB_REG		0x03
#define Y_AXIS_LSB_REG		0x04
#define Y_AXIS_MSB_REG		0x05
#define Z_AXIS_LSB_REG		0x06
#define Z_AXIS_MSB_REG		0x07
#define TEMP_RD_REG			0x08
#define SMB150_STATUS_REG	0x09
#define SMB150_CTRL_REG		0x0a
#define SMB150_CONF1_REG	0x0b
#define LG_THRESHOLD_REG	0x0c
#define LG_DURATION_REG		0x0d
#define HG_THRESHOLD_REG	0x0e
#define HG_DURATION_REG		0x0f
#define MOTION_THRS_REG		0x10
#define HYSTERESIS_REG		0x11
#define CUSTOMER1_REG		0x12
#define CUSTOMER2_REG		0x13
#define RANGE_BWIDTH_REG	0x14
#define SMB150_CONF2_REG	0x15

#define OFFS_GAIN_X_REG		0x16
#define OFFS_GAIN_Y_REG		0x17
#define OFFS_GAIN_Z_REG		0x18
#define OFFS_GAIN_T_REG		0x19
#define OFFSET_X_REG		0x1a
#define OFFSET_Y_REG		0x1b
#define OFFSET_Z_REG		0x1c
#define OFFSET_T_REG		0x1d

#define BMA150_REG_C0A_RESET_INT         0x40
#define BMA150_REG_C0A_SLEEP             0x01

#define BMA150_REG_C0B_ANY_MOTION        0x40
#define BMA150_REG_C0B_ENABLE_HG         0x02
#define BMA150_REG_C0B_ENABLE_LG         0x01

#define BMA150_REG_WID_BANDW_MASK        0xe0

#define BMA150_REG_C15_SPI4              0x80
#define BMA150_REG_C15_EN_ADV_INT        0x40
#define BMA150_REG_C15_NEW_DATA_INT      0x20
#define BMA150_REG_C15_LATCH_INT         0x10

#define BMA150_BANDW_INIT                0x04
#define BMA150_ANY_MOTION_INIT           0x02

/* temperature offset of -30 degrees in units of 0.5 degrees */
#define BMA150_TEMP_OFFSET               60



/* IOCTLs*/
#define BMA_IOCTL_INIT                  	_IO(BMAIO, 0x08)
#define BMA_IOCTL_WRITE                 	_IOW(BMAIO, 0x09, char[5])
#define BMA_IOCTL_READ                  	_IOWR(BMAIO, 0x0A, char[5])
#define BMA_IOCTL_SET_MODE	  			_IOW(BMAIO, 0x0B, short)
#define BMA_IOCTL_GET_INT	  				_IOR(BMAIO, 0x0C, short)
#define BMA_IOCTL_ENABLE               	_IO(BMAIO, 0x0D)
#define BMA_IOCTL_DISABLE               	_IO(BMAIO, 0x0E)
#define BMA_IOCTL_READ_ACCELERATION    	_IOWR(BMAIO, 0x0F, int[3])


/* range and bandwidth */
#define BMA_RANGE_2G			0
#define BMA_RANGE_4G			1
#define BMA_RANGE_8G			2

#define BMA_BW_25HZ		0
#define BMA_BW_50HZ		1
#define BMA_BW_100HZ		2
#define BMA_BW_190HZ		3
#define BMA_BW_375HZ		4
#define BMA_BW_750HZ		5
#define BMA_BW_1500HZ	6

/* mode settings */
#define BMA_MODE_NORMAL   	0
#define BMA_MODE_SLEEP       	1

struct bma150_platform_data {
	int intr;
};

#endif
