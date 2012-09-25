/*******************************************************************************************
File: dmbdrv.c
Description: DMB Device Driver
Writer: KTTECH SW1 khkim
LastUpdate: 2010-06-30
*******************************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/smp_lock.h>
#include <linux/jiffies.h>
#include <asm/uaccess.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/ioctl.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <asm/unistd.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include "INC_INCLUDES.h"
#include "dmbdrv_kttech.h"
#include <asm/io.h>
#include <linux/pmic8058-othc.h>
#include <linux/regulator/consumer.h>
#include <mach/pmic.h>
#include <linux/spi/spi.h>

#if 0
#define DEVLOG(fmt, ...) printk(KERN_DEBUG "### <DMB>" fmt ,##__VA_ARGS__)
#else
#define DEVLOG(fmt, ...)
#endif

//#define TDMB_I2C_INTERFACE
#define TDMB_SPI_INTERFACE
//#define TDMB_SPI_INTERFACE_TEST

#define DMB_MAJOR 260

#define DMB_GPIO_DEMOD_EN_N                126
#define DMB_GPIO_RESET_N                         127
#define DMB_GPIO_INT_N                              94

#define TDMB_DEV_NAME       "tdmb"
#define TDMB_DRV_NAME       "dmbdrv"
#define TDMB_CLASS_NAME   "dmbclass"
#define TDMB_I2c_SLAVE_N 0x40

#define KX_DMB_LNA_GAIN 0
#define KXDMB_RSSI_LEVEL_0_MAX	(85)
#define KXDMB_RSSI_LEVEL_1_MAX	(78)
#define KXDMB_RSSI_LEVEL_2_MAX	(74)
#define KXDMB_RSSI_LEVEL_3_MAX	(71)
#define KXDMB_RSSI_LEVEL_4_MAX	(67)
#define KXDMB_RSSI_LEVEL_5_MAX	(63)
#define KXDMB_RSSI_LEVEL_6_MAX	(59)
typedef enum
{
  KXDMB_RSSI_LEVEL_0,	// - (94) dBm
  KXDMB_RSSI_LEVEL_1,	// - (86) dBm
  KXDMB_RSSI_LEVEL_2,	// - (78) dBm
  KXDMB_RSSI_LEVEL_3,	// - (70) dBm
  KXDMB_RSSI_LEVEL_4,	// - (62) dBm
  KXDMB_RSSI_LEVEL_5,	// - (54) dBm
  KXDMB_RSSI_LEVEL_6,	// - (46) dBm
  KXDMB_RSSI_LEVEL_MAX,
} KTFT_TDMB_RSSI_LEVEL_T; 

#define INT_OCCUR_SIG		0x0A		// [S5PV210_Kernel], 20101220, ASJ, 
#define DIRECT_OUT_SIG		0x01

struct ST_TDMB_Interrupt{
	int tdmb_irq;
	struct completion comp;
};
static struct ST_TDMB_Interrupt g_stTdmb_Int;

struct task_struct* g_pTS;
unsigned int  g_nThreadcnt = 0;
wait_queue_head_t WaitQueue_Read;
static unsigned char ReadQ;
static ssize_t dmbdev_read (struct file *file, char __user *buf, size_t count, loff_t *offset);
static ssize_t dmbdev_write (struct file *file, const char __user *buf, size_t count, loff_t *offset);
static long dmbdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int dmbdev_open(struct inode *inode, struct file *file);
static int dmbdev_release(struct inode *inode, struct file *file);
static int set_ch_data(DMB_CH_DATA_S* pchbuf, INC_CHANNEL_INFO* pincch, int cnt);
static int get_rssi_level(unsigned int rssi);
static void tdmb_gpios_free(const struct msm_gpio *table, int size);
static int tdmb_gpios_disable(const struct msm_gpio *table, int size);
static int tdmb_power(int on);

static struct class *dmbclass;
static struct device *dmbdev;
static DMB_CH_DATA_S *found_channel=NULL;
#ifdef TDMB_I2C_INTERFACE
static struct i2c_adapter* dmbi2c_adap;
#endif
#ifdef TDMB_SPI_INTERFACE
extern struct spi_device *gInc_spi;
unsigned char g_StreamBuff[INC_INTERRUPT_SIZE] = {0};
static char start_service = 0;
#endif

static struct msm_gpio tdmb_gpio_config_data[] = {
#ifdef TDMB_SPI_INTERFACE
  { GPIO_CFG(94, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA), "tdmb_irq" },
#endif	
  { GPIO_CFG(126, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA), "tdmb_reset" },
  { GPIO_CFG(127, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA), "tdmb_dem_enable" },
};

int tdmb_gpios_enable(const struct msm_gpio *table, int size)
{
  int rc;
  int i;
  const struct msm_gpio *g;
  for (i = 0; i < size; i++) {
  	g = table + i;
  	rc = gpio_tlmm_config(g->gpio_cfg, GPIO_CFG_ENABLE);
  	if (rc) {
  		pr_err("gpio_tlmm_config(0x%08x, GPIO_CFG_ENABLE)"
  		       " <%s> failed: %d\n",
  		       g->gpio_cfg, g->label ?: "?", rc);
  		pr_err("pin %d func %d dir %d pull %d drvstr %d\n",
  		       GPIO_PIN(g->gpio_cfg), GPIO_FUNC(g->gpio_cfg),
  		       GPIO_DIR(g->gpio_cfg), GPIO_PULL(g->gpio_cfg),
  		       GPIO_DRVSTR(g->gpio_cfg));
  		goto err;
  	}
  }
  return 0;
  err:
    tdmb_gpios_disable(table, i);
  return rc;
}

int tdmb_gpios_disable(const struct msm_gpio *table, int size)
{
  int rc = 0;
  int i;
  const struct msm_gpio *g;
  for (i = size-1; i >= 0; i--) {
  	int tmp;
  	g = table + i;
  	tmp = gpio_tlmm_config(g->gpio_cfg, GPIO_CFG_DISABLE);
  	if (tmp) {
  		pr_err("gpio_tlmm_config(0x%08x, GPIO_CFG_DISABLE)"
  		       " <%s> failed: %d\n",
  		       g->gpio_cfg, g->label ?: "?", rc);
  		pr_err("pin %d func %d dir %d pull %d drvstr %d\n",
  		       GPIO_PIN(g->gpio_cfg), GPIO_FUNC(g->gpio_cfg),
  		       GPIO_DIR(g->gpio_cfg), GPIO_PULL(g->gpio_cfg),
  		       GPIO_DRVSTR(g->gpio_cfg));
  		if (!rc)
  			rc = tmp;
  	}
  }
  return rc;
}

int tdmb_gpios_request(const struct msm_gpio *table, int size)
{
  int rc;
  int i;
  const struct msm_gpio *g;
  for (i = 0; i < size; i++) {
  	g = table + i;
  	rc = gpio_request(GPIO_PIN(g->gpio_cfg), g->label);
  	if (rc) {
  		pr_err("gpio_request(%d) <%s> failed: %d\n",
  		       GPIO_PIN(g->gpio_cfg), g->label ?: "?", rc);
  		goto err;
  	}
  }
  return 0;
  err:
    tdmb_gpios_free(table, i);
  return rc;
}

void tdmb_gpios_free(const struct msm_gpio *table, int size)
{
  int i;
  const struct msm_gpio *g;
  for (i = size-1; i >= 0; i--) {
  	g = table + i;
  	gpio_free(GPIO_PIN(g->gpio_cfg));
  }
}

int tdmb_gpios_request_enable(const struct msm_gpio *table, int size)
{
  int rc = tdmb_gpios_request(table, size);
  if (rc)
  	return rc;
  rc = tdmb_gpios_enable(table, size);
  if (rc)
  	tdmb_gpios_free(table, size);
  return rc;
}

void tdmb_gpios_disable_free(const struct msm_gpio *table, int size)
{
  tdmb_gpios_disable(table, size);
  tdmb_gpios_free(table, size);
}

static int tdmb_gpio_setup(void)
{
  int rc;

  rc = tdmb_gpios_request_enable(tdmb_gpio_config_data,
  ARRAY_SIZE(tdmb_gpio_config_data));

  return rc;
}

static void tdmb_gpio_teardown(void)
{
  tdmb_gpios_disable_free(tdmb_gpio_config_data,
  ARRAY_SIZE(tdmb_gpio_config_data));
}

static struct tdmb_platform_data tdmb_pdata = {
  .power  = tdmb_power,
  .setup    = tdmb_gpio_setup,
  .teardown = tdmb_gpio_teardown,
  .reset = DMB_GPIO_RESET_N,
  .demod_enable = DMB_GPIO_DEMOD_EN_N,
};

static void tdmb_reset(void)
{
  gpio_set_value(tdmb_pdata.reset, 1);
  msleep(5);
  gpio_set_value(tdmb_pdata.reset, 0);
  msleep(5);
  gpio_set_value(tdmb_pdata.reset, 1);
  msleep(5);    
  DEVLOG(" reset inc chip \n");
}

static void tdmb_demod_enable(int enable)
{
  gpio_set_value(tdmb_pdata.demod_enable, enable);
  msleep(10);
}

static struct regulator *dmb_8058_l10; //2.6V Main
static int tdmb_power(int on)
{
  int rc = 0;

  if(on) {

    dmb_8058_l10 = regulator_get(NULL, "8058_l10");
    if (IS_ERR(dmb_8058_l10)) {
    	pr_err("%s: regulator_get(8058_l10) failed (%d)\n",
    			__func__, rc);
    return PTR_ERR(dmb_8058_l10);
    }
    //set voltage level
    rc = regulator_set_voltage(dmb_8058_l10, 2600000, 2600000);
    if (rc)
    { 
      pr_err("%s: regulator_set_voltage(8058_l10) failed (%d)\n",
      __func__, rc);
      regulator_put(dmb_8058_l10);
      return rc;
    }

    //enable output
    rc = regulator_enable(dmb_8058_l10);
    if (rc)
    { 
      pr_err("%s: regulator_enable(8058_l10) failed (%d)\n", __func__, rc);
      regulator_put(dmb_8058_l10);
      return rc;
    }

    tdmb_demod_enable(on);
    tdmb_reset();
  }
  else {
    if (!dmb_8058_l10)
      return rc;

    rc = regulator_disable(dmb_8058_l10);
    if (rc)
    { 
      pr_err("%s: regulator_disable(8058_l10) failed (%d)\n",
      __func__, rc);
      regulator_put(dmb_8058_l10);
      return rc;
    }
    regulator_put(dmb_8058_l10);
    dmb_8058_l10 = NULL;
  	gpio_set_value_cansleep(tdmb_pdata.reset, 0);
  	gpio_set_value_cansleep(tdmb_pdata.demod_enable, 0);
  	gpio_set_value_cansleep(DMB_GPIO_INT_N, 0);
  }

  msleep(10);    
  return 0;
}

#ifdef TDMB_SPI_INTERFACE
int INC_dump_thread(void *kthread)
{
	long nTimeStamp = 0;
	unsigned char  	bFirstLoop = 1;
	unsigned int   	nTickCnt = 0;
	unsigned short	nDataLength = 0, nStatus = 0;
	ST_BBPINFO*		pInfo;
	
	g_nThreadcnt++;
	DEVLOG("\n %s : INC_dump_thread start [%d]===========>> \r\n",
		__func__, g_nThreadcnt);


	while (!kthread_should_stop())
	{
		
		if(!start_service)	break;

		if(bFirstLoop)
		{
			nTickCnt = 0;
			bFirstLoop = 0;
			INTERFACE_INT_ENABLE(TDMB_I2C_ID80,INC_MPI_INTERRUPT_ENABLE); 
			INTERFACE_INT_CLEAR(TDMB_I2C_ID80, INC_MPI_INTERRUPT_ENABLE);
			INC_INIT_MPI(TDMB_I2C_ID80);	
			DEVLOG(" %s : nTickCnt(%d) INC_INIT_MPI : [ FirstLoop ] !!!!!! \r\n", 
				 __func__, nTickCnt);

			init_completion(&g_stTdmb_Int.comp);
		}

		if(!start_service)	break;
#ifdef INTERRUPT_METHOD
		if(!wait_for_completion_timeout(&g_stTdmb_Int.comp, 1000*HZ/1000)){ // 1000msec 
			DEVLOG(" INTR TimeOut : nTickCnt[%d]\r\n", nTickCnt);
			bFirstLoop = 1;
			continue;
		}
#else
		if(!INTERFACE_INT_CHECK(TDMB_I2C_ID80)){
			INC_DELAY(5);
			continue;
		}
#endif

		if(!start_service)	break;
		////////////////////////////////////////////////////////////////
		// Read the dump size
		////////////////////////////////////////////////////////////////
		nDataLength = INC_CMD_READ(TDMB_I2C_ID80, APB_MPI_BASE+ 0x08);
		if( nDataLength & 0x4000 ){
			bFirstLoop = 1;
			DEVLOG("==> FIFO FULL   : 0x%X nTickCnt(%d)\r\n", nDataLength, nTickCnt);
			continue;
		}
		else if( !(nDataLength & 0x3FFF ))	{
			nDataLength = 0;
			INTERFACE_INT_CLEAR(TDMB_I2C_ID80, INC_MPI_INTERRUPT_ENABLE);
			DEVLOG("==> FIFO Empty   : 0x%X nTickCnt(%d)\r\n", nDataLength, nTickCnt);
			continue;
		}
		else{
			nDataLength &= 0x3FFF;
			nDataLength = INC_INTERRUPT_SIZE;
		}

		if(!start_service)	break;
		////////////////////////////////////////////////////////////////
		// dump the stream
		////////////////////////////////////////////////////////////////
		if(nDataLength >0){
			INC_CMD_READ_BURST(TDMB_I2C_ID80, APB_STREAM_BASE, g_StreamBuff, INC_INTERRUPT_SIZE);
			ReadQ = INT_OCCUR_SIG; 
			DEVLOG("==> BURST DATA READ   : 0x%X nTickCnt(%d)\r\n", nDataLength, nTickCnt);
		}

		nTickCnt++;
		INTERFACE_INT_CLEAR(TDMB_I2C_ID80, INC_MPI_INTERRUPT_ENABLE);

		wake_up_interruptible(&WaitQueue_Read);
	} 

	ReadQ = DIRECT_OUT_SIG;
	wake_up_interruptible(&WaitQueue_Read);
	
	g_nThreadcnt--;
	DEVLOG( " %s : INC_dump_thread end [%d]=============<< \r\n\r\n",
					 __func__, g_nThreadcnt);
	return 0;
}

static irqreturn_t tdmb_interrupt(int irq, void *dev_id)
{
    if(start_service)
	complete(&g_stTdmb_Int.comp);

    DEVLOG(" tdmb_interrupt....\n");
	return IRQ_HANDLED;
}
#endif

static ssize_t dmbdev_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
#ifdef TDMB_SPI_INTERFACE
  int res = -1;
  unsigned int 	nReadSize = 0;

  if(count > INC_INTERRUPT_SIZE)
  {
    DEVLOG( "==> SEND TO UI  FAIL 1 \n");  
  	return -EMSGSIZE;
  }  

  if(count <= 0 )
  {
    DEVLOG("==> SEND TO UI  FAIL 2 \n");    
    return 0;
  }  

  DEVLOG("==> SEND TO UI  SUCCESS \n");
    
  nReadSize = count & 0xFFFF;
  res = copy_to_user(buf, g_StreamBuff, INC_INTERRUPT_SIZE);
  if (res > 0) {
  	res = -EFAULT;
  	DEVLOG( "[%s] %s : Error!! \n", __FILE__, __func__);
  	return -1;
  }

  return nReadSize;
#else
  return 0;
#endif
}

static ssize_t dmbdev_write (struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
  DEVLOG("dmbdrv write....\n");
  return 0;
}

int dmbdev_poll( struct file *filp, poll_table *wait )
{
	int mask = 0;
	poll_wait( filp, &WaitQueue_Read, wait );

	if(ReadQ == INT_OCCUR_SIG){
		mask |= (POLLIN);
	}else if(ReadQ != 0){
		mask |= POLLERR;
	}else{
	}

	ReadQ = 0x00;
	return mask;
}	

static long dmbdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
  int ret;

  switch(cmd)
  {
  	case DMB_IOCTL_INIT:
     {
          tdmb_pdata.power(1);
          INC_DRIVER_OPEN();
          start_service = 0;
          
          ret = INTERFACE_INIT(TDMB_I2C_ID80);

          if(ret != INC_SUCCESS)
          {
            DEVLOG(" Error: INTERFACE_INIT err..ret=%d\n", ret);
            return -1;
          }

          if(arg==1) // channel search mode
          {
            DEVLOG("INTERFACE_INIT: DB init\n");
            INTERFACE_DBINIT();
          }

#ifdef TDMB_I2C_INTERFACE
          dmbi2c_adap->timeout = 100;
          dmbi2c_adap->retries = 3;
#endif      
      }
      return 0;
  		
  	case DMB_IOCTL_SCAN_ENSEMBLE:
      {
        unsigned int freq;
        int found_cnt;
        ensemble_scan_req_t *pscan=NULL;
        INC_CHANNEL_INFO *inc_chs=NULL;

        pscan = (ensemble_scan_req_t*)kmalloc(sizeof(*pscan), GFP_KERNEL);
        if(!pscan)
        {
          DEVLOG("Error: memory allocation error.\n");
          ret = -1;
          goto SCAN_ERR;
        }
        ret = copy_from_user(pscan, (void*)arg, sizeof(ensemble_scan_req_t));
        if(ret)
          goto SCAN_ERR;

        freq = INC_GET_KOREABAND_FULL_TABLE(pscan->freq);
        if(freq == 0xffff) 
        {
          DEVLOG(" invalid freq index=%d\n", freq);
          ret = -1;
          goto SCAN_ERR;
        }

        inc_chs = kmalloc(sizeof(INC_CHANNEL_INFO)*MAX_ENSEMBLE_CHANNEL, GFP_KERNEL);
        if(!inc_chs)
        {
          DEVLOG(" Memory allocation error\n");
          ret = -1;
          goto SCAN_ERR;
        }

        found_cnt = 0;
        if(!INTERFACE_SCAN(TDMB_I2C_ID80, inc_chs, MAX_ENSEMBLE_CHANNEL, &found_cnt, freq))
        {
          DEVLOG(" scan fail... freq=%d, error-code=0x%X \n", freq, INTERFACE_ERROR_STATUS(TDMB_I2C_ID80));
          goto SCAN_ERR;
          ret = -1;
        }
        else
        {
          DEVLOG("  scan success freq=%d, found_ch_count=%d \n", freq, found_cnt);
          pscan->channel_count = set_ch_data(pscan->channel_data, inc_chs, found_cnt);
          ret = copy_to_user((void*)arg, pscan, sizeof(*pscan));
          if(ret)
            goto SCAN_ERR;
        }

        SCAN_ERR:				
        if(inc_chs)
        {
          kfree(inc_chs);
        }

        if(pscan)
        {
          kfree(pscan);
        }
        return ret;
      }	
  		
	case DMB_IOCTL_SERVICE_START:
      {
		ST_SUBCH_INFO *chinfo=NULL;
		int start_ret;
		service_start_req_t serv_req;
        INC_UINT32 int_state;
		start_service = 0;
    
		chinfo = (ST_SUBCH_INFO*)kmalloc(sizeof(*chinfo), GFP_KERNEL);
		if(!chinfo)
		{
			DEVLOG(" Error: memory alloc error\n");
			ret = -1;
			goto SERVICE_START_ERR;
		}
		memset(chinfo, 0, sizeof(*chinfo));

		ret = copy_from_user(&serv_req, (void*)arg, sizeof(serv_req));
		if(ret)
		{
			ret = -1;					
			goto SERVICE_START_ERR;
		}
			
		chinfo->nSetCnt = 1;
		chinfo->astSubChInfo[0].ulRFFreq = serv_req.freq;
		chinfo->astSubChInfo[0].ucServiceType = serv_req.serv_type;
		chinfo->astSubChInfo[0].ucSubChID = serv_req.subch_id;
		chinfo->astSubChInfo[0].uiTmID = serv_req.tmid;

		start_ret = INTERFACE_START(TDMB_I2C_ID80, chinfo);
		if(start_ret != INC_SUCCESS)
		{
          INC_ERROR_INFO err;
          err = INTERFACE_ERROR_STATUS(TDMB_I2C_ID80);
          DEVLOG(" INTERFACE_START Error....ret=%d, err=%04X\n", start_ret, err);
          if(err==ERROR_USER_STOP)
            ret = -2;
          else
            ret = -1;
		}	
#ifdef TDMB_SPI_INTERFACE
		else
		{
          DEVLOG(" INTERFACE_START Success ret=%d\n", start_ret);
          start_service = 1;

		  g_pTS = kthread_run(INC_dump_thread, NULL, "kidle_timeout");

			if(IS_ERR(g_pTS)) 
			{
				DEVLOG("[%s] %s : cann't create the INC_dump_thread !!!! \n", __FILE__, __func__);
				return -1;	
			}
          ret = 0;
		}
#endif
        {
            service_start_req_t *preq;
            preq = (service_start_req_t*)arg;
            DEVLOG("  start result=%d \n", ret);
            ret = copy_to_user(&preq->result, &ret, sizeof(ret));
            if(ret)
            goto SERVICE_START_ERR;
  		}

SERVICE_START_ERR:
        if(chinfo)
          kfree(chinfo);
        return ret;
      }
			
	case DMB_IOCTL_SERVICE_END:
		DEVLOG(" service stop...\n");
		INC_STOP(TDMB_I2C_ID80);
		if(g_pTS != NULL && start_service){
			start_service = 0;
            printk("***********DMB: service end = 0x%p ********\n", &g_stTdmb_Int.comp);
            if(&g_stTdmb_Int.comp != NULL)
    			complete(&g_stTdmb_Int.comp);
			msleep(30);
			g_pTS = NULL;
		}
		return 0;
			
    case DMB_IOCTL_GET_PREBER:
      {
        INC_UINT32 val;
        val = INC_GET_PREBER(TDMB_I2C_ID80);
        DEVLOG(" ioctl get preber=%ld\n", val);
        ret = copy_to_user((unsigned char*)arg, &val, sizeof(val));
        if(ret)
        return -1;
      }
      return 0;
      
    case DMB_IOCTL_GET_POSTBER:
      {
        INC_UINT32 val;
        val = INC_GET_POSTBER(TDMB_I2C_ID80);
        DEVLOG(" ioctl get postber=%ld\n", val);
        ret = copy_to_user((unsigned char*)arg, &val, sizeof(val));
        if(ret)
        return -1;
      }
      return 0;  
      
    case DMB_IOCTL_GET_CER:
      {
        INC_UINT32 val;
        val = (INC_UINT32)INC_GET_CER(TDMB_I2C_ID80);
        DEVLOG(" ioctl get cer=%ld\n", val);
        ret = copy_to_user((unsigned char*)arg, &val, sizeof(val));
        if(ret)
        return -1;
      }
      return 0;
      
    case DMB_IOCTL_GET_RSSI:
      {
        INC_UINT32 val;
        val = (INC_UINT32)INC_GET_RSSI(TDMB_I2C_ID80);
        DEVLOG(" ioctl get rssi=%ld\n", val);
        val = get_rssi_level(val);
        ret = copy_to_user((unsigned char*)arg, &val, sizeof(val));
        if(ret)
        return -1;
      }
      return 0;
      
    case DMB_IOCTL_SET_ABORT:
      {
        DEVLOG(" ioctl set abort.......\n");
        INTERFACE_USER_STOP(TDMB_I2C_ID80);
        return 0;
      }
	case DMB_IOCTL_CLOSE:
      {
        tdmb_pdata.power(0);          
        return 0;
      }
  
	case DMB_IOCTL_SET_I2C_RETRY:
      {
        DEVLOG(" set i2c retry=%d\n", arg);
        return 0;
      }
  
	case DMB_IOCTL_SET_I2C_TIMEOUT:
      {
#ifdef TDMB_I2C_INTERFACE
        DEVLOG(" set i2c timeout=%d\n", arg);
        dmbi2c_adap->timeout = arg;
#endif        
        return 0;
      }
  
	case DMB_IOCTL_GET_CHIPID:
      {
        INC_UINT32 nChipID;

        nChipID = (INC_UINT32)INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0x10); // Read Only Register
        DEVLOG(" chip id=%0x\n", nChipID);
        ret = copy_to_user((unsigned char*)arg, &nChipID, sizeof(nChipID));
        if(ret)
        return -1;
      }
        return 0;
   	default:
        DEVLOG(" Error: Unknown command\n");
        break;
  }
  return -1;
}

static int dmbdev_open(struct inode *inode, struct file *file)
{
  int ret;

  DEVLOG(" dmbdrv open...\n");
#ifdef TDMB_SPI_INTERFACE

  if(g_stTdmb_Int.tdmb_irq == NULL)
  {
    g_stTdmb_Int.tdmb_irq = MSM_GPIO_TO_INT(DMB_GPIO_INT_N);

    ret = request_irq(g_stTdmb_Int.tdmb_irq, tdmb_interrupt, IRQF_TRIGGER_FALLING, TDMB_DEV_NAME, NULL);
    if (ret) {
      free_irq(g_stTdmb_Int.tdmb_irq, 0);
      DEVLOG("can't get IRQ %d, ret %d\n", g_stTdmb_Int.tdmb_irq, ret);
    }
  }
  
  g_pTS = NULL;
  g_nThreadcnt = 0;
#endif	
  return 0;
}

static int dmbdev_release(struct inode *inode, struct file *file)
{
   DEVLOG(" dmbdrv release...\n");
   INC_STOP(TDMB_I2C_ID80);
   
#ifdef TDMB_SPI_INTERFACE
   if(start_service){
		start_service = 0;
		complete(&g_stTdmb_Int.comp);
		msleep(30);
   }
   
   if(g_stTdmb_Int.tdmb_irq != NULL)
     free_irq(g_stTdmb_Int.tdmb_irq, NULL);
   
   DEVLOG( "%s : irq[%d] end!! \n", __func__, g_stTdmb_Int.tdmb_irq);

   g_stTdmb_Int.tdmb_irq = NULL;
   g_pTS = NULL;
#endif

  if(found_channel)
  {
    kfree(found_channel);
    found_channel = NULL;
  }
  return 0;
}

int dmbi2c_reg_read(unsigned short reg_addr, unsigned char* buf, int len)
{
#ifdef TDMB_I2C_INTERFACE
  int ret;
  static unsigned short treg;
  struct i2c_msg msg[] = \
  {
  	{
  		.addr = TDMB_I2c_SLAVE_N,
  		.flags = 0,
  		.len = 2,
  		.buf = (unsigned char*)&treg,
  	},
  	{
  		.addr = TDMB_I2c_SLAVE_N,
  		.flags = I2C_M_RD,
  		.len = len,
  		.buf = buf,
  	},
  };

  treg = htons(reg_addr);

  ret = i2c_transfer(dmbi2c_adap, msg, 2);
  if(ret<0)
  {
  	DEVLOG(" Error: i2c reg read error. err=%d\n", ret);
  	return -1;
  }	
  else
  {
  	return len;
  }	
#else
  return 0;
#endif
}

int dmbi2c_reg_readw(unsigned short reg_addr, unsigned short *pvalue)
{
#ifdef TDMB_I2C_INTERFACE
  int ret;
  ret = dmbi2c_reg_read(reg_addr, (unsigned char*)pvalue, 2);
  if(ret<0)
  {
  	return -1;
  }
  else
  {

  	*pvalue = ntohs(*pvalue);
  	return 0;
  }	
#else
  return 0;
#endif
}


int dmbi2c_reg_writew(unsigned short reg_addr, unsigned short value)
{
#ifdef TDMB_I2C_INTERFACE
  int ret;
  static unsigned short wdata[2];
  struct i2c_msg msg[] = \
  {
  	{
  		.addr = TDMB_I2c_SLAVE_N,
  		.flags = 0,
  		.len = 4,
  		.buf = (unsigned char*)wdata,
  	},
  };

  wdata[0] = htons(reg_addr);
  wdata[1] = htons(value);

  ret = i2c_transfer(dmbi2c_adap, msg, 1);
  if(ret<0)
  {
  	DEVLOG(" Error: i2c write word=%d\n", ret);
  	return -1;
  }	
  else
  	return 0;
#else
  	return 0;
#endif
}

static int set_ch_data(DMB_CH_DATA_S* pchbuf, INC_CHANNEL_INFO* pincch, int cnt)
{
  int i;
  int chcnt;
  DMB_CH_DATA_S* pch;
  pch = pchbuf;
  
  for(i=0,chcnt=0;i<cnt;i++)
  {
    if(pincch->uiTmID==0 || (pincch->uiTmID==1 && pincch->ucServiceType==0x18)) // audio, video channel
    {
      pch->freq     = pincch->ulRFFreq;
      pch->ensemble_id   = pincch->uiEnsembleID;
      pch->subch_id      = pincch->ucSubChID;
      pch->type    = pincch->ucServiceType;
      pch->tm_id       = pincch->uiTmID;
      pch->bit_rate      = pincch->uiBitRate;
      strcpy(pch->ensemble_label, pincch->aucEnsembleLabel);
      strcpy(pch->channel_label, pincch->aucLabel);
      pch++;
      chcnt++;
    }
    
    pincch++;
  }
  
  return chcnt;
}

static int get_rssi_level(unsigned int rssi)
{
  if(rssi > KXDMB_RSSI_LEVEL_0_MAX-KX_DMB_LNA_GAIN)
  {
    return KXDMB_RSSI_LEVEL_0;
  } 
  else if(rssi > KXDMB_RSSI_LEVEL_1_MAX-KX_DMB_LNA_GAIN)
  {
    return KXDMB_RSSI_LEVEL_1;
  }
  else if(rssi > KXDMB_RSSI_LEVEL_2_MAX-KX_DMB_LNA_GAIN)
  {
    return KXDMB_RSSI_LEVEL_2;
  }
  else if(rssi > KXDMB_RSSI_LEVEL_3_MAX-KX_DMB_LNA_GAIN)
  {
    return KXDMB_RSSI_LEVEL_3;
  }
  else if(rssi > KXDMB_RSSI_LEVEL_4_MAX-KX_DMB_LNA_GAIN)
  {
    return KXDMB_RSSI_LEVEL_4;
  }
  else if(rssi > KXDMB_RSSI_LEVEL_5_MAX-KX_DMB_LNA_GAIN)
  {
    return KXDMB_RSSI_LEVEL_5;
  }
  else
  {
    return KXDMB_RSSI_LEVEL_6;
  }
}

#ifdef TDMB_SPI_INTERFACE_TEST
int inc_spi_interface_test(void)
{
  unsigned short nLoop = 0, nIndex = 0;
  unsigned short nData = 0;

  while(nIndex < 10)
  {
    for(nLoop=0; nLoop<8; nLoop++)
    {
      INC_CMD_WRITE(TDMB_I2C_ID80, APB_RF_BASE+ nLoop, nLoop) ;
      nData = INC_CMD_READ(TDMB_I2C_ID80, APB_RF_BASE+ nLoop);

      if(nLoop != nData){
      	DEVLOG(" [Interface Test : %02d][FAIL]: WriteData[0x%X], ReadData[0x%X] \r\n", 
      		(nIndex*8)+nLoop, nLoop, nData);
      			return 0;
      }
    }
      nIndex++;
  }
  DEVLOG(" [Interface Test][SUCCESS]: OK \r\n");
  return 1;
}

static void dmb_test(void)
{
  int ret;

  INC_DRIVER_OPEN();
  inc_spi_interface_test();    
}
#endif

#ifdef TDMB_SPI_INTERFACE
static int tdmb_spi_probe(struct spi_device *spi)
{
  int ret;

  DEVLOG(" [%s] %s [%s] : spi.cs[%d], mod[%d], hz[%d] \n", 
  __FILE__,__func__, spi->modalias, spi->chip_select, spi->mode, spi->max_speed_hz);
  gInc_spi = spi;

#ifdef TDMB_SPI_INTERFACE_TEST
  dmb_test();
#endif

  g_stTdmb_Int.tdmb_irq = MSM_GPIO_TO_INT(DMB_GPIO_INT_N);

  ret = request_irq(g_stTdmb_Int.tdmb_irq, tdmb_interrupt, IRQF_TRIGGER_FALLING, TDMB_DEV_NAME, NULL);
  if (ret) {
	free_irq(g_stTdmb_Int.tdmb_irq, 0);
    DEVLOG("can't get IRQ %d, ret %d\n", g_stTdmb_Int.tdmb_irq, ret);
    return -EINVAL;
  }

  return 0;
}

static struct spi_driver tdmb_spi = {
  .driver = {
  	.name = 	TDMB_DEV_NAME,
  	.owner =	THIS_MODULE,
  },
  .probe =	tdmb_spi_probe,
};
#endif

static const struct file_operations dmbdev_fops = {
  .owner		= THIS_MODULE,
  .llseek		= no_llseek,
  .read		= dmbdev_read,
  .write		= dmbdev_write,
  .poll		= dmbdev_poll,
  .unlocked_ioctl	= dmbdev_ioctl,
  .open		= dmbdev_open,
  .release	= dmbdev_release,
};

static int __init dmb_init_module(void)
{
  int ret;

  DEVLOG(" dmbdrv module start....\n");

  tdmb_pdata.setup();

  gpio_set_value_cansleep(tdmb_pdata.reset, 0);
  gpio_set_value_cansleep(tdmb_pdata.demod_enable, 0);
  gpio_set_value_cansleep(DMB_GPIO_INT_N, 1);

#ifdef TDMB_SPI_INTERFACE_TEST
  tdmb_pdata.power(1);
#else
  tdmb_pdata.power(0);          
#endif

  ret = register_chrdev(DMB_MAJOR, TDMB_DRV_NAME, &dmbdev_fops); // if success, return 0
  if (ret < 0) {
    DEVLOG(" [%s] unable to get major %d for fb devs\n", __func__, DMB_MAJOR);
    return ret;
  }

  dmbclass = class_create(THIS_MODULE, TDMB_CLASS_NAME);
  if (IS_ERR(dmbclass)) {
    DEVLOG(" [%s] Unable to create dmbclass; errno = %ld\n", 
    __func__, PTR_ERR(dmbclass));
    unregister_chrdev(DMB_MAJOR, TDMB_DRV_NAME);
    return PTR_ERR(dmbclass);
  }

  dmbdev = device_create(dmbclass, NULL, MKDEV(DMB_MAJOR, 26), NULL, TDMB_DEV_NAME);
  if (IS_ERR(dmbdev)) {
    DEVLOG(" [%s] Unable to create device for framebuffer ; errno = %ld\n",
    __func__, PTR_ERR(dmbdev));
    unregister_chrdev(DMB_MAJOR, TDMB_DRV_NAME);
    return PTR_ERR(dmbdev);
  }

#ifdef TDMB_SPI_INTERFACE
  ret = spi_register_driver(&tdmb_spi);
  if (ret < 0) {
    DEVLOG( " [%s] Unable spi_register_driver; result = %ld\n", 
    __func__, ret);
    class_destroy(dmbclass);
    unregister_chrdev(DMB_MAJOR,TDMB_DRV_NAME);
  }
  init_waitqueue_head(&WaitQueue_Read);
#endif
#ifdef TDMB_I2C_INTERFACE
  dmbi2c_adap = i2c_get_adapter(12);
  if(dmbi2c_adap)
  {
    struct i2c_algo_bit_data *adap = (struct i2c_algo_bit_data *)dmbi2c_adap->algo_data;
    dmbi2c_adap->timeout = 100;
    dmbi2c_adap->retries = 3;
  }
#endif  

  DEVLOG(" %s Success!\n", __func__);
  return 0;
}

static void __exit dmb_cleanup_module(void)
{
  DEVLOG(" dmbdrv module exit.....\n");
#ifdef TDMB_I2C_INTERFACE
  i2c_put_adapter(dmbi2c_adap);
#endif
  free_irq(g_stTdmb_Int.tdmb_irq, NULL);
  device_destroy(dmbclass, MKDEV(DMB_MAJOR,0));
  class_destroy(dmbclass);
  unregister_chrdev(DMB_MAJOR, TDMB_DRV_NAME);
}	

module_init(dmb_init_module);
module_exit(dmb_cleanup_module);
MODULE_AUTHOR("KTTech. <xxx@kttech.co.kr>");
MODULE_DESCRIPTION("dmb /dev entries driver");
MODULE_LICENSE("GPL");
