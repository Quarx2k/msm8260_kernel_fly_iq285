/*
* Customer code to add GPIO control during WLAN start/stop
* Copyright (C) 1999-2010, Broadcom Corporation
* 
*      Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2 (the "GPL"),
* available at http://www.broadcom.com/licenses/GPLv2.php, with the
* following added to such license:
* 
*      As a special exception, the copyright holders of this software give you
* permission to link this software with independent modules, and to copy and
* distribute the resulting executable under terms of your choice, provided that
* you also meet, for each linked independent module, the terms and conditions of
* the license of that module.  An independent module is a module which is not
* derived from this software.  The special exception does not apply to any
* modifications of the software.
* 
*      Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a license
* other than the GPL, without Broadcom's express prior written consent.
*
* $Id: dhd_custom_gpio.c,v 1.1.4.8.4.1 2010/09/02 23:13:16 Exp $
*/


#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>
#include <bcmutils.h>

#include <dngl_stats.h>
#include <dhd.h>

#include <wlioctl.h>
#include <wl_iw.h>

#include <mach/pmic.h>
#include <mach/vreg.h>

#define WL_ERROR(x) printf x
#ifdef KTTECH_WIFI_BCM4329_MSG
#define WL_TRACE(x) printf x
#else
#define WL_TRACE(x)
#endif

#include <asm/gpio.h>

#include <mach/mpp.h>
#define MPP_WIFI_RESET_N 20

#ifdef CONFIG_KTTECH_WIFI_BCM4329
// KTT_UPDATE : 20101030 powerkss ADD for nv mac addr
#define GET_CUSTOM_MAC_ENABLE
#endif

#ifdef CUSTOMER_HW
extern  void bcm_wlan_power_off(int);
extern  void bcm_wlan_power_on(int);
#endif /* CUSTOMER_HW */
#ifdef CUSTOMER_HW2
int wifi_set_carddetect(int on);
int wifi_set_power(int on, unsigned long msec);
int wifi_get_irq_number(unsigned long *irq_flags_ptr);
#endif
#ifdef CONFIG_KTTECH_WIFI_BCM4329
extern void bcm4329_ldo_pwr(int device, int on);
extern int bcm4329_wifi_reset(int on);
extern int bcm4329_wifi_wow_en(int on);
#endif
//static int dhd_wl_reset_gpio = CONFIG_BCM4329_WL_RESET_GPIO;
#if defined(OOB_INTR_ONLY)

#if defined(BCMLXSDMMC)
extern int sdioh_mmc_irq(int irq);
#endif /* (BCMLXSDMMC)  */

#ifdef CUSTOMER_HW3
#include <mach/gpio.h>
#endif

/* Customer specific Host GPIO defintion  */
static int dhd_oob_gpio_num = -1; /* GG 19 */

module_param(dhd_oob_gpio_num, int, 0644);
MODULE_PARM_DESC(dhd_oob_gpio_num, "DHD oob gpio number");

int dhd_customer_oob_irq_map(unsigned long *irq_flags_ptr)
{
	int  host_oob_irq = 0;

#ifdef CUSTOMER_HW2
	host_oob_irq = wifi_get_irq_number(irq_flags_ptr);

#else /* for NOT  CUSTOMER_HW2 */
#if defined(CUSTOM_OOB_GPIO_NUM)
	if (dhd_oob_gpio_num < 0) {
		dhd_oob_gpio_num = CUSTOM_OOB_GPIO_NUM;
	}
#endif
	*irq_flags_ptr = IRQF_TRIGGER_FALLING;
	if (dhd_oob_gpio_num < 0) {
		WL_ERROR(("%s: ERROR customer specific Host GPIO is NOT defined \n",
			__FUNCTION__));
		return (dhd_oob_gpio_num);
	}

#if defined CUSTOMER_HW  || defined(CONFIG_KTTECH_WIFI_BCM4329)
	host_oob_irq = MSM_GPIO_TO_INT(dhd_oob_gpio_num);
#elif defined CUSTOMER_HW3
	gpio_request(dhd_oob_gpio_num, "oob irq");
	host_oob_irq = gpio_to_irq(dhd_oob_gpio_num);
	gpio_direction_input(dhd_oob_gpio_num);
#endif /* CUSTOMER_HW */
#endif /* CUSTOMER_HW2 */


	return (host_oob_irq);
}
#endif /* defined(OOB_INTR_ONLY) */

/* Customer function to control hw specific wlan gpios */
void
dhd_customer_gpio_wlan_ctrl(int onoff)
{
	switch (onoff) {
		case WLAN_RESET_OFF:
			WL_TRACE(("%s: call customer specific GPIO to insert WLAN RESET\n",
				__FUNCTION__));
#ifdef CUSTOMER_HW
			bcm_wlan_power_off(2);
#endif /* CUSTOMER_HW */
#ifdef CUSTOMER_HW2
			wifi_set_power(0, 0);
#endif

#ifdef CONFIG_KTTECH_WIFI_BCM4329
            bcm4329_wifi_reset(0);
            bcm4329_wifi_wow_en(0);
	        msleep(1);
	        bcm4329_ldo_pwr(0x01,0);
   	        msleep(1);
#endif
			WL_ERROR(("=========== WLAN placed in RESET ========\n"));
		break;

		case WLAN_RESET_ON:
			WL_TRACE(("%s: callc customer specific GPIO to remove WLAN RESET\n",
				__FUNCTION__));
#ifdef CUSTOMER_HW
			bcm_wlan_power_on(2);
#endif /* CUSTOMER_HW */
#ifdef CUSTOMER_HW2
			wifi_set_power(1, 0);
#endif
#ifdef CONFIG_KTTECH_WIFI_BCM4329
            bcm4329_ldo_pwr(0x01,1);
			msleep(10);
            bcm4329_wifi_reset(1);
	        msleep(1);
            bcm4329_wifi_wow_en(1);
			msleep(3);
#endif
			WL_ERROR(("=========== WLAN going back to live  ========\n"));
		break;

		case WLAN_POWER_OFF:
			WL_TRACE(("%s: call customer specific GPIO to turn off WL_REG_ON\n",
				__FUNCTION__));
#ifdef CUSTOMER_HW
			bcm_wlan_power_off(1);
#endif /* CUSTOMER_HW */

#ifdef CONFIG_KTTECH_WIFI_BCM4329
            bcm4329_wifi_reset(0);
            bcm4329_wifi_wow_en(0);
			msleep(1);
   		    bcm4329_ldo_pwr(0x01,0);
			msleep(1);
#endif
		break;

		case WLAN_POWER_ON:
			WL_TRACE(("%s: call customer specific GPIO to turn on WL_REG_ON\n",
				__FUNCTION__));
#ifdef CUSTOMER_HW
			bcm_wlan_power_on(1);
#endif /* CUSTOMER_HW */

#ifdef CONFIG_KTTECH_WIFI_BCM4329
   		    bcm4329_ldo_pwr(0x01,1);
			msleep(10);
            bcm4329_wifi_reset(1);
            msleep(5);
            bcm4329_wifi_wow_en(1);
			msleep(3);
			/* Lets customer power to get stable */
#endif
		break;
	}
}

#ifdef GET_CUSTOM_MAC_ENABLE
#ifdef CONFIG_KTTECH_WIFI_BCM4329
 // KTT_UPDATE : 20101030 powerkss ADD for nv mac addr	
#define ATH_MAC_LEN                         6
#define ATH_SOFT_MAC_TMP_BUF_LEN            64
char *softmac_file = "/data/misc/wifi/softmac";


/*------------------------------------------------------------------*/
/*
 * Input an Ethernet address and convert to binary.
 */
/* soft mac */
static int
wmic_ether_aton(const char *orig, size_t len, unsigned char *eth)
{
  const char *bufp;
  int i;

  i = 0;
  for(bufp = orig; bufp!=orig+len && *bufp; ++bufp) {
    unsigned int val;
    unsigned char c = *bufp++;
    if (c >= '0' && c <= '9') val = c - '0';
    else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
    else {
        printk("%s: MAC value is invalid\n", __FUNCTION__);
        break;
    }

    val <<= 4;
    c = *bufp++;
    if (c >= '0' && c <= '9') val |= c - '0';
    else if (c >= 'a' && c <= 'f') val |= c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') val |= c - 'A' + 10;
    else {
        printk("%s: MAC value is invalid\n", __FUNCTION__);
        break;
    }

    eth[i] = (unsigned char) (val & 0377);
    if(++i == ATH_MAC_LEN) {
        return 1;
    }
    if (*bufp != ':')
        break;
  }
  return 0;
}

static int read_mac_addr_from_file(char *mac_addr)
{
        mm_segment_t        oldfs;
        struct file     *filp;
        struct inode        *inode = NULL;
        int         length;
        unsigned char soft_mac_tmp_buf[ATH_SOFT_MAC_TMP_BUF_LEN];

        /* open file */
        oldfs = get_fs();
        set_fs(KERNEL_DS);
        filp = filp_open(softmac_file, O_RDONLY, S_IRUSR);

    WL_TRACE(("%s try to open file %s\n", __FUNCTION__, softmac_file));

        if (IS_ERR(filp)) {
            printk("%s: file %s filp_open error\n", __FUNCTION__, softmac_file);
            set_fs(oldfs);
        return -1;
        }

        if (!filp->f_op) {
            printk("%s: File Operation Method Error\n", __FUNCTION__);
            filp_close(filp, NULL);
            set_fs(oldfs);
        return -1;
        }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
        inode = filp->f_path.dentry->d_inode;
#else
        inode = filp->f_dentry->d_inode;
#endif
        if (!inode) {
            printk("%s: Get inode from filp failed\n", __FUNCTION__);
            filp_close(filp, NULL);
            set_fs(oldfs);
        return -1;
        }

    WL_TRACE(("%s file offset opsition: %xh\n", __FUNCTION__, (unsigned)filp->f_pos));

        /* file's size */
        length = i_size_read(inode->i_mapping->host);
    WL_TRACE(("%s: length=%d\n", __FUNCTION__, length));
        if (length+1 > ATH_SOFT_MAC_TMP_BUF_LEN) {
            printk("%s: MAC file's size is not as expected\n", __FUNCTION__);
            filp_close(filp, NULL);
            set_fs(oldfs);
        return -1;
        }

        /* read data */
        if (filp->f_op->read(filp, soft_mac_tmp_buf, length, &filp->f_pos) != length) {
            printk("%s: file read error\n", __FUNCTION__);
            filp_close(filp, NULL);
            set_fs(oldfs);
        return -1;
        }
        soft_mac_tmp_buf[length] = '\0'; /* ensure that it is NULL terminated */
#if 0
        /* the data we just read */
        printk("%s: mac address from the file:\n", __FUNCTION__);
        for (i = 0; i < length; i++)
            printk("[%c(0x%x)],", soft_mac_tmp_buf[i], soft_mac_tmp_buf[i]);
        printk("\n");
#endif

        /* read data out successfully */
        filp_close(filp, NULL);
        set_fs(oldfs);

        /* convert mac address */
        if (!wmic_ether_aton(soft_mac_tmp_buf, length, mac_addr)) {
            printk("%s: convert mac value fail\n", __FUNCTION__);
        return -1;
        }

#if 0
        /* the converted mac address */
        printk("%s: the converted mac value\n", __FUNCTION__);
        for (i = 0; i < ATH_MAC_LEN; i++)
            printk("[0x%x],", mac_addr[i]);
        printk("\n");
#endif
    return 0;
}
#endif /* CONFIG_KTTECH_WIFI_BCM4329 */

/* Function to get custom MAC address */
int
dhd_custom_get_mac_address(unsigned char *buf)
{
    unsigned char soft_mac_addr[ATH_MAC_LEN];

	WL_TRACE(("%s Enter\n", __FUNCTION__));
	if (!buf)
		return -EINVAL;

	/* Customer access to MAC address stored outside of DHD driver */
#ifdef CONFIG_KTTECH_WIFI_BCM4329	
  // KTT_UPDATE : 20101030 powerkss ADD for nv mac addr	
  {
    if (read_mac_addr_from_file(soft_mac_addr) != 0) {
      printk("%s: read_mac_addr_from_file failed\n", __FUNCTION__);
      return -1;
    }
#endif    

    memcpy(buf, soft_mac_addr, ATH_MAC_LEN);
    //jhkim 100811: soft mac에서 읽어온 값이 00:11:22:33:44:55(acsii:0:17:34:51:68:85) 인경우는 무시하고 rom에서 mac을 읽어오도록 한다.
    if((soft_mac_addr[0] == 0 && soft_mac_addr[1] == 0 && soft_mac_addr[2] == 0 && soft_mac_addr[3] == 0 && soft_mac_addr[4] == 0 && soft_mac_addr[5] == 0) 
    || (soft_mac_addr[0] == 0 && soft_mac_addr[1] == 17 && soft_mac_addr[2] == 34 && soft_mac_addr[3] == 51 && soft_mac_addr[4] == 68 && soft_mac_addr[5] == 85))
    {
      return -1;        
    }
  }
    
#ifdef EXAMPLE_GET_MAC
	/* EXAMPLE code */
	{
		struct ether_addr ea_example = {{0x00, 0x11, 0x22, 0x33, 0x44, 0xFF}};
		bcopy((char *)&ea_example, buf, sizeof(struct ether_addr));
	}
#endif /* EXAMPLE_GET_MAC */

	return 0;
}
#endif /* GET_CUSTOM_MAC_ENABLE */
