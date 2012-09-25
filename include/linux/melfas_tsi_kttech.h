/*
 * include/linux/melfas_tsi.h - platform data structure for f75375s sensor
 *
 * Copyright (C) 2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_melfas_TSI_H
#define _LINUX_melfas_TSI_H

#define MELFAS_I2C_NAME "melfas-tsi-ts"
#define melfas_I2C_ADDR	0x40

#define MELFAS_I2C_CMD_STATUS 0X00
#define MELFAS_I2C_CMD_SENSITIVITY 0x02
#define MELFAS_I2C_CMD_X_SIZE 0x08
#define MELFAS_I2C_CMD_Y_SIZE 0x0A
#define MELFAS_I2C_CMD_INPUT_INFORMATION 0x10
#define MELFAS_I2C_CMD_FIRMWARE_VER 0x20
#define MELFAS_I2C_CMD_HARDWARE_REV 0x21


#define MELFAS_DIAMOND_PATTERN 0x45

struct melfas_virtual_key {
	int status;
	int keycode;
	int range_min;
	int range_max;
};

struct melfas_i2c_rmi_platform_data {
	const char *input_name;
	uint16_t key_type;
	uint32_t version;	/* Use this entry for panels with */
				/* (major << 8 | minor) version or above. */
				/* If non-zero another array entry follows */
	int (*power)(int on);	/* Only valid in first array entry */
	struct melfas_virtual_key *virtual_key;
	int virtual_key_num;
	int intr;
	int wake_up;
};

#endif /* _LINUX_melfas_I2C_tsi_H */
