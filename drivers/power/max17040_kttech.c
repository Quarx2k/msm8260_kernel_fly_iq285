#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

void kttech_battery_gauge_init(void);
int kttech_battery_gauge_get_soc(void);
int kttech_battery_gauge_get_voltage(void);
int kttech_battery_gauge_get_current(void);
int kttech_battery_gauge_get_temperature(void);

#ifdef CONFIG_KTTECH_BATTERY_GAUGE_MAXIM

static int maxim_read_reg(u8 regoffset, u8 *value);

#define MAX17410_SLAVE_ADDR			0x36
#define MAX17410_QUP_I2C_BUS_ID	10

#define MAX17410_I2C_RETRY_NUM		16

#define MAX17040_VCELL_MSB	0x02
#define MAX17040_VCELL_LSB	0x03
#define MAX17040_SOC_MSB	0x04
#define MAX17040_SOC_LSB	0x05
#define MAX17040_MODE_MSB	0x06
#define MAX17040_MODE_LSB	0x07
#define MAX17040_VER_MSB	0x08
#define MAX17040_VER_LSB	0x09
#define MAX17040_RCOMP_MSB	0x0C
#define MAX17040_RCOMP_LSB	0x0D
#define MAX17040_CMD_MSB	0xFE
#define MAX17040_CMD_LSB	0xFF


static int maxim_read_reg(u8 regoffset, u8 *value)
{
	struct i2c_adapter *adap;
	int ret;
	struct i2c_msg msg[2];

	adap = i2c_get_adapter(MAX17410_QUP_I2C_BUS_ID);
	if (!adap)
		return -ENODEV;

	/* setup the address to read */
	msg[0].addr = MAX17410_SLAVE_ADDR;
	msg[0].len = 1;
	msg[0].flags = 0;
	msg[0].buf = &regoffset;

	/* setup the read buffer */
	msg[1].addr = MAX17410_SLAVE_ADDR;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 2;
	msg[1].buf = value;

	ret = i2c_transfer(adap, msg, 2);

	return (ret >= 0) ? 0 : ret;
}

int kttech_battery_gauge_get_voltage(void)
{
	static int voltage_err_count = 0;
	u8 voltage[2];
	int vcell;
	int err;
	int i = 0;

	for(i = 0; i < MAX17410_I2C_RETRY_NUM; i++)
	{
		err = maxim_read_reg(MAX17040_VCELL_MSB, &voltage[0]);
		if(err >= 0) {
			vcell = (((voltage[0] << 4) + (voltage[1] >> 4)) * 5) >> 2;
			/* printk(KERN_INFO "kttech-charger %s: %d %x %x\n", __func__, vcell, voltage[0], voltage[1]); */
			return vcell;
		}
		voltage_err_count++;
	}	

	printk(KERN_INFO "kttech-charger I2C failed.. %d\n", voltage_err_count);
	return err;
}

int kttech_battery_gauge_get_current(void)
{
	return 0;
}

int kttech_battery_gauge_get_soc(void)
{
	static int soc_err_count = 0;
	u8 soc[2];
	int SOCValue;
	int DisplayedSOC;
	int err;
	int i = 0;

	for(i = 0; i < MAX17410_I2C_RETRY_NUM; i++)
	{
		err = maxim_read_reg(MAX17040_SOC_MSB, &soc[0]);
		if(err >= 0) {
			SOCValue = (( (soc[0] << 8) + soc[1] ) );
			DisplayedSOC = (SOCValue - 256) / 500;
			if(DisplayedSOC > 100)
				DisplayedSOC = 100;
			else if(DisplayedSOC < 0)
				DisplayedSOC = 0;

			/* printk(KERN_INFO "kttech-charger %s: %d %x %x\n", __func__, DisplayedSOC, soc[0], soc[1]); */
			return DisplayedSOC;
		}
		soc_err_count++;
	}

	printk(KERN_INFO "kttech-charger I2C failed. %d\n", soc_err_count);
	return err;
}

int kttech_battery_gauge_get_temperature(void)
{
	return 30;
}

void kttech_battery_gauge_init(void)
{

}
#endif