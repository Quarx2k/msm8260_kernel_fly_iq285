//
// Definitions for amosense magnetic field sensor chips (ALPS0303H, ALPS0303M)
//

#ifndef		ALPS0303_H
#define		ALPS0303_H

#include <linux/ioctl.h>

// IOCTLs for ALPS0303
#define		ALPS_MAG_IO										0x50
#define		ALPS_IOCTL_SENSOR_SET_DELAY					_IOW(ALPS_MAG_IO, 1, int)
#define		ALPS_IOCTL_MAG_ENABLE							_IO(ALPS_MAG_IO, 2)
#define		ALPS_IOCTL_MAG_DISABLE						_IO(ALPS_MAG_IO, 3)
#define		ALPS_IOCTL_ACC_ENABLE							_IO(ALPS_MAG_IO, 4)
#define		ALPS_IOCTL_ACC_DISABLE						_IO(ALPS_MAG_IO, 5)
#define		ALPS_IOCTL_COMPASS_ENABLE					_IO(ALPS_MAG_IO, 6)
#define		ALPS_IOCTL_COMPASS_DISABLE					_IO(ALPS_MAG_IO, 7)
#define		ALPS_IOCTL_IS_POWER					_IOW(ALPS_MAG_IO, 8, int)

extern void alps_power_reset(void);

#endif		// ALPS0303_H
