/* Copyright (c) 2009, Code Aurora Forum. All rights reserved.
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
 *
 */

#ifndef MT9M113_H
#define MT9M113_H

#include <linux/types.h>
#include <mach/camera.h>

extern struct mt9m113_reg mt9m113_regs;

enum mt9m113_width {
	WORD_LEN,
	BYTE_LEN
};

enum mt9m113_setting {
	RES_PREVIEW,
	RES_CAPTURE
};
enum mt9m113_reg_update {
	/* Sensor egisters that need to be updated during initialization */
	REG_INIT,
	/* Sensor egisters that needs periodic I2C writes */
	UPDATE_PERIODIC,
	/* All the sensor Registers will be updated */
	UPDATE_ALL,
	/* Not valid update */
	UPDATE_INVALID
};

struct mt9m113_i2c_reg_conf {
	unsigned short waddr;
	unsigned short wdata;
	enum mt9m113_width width;
	unsigned short mdelay_time;
};

struct mt9m113_reg {
	const struct mt9m113_i2c_reg_conf *regtbl;
	uint16_t regtbl_size; 
#if 0
	const struct register_address_value_pair *prev_snap_reg_settings;
	uint16_t prev_snap_reg_settings_size;
	const struct register_address_value_pair *noise_reduction_reg_settings;
	uint16_t noise_reduction_reg_settings_size;
	const struct mt9m113_i2c_reg_conf *plltbl;
	uint16_t plltbl_size;
	const struct mt9m113_i2c_reg_conf *stbl;
	uint16_t stbl_size;
	const struct mt9m113_i2c_reg_conf *rftbl;
	uint16_t rftbl_size;
#endif
};

#endif /* MT9M113_H */
