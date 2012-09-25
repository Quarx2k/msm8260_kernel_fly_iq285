/*
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef CAPELLA_H
#define CAPELLA_H

#include <linux/ioctl.h>


#define I2C_NAME_SIZE 20
#define CAPELLA_ALS_I2C_NAME "capella-als"
#define CAPELLA_PS_I2C_NAME "capella-ps"
#define CAPELLA_DEVICE_FILE_NAME "capella_ioctl"
#define CAPELLA_ALS_DEVICE_INPUT_NAME "lightinput"
#define CAPELLA_PROX_DEVICE_INPUT_NAME "proximityinput"

/* IOCTLs*/
#define CAPELLAIO				0x61

#define CAPELLA_IOCTL_LIGHT_ENABLE				_IO(CAPELLAIO, 1)
#define CAPELLA_IOCTL_LIGHT_DISABLE				_IO(CAPELLAIO, 2)
#define CAPELLA_IOCTL_PROXIMITY_ENABLE				_IO(CAPELLAIO, 3)
#define CAPELLA_IOCTL_PROXIMITY_DISABLE				_IO(CAPELLAIO, 4)
#define CAPELLA_IOCTL_LIGHT_SET_DELAY				_IOW(CAPELLAIO, 5, int)
#define CAPELLA_IOCTL_PROX_SET_DELAY				_IOW(CAPELLAIO, 6, int)
#define CAPELLA_IOCTL_IS_POWER				_IOW(CAPELLAIO, 7, int)
#define CAPELLA_IOCTL_SET_PROX_CAL				_IOW(CAPELLAIO, 8, int)



struct CAPELLA_platform_data {
 int (*get_adc)(void);
 void (*power_enable)(int);
 int vo_gpio;
 int prox_mode;
 int enable;
};

extern void capella_power_reset(void);

#endif
