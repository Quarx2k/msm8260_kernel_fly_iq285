
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/smp_lock.h>
#include <linux/jiffies.h>
#include <asm/uaccess.h>

#include <linux/delay.h>
#include "INC_INCLUDES.h"
#include <linux/spi/spi.h>

#ifdef INC_KERNEL_SPACE
#include "dmbdrv_kttech.h"
#endif

#define I2C_RETRIES	0x0701	/* number of times a device address should
				   be polled when not acknowledging */
#define I2C_TIMEOUT	0x0702	/* set timeout in units of 10 ms */

/* NOTE: Slave address is 7 or 10 bits, but 10-bit addresses
 * are NOT supported! (due to code brokenness)
 */
#define I2C_SLAVE	0x0703	/* Use this slave address */
#define I2C_SLAVE_FORCE	0x0706	/* Use this slave address, even if it
				   is already in use by a driver! */
#define I2C_TENBIT	0x0704	/* 0 for 7 bit addrs, != 0 for 10 bit */

#define I2C_FUNCS	0x0705	/* Get the adapter functionality mask */

#define I2C_RDWR	0x0707	/* Combined R/W transfer (one STOP only) */

#define I2C_PEC		0x0708	/* != 0 to use PEC with SMBus */
#define I2C_SMBUS	0x0720	/* SMBus transfer */


#ifndef INC_KERNEL_SPACE
int inc_open_dev(void);
void inc_close_dev(void);
int inc_i2c_writew(unsigned short uiAddr, unsigned short uiData);
int inc_i2c_readw(unsigned short uiaddr, unsigned short *wdata);
int inc_i2c_burst_read(unsigned short uiAddr, unsigned char* pbuf, int len);
#endif

#ifndef INC_KERNEL_SPACE
int hdev = -1; // device file handle
static const char *device_name = "/dev/i2c-11";
#endif

/*********************************************************************************/
/* Operating Chip set : T3900                                                    */
/* Software version   : version 1.00                                             */
/* Software Update    : 2010.08.11                                              */
/*********************************************************************************/
#define INC_INTERRUPT_LOCK()		
#define INC_INTERRUPT_FREE()



ST_SUBCH_INFO		g_stDmbInfo;
ST_SUBCH_INFO		g_stDabInfo;
ST_SUBCH_INFO		g_stDataInfo;
ST_SUBCH_INFO		g_stFIDCInfo;

ENSEMBLE_BAND 		m_ucRfBand 		= KOREA_BAND_ENABLE;

/*********************************************************************************/
/*  RF Band Select						                                         */
/*																				 */
/*  INC_UINT8 m_ucRfBand = KOREA_BAND_ENABLE,									 */
/*						   BANDIII_ENABLE,										 */
/*						   LBAND_ENABLE,										 */
/*						   CHINA_ENABLE,										 */
/*						   EXTERNAL_ENABLE,										 */
/*********************************************************************************/

CTRL_MODE 			m_ucCommandMode 		= INC_SPI_CTRL;
ST_TRANSMISSION		m_ucTransMode			= TRANSMISSION_MODE1;
UPLOAD_MODE_INFO	m_ucUploadMode 			= STREAM_UPLOAD_SPI;
CLOCK_SPEED			m_ucClockSpeed 			= INC_OUTPUT_CLOCK_4096;
INC_ACTIVE_MODE		m_ucMPI_CS_Active 		= INC_ACTIVE_LOW;
INC_ACTIVE_MODE		m_ucMPI_CLK_Active 		= INC_ACTIVE_LOW;
/*
INC_UINT16			m_unIntCtrl				= (INC_INTERRUPT_ACTIVE_POLALITY_LOW | \
											   INC_INTERRUPT_PULSE | \
											   INC_INTERRUPT_AUTOCLEAR_ENABLE | \
											   (INC_INTERRUPT_PULSE_COUNT & INC_INTERRUPT_PULSE_COUNT_MASK));
*/
INC_UINT16			m_unIntCtrl 			= (INC_INTERRUPT_POLARITY_HIGH | \
											   INC_INTERRUPT_LEVEL | \
											   INC_INTERRUPT_AUTOCLEAR_DISABLE | \
											   (INC_INTERRUPT_PULSE_COUNT & INC_INTERRUPT_PULSE_COUNT_MASK));


/*********************************************************************************/
/* PLL_MODE			m_ucPLL_Mode                                                 */
/*T3700  Input Clock Setting                                                     */
/*********************************************************************************/
PLL_MODE			m_ucPLL_Mode		= INPUT_CLOCK_19200KHZ;


/*********************************************************************************/
/* INC_DPD_MODE		m_ucDPD_Mode                                                 */
/* T3700  Power Saving mode setting                                              */
/*********************************************************************************/
INC_DPD_MODE		m_ucDPD_Mode		= INC_DPD_OFF;


/*********************************************************************************/
/*  MPI Chip Select and Clock Setup Part                                         */
/*                                                                               */
/*  INC_UINT8 m_ucCommandMode = INC_I2C_CTRL, INC_SPI_CTRL, INC_EBI_CTRL         */
/*                                                                               */
/*  INC_UINT8 m_ucUploadMode = STREAM_UPLOAD_MASTER_SERIAL,                      */
/*                 STREAM_UPLOAD_SLAVE_PARALLEL,                                 */
/*                 STREAM_UPLOAD_TS,                                             */
/*                 STREAM_UPLOAD_SPI,                                            */
/*                                                                               */
/*  INC_UINT8 m_ucClockSpeed = INC_OUTPUT_CLOCK_4096,                            */
/*                 INC_OUTPUT_CLOCK_2048,                                        */
/*                 INC_OUTPUT_CLOCK_1024,                                        */
/*********************************************************************************/

void INC_DELAY(INC_UINT16 uiDelay)
{

	msleep(uiDelay);
}

#ifdef INC_DEBUG_MSG
void INC_MSG_PRINTF(INC_INT8 nFlag, INC_INT8* pFormat, ...)
{
	va_list Ap;
	INC_UINT16 nSize;
	INC_INT8 acTmpBuff[256] = {0};
//	INC_INT8 logstr[256] = {0};

	va_start(Ap, pFormat);
	nSize = vsprintf(acTmpBuff, pFormat, Ap);
	va_end(Ap);

	//RETAILMSG(nFlag, (TEXT("%s"), wcstring));
#ifdef INC_KERNEL_SPACE
	if(nFlag)
		printk(KERN_DEBUG "%s", acTmpBuff);

#else
	printf("%s", acTmpBuff);
#endif
	//TODO Serial code here...
	///////////////////////////////////////////////////
	// .NET 버전일 경우 wchar_t로 변환
/*	wchar_t wcstring[1024] = {0};
	mbstowcs(wcstring, acTmpBuff, strlen(acTmpBuff)+1);
	RETAILMSG(nFlag, (TEXT("%s"), wcstring));
	///////////////////////////////////////////////////
	if(m_hINC_LogFile && nFlag){
		SYSTEMTIME time;
		GetLocalTime(&time);

		sprintf(logstr, "[%02d.%02d %02d:%02d:%02d] %s",
			time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, acTmpBuff);

		fwrite(logstr, sizeof(char), strlen(logstr)+1, m_hINC_LogFile);
	}
*/
}
#endif

#ifdef INC_KERNEL_SPACE
#define CMD_SIZE	4
static int	hSPI;
static unsigned char mode = 0;
static unsigned char bits_per_word = 8;
static unsigned int  speed = 5400000;
struct spi_device *gInc_spi = NULL;
static int transfer(int fd, char *tbuf, char *rbuf, int bytes)
{
	int ret=0;
	struct spi_transfer tr = {
		.tx_buf = tbuf,
		.rx_buf = rbuf,
		.len = bytes,
	};

	struct spi_message msg;

	spi_setup(gInc_spi);
 
	spi_message_init(&msg);
	spi_message_add_tail(&tr, &msg);

	ret = spi_sync(gInc_spi, &msg);
	if(ret != 0)
		INC_MSG_PRINTF(1, "[%s] can't send spi message", __func__);

	return ret;
}
#endif

INC_UINT8 INC_DRIVER_OPEN(void)
{
  int res =0;
  
#ifdef INC_KERNEL_SPACE
	gInc_spi->mode = mode;
	gInc_spi->bits_per_word = bits_per_word;
	gInc_spi->max_speed_hz = speed;

	return INC_SUCCESS;
#else
  int ret;
  ret = inc_open_dev();
  if(ret>=0)
    return INC_SUCCESS;
  else
    return -1;
#endif    
  
}

INC_UINT8 INC_DRIVER_CLOSE(void)
{
#ifdef INC_KERNEL_SPACE
	return INC_SUCCESS;
#else
	inc_close_dev();
	return INC_SUCCESS;
#endif  
}

INC_UINT8 INC_DRIVER_RESET(void)
{
	INC_DRIVER_CLOSE();
	if(INC_ERROR == INC_DRIVER_OPEN())
		return INC_ERROR;

	INC_DELAY(500);
	return INC_SUCCESS;
}


INC_UINT16 INC_I2C_READ(INC_UINT8 ucI2CID, INC_UINT16 uiAddr)
{
  unsigned short wdata;
  int ret;
  ret = dmbi2c_reg_readw(uiAddr, &wdata);
  if(ret<0)
  {
    INC_MSG_PRINTF(INC_DEBUG_LEVEL, "#### read word error!!!!!. uiAddr=%04x, ret=%d\n", uiAddr, ret);
    return -1;
  }
  else
  {
    return wdata;
  }
}

INC_UINT8 INC_I2C_WRITE(INC_UINT8 ucI2CID, INC_UINT16 uiAddr, INC_UINT16 uiData)
{
  int ret;
	ret = dmbi2c_reg_writew(uiAddr, uiData);
  if(ret>=0)
  {
    return INC_SUCCESS;
  }
  else
  {
    INC_MSG_PRINTF(INC_DEBUG_LEVEL, "#### write word error!!!!!. uiAddr=%04x, data=%04x, ret=%d\n", uiAddr, uiData, ret);
    return -1;
  }
}


INC_UINT8 INC_I2C_READ_BURST(INC_UINT8 ucI2CID,  INC_UINT16 uiAddr, INC_UINT8* pData, INC_UINT16 nSize)
{
  int ret;
#ifdef INC_KERNEL_SPACE
	ret = dmbi2c_reg_read(uiAddr, pData, nSize);
#else	
	ret = inc_i2c_burst_read(uiAddr, pData, nSize);
#endif	
	if(ret == nSize)
		return INC_SUCCESS;
	else
		return INC_ERROR;
}

INC_UINT8 INC_EBI_WRITE(INC_UINT8 ucI2CID, INC_UINT16 uiAddr, INC_UINT16 uiData)
{
	INC_UINT16 uiCMD = INC_REGISTER_CTRL(SPI_REGWRITE_CMD) | 1;
	INC_UINT16 uiNewAddr = (ucI2CID == TDMB_I2C_ID82) ? (uiAddr | 0x8000) : uiAddr;

	INC_INTERRUPT_LOCK();

	*(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS = uiNewAddr >> 8;
	*(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS = uiNewAddr & 0xff;
	*(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS = uiCMD >> 8;
	*(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS = uiCMD & 0xff;

	*(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS = (uiData >> 8) & 0xff;
	*(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS =  uiData & 0xff;

	INC_INTERRUPT_FREE();
	return INC_SUCCESS;
}

INC_UINT16 INC_EBI_READ(INC_UINT8 ucI2CID, INC_UINT16 uiAddr)
{
	INC_UINT16 uiRcvData = 0;
	INC_UINT16 uiCMD = INC_REGISTER_CTRL(SPI_REGREAD_CMD) | 1;
	INC_UINT16 uiNewAddr = (ucI2CID == TDMB_I2C_ID82) ? (uiAddr | 0x8000) : uiAddr;

	INC_INTERRUPT_LOCK();
	*(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS = uiNewAddr >> 8;
	*(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS = uiNewAddr & 0xff;
	*(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS = uiCMD >> 8;
	*(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS = uiCMD & 0xff;

	uiRcvData  = (*(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS  & 0xff) << 8;
	uiRcvData |= (*(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS & 0xff);
	
	INC_INTERRUPT_FREE();
	return uiRcvData;
}

INC_UINT8 INC_EBI_READ_BURST(INC_UINT8 ucI2CID, INC_UINT16 uiAddr, INC_UINT8* pData, INC_UINT16 nSize)
{
	INC_UINT16 uiLoop, nIndex = 0, anLength[2], uiCMD, unDataCnt;
	INC_UINT16 uiNewAddr = (ucI2CID == TDMB_I2C_ID82) ? (uiAddr | 0x8000) : uiAddr;

	if(nSize > INC_MPI_MAX_BUFF) return INC_ERROR;
	memset((INC_INT8*)anLength, 0, sizeof(anLength));

	if(nSize > INC_TDMB_LENGTH_MASK) {
		anLength[nIndex++] = INC_TDMB_LENGTH_MASK;
		anLength[nIndex++] = nSize - INC_TDMB_LENGTH_MASK;
	}
	else anLength[nIndex++] = nSize;

	INC_INTERRUPT_LOCK();
	for(uiLoop = 0; uiLoop < nIndex; uiLoop++){

		uiCMD = INC_REGISTER_CTRL(SPI_MEMREAD_CMD) | (anLength[uiLoop] & INC_TDMB_LENGTH_MASK);

		*(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS = uiNewAddr >> 8;
		*(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS = uiNewAddr & 0xff;
		*(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS = uiCMD >> 8;
		*(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS = uiCMD & 0xff;

		for(unDataCnt = 0 ; unDataCnt < anLength[uiLoop]; unDataCnt++){
			*pData++ = *(volatile INC_UINT8*)STREAM_PARALLEL_ADDRESS & 0xff;
		}
	}
	INC_INTERRUPT_FREE();

	return INC_SUCCESS;
}

#define CMD_SIZE	4
INC_UINT16 INC_SPI_REG_READ(INC_UINT8 ucI2CID, INC_UINT16 uiAddr)
{
	INC_UINT16 uiRcvData = 0;
	int			res;
	INC_UINT16	uiCMD = INC_REGISTER_CTRL(SPI_REGREAD_CMD) | 1;
	INC_UINT8	auiTxBuff[CMD_SIZE+2] = {0};
	INC_UINT8	auiRxBuff[CMD_SIZE+2] = {0};

	auiTxBuff[0] = uiAddr>>8;
	auiTxBuff[1] = uiAddr&0xff;
	auiTxBuff[2] = uiCMD>>8;
	auiTxBuff[3] = uiCMD&0xff;

	INC_INTERRUPT_LOCK();
	res = transfer(hSPI, auiTxBuff, auiRxBuff, CMD_SIZE+2);
	if (res < 0) {	
		INC_MSG_PRINTF(1, "[%s] Fail : 0x%X\n", __func__,res);
		return INC_ERROR;
	}
	INC_INTERRUPT_FREE();
	return auiRxBuff[4]<<8 | auiRxBuff[5];
}

INC_UINT8 INC_SPI_REG_WRITE(INC_UINT8 ucI2CID, INC_UINT16 uiAddr, INC_UINT16 uiData)
{
	int res;
	INC_UINT16 uiCMD = INC_REGISTER_CTRL(SPI_REGWRITE_CMD) | 1;
	INC_UINT8 auiTxBuff[CMD_SIZE+2] = {0};

	auiTxBuff[0] = uiAddr>>8 ;
	auiTxBuff[1] = uiAddr&0xff;
	auiTxBuff[2] = uiCMD>>8;
	auiTxBuff[3] = uiCMD&0xff;
	auiTxBuff[4] = uiData>>8;
	auiTxBuff[5] = uiData&0xff;

	INC_INTERRUPT_LOCK();
	res = transfer(hSPI, auiTxBuff, 0, CMD_SIZE+2);
	if (res < 0) {	
		INC_MSG_PRINTF(1, "[%s] Fail : 0x%X\n", __func__,res);
		return INC_ERROR;
	}
	INC_INTERRUPT_FREE();
	return INC_SUCCESS;
}

INC_UINT8 INC_SPI_READ_BURST(INC_UINT8 ucI2CID, INC_UINT16 uiAddr, INC_UINT8* pBuff, INC_UINT16 wSize)
{
	int res;
	INC_UINT16 uiCMD = SPI_MEMREAD_CMD << 12 | (wSize & 0xFFF);
	INC_UINT8 auiTxBuff[CMD_SIZE+2] = {0};
	static INC_UINT8 auiRxBuff[4096] = {0};

	if(wSize > 0xFFF) 
		return INC_ERROR;

	auiTxBuff[0] = uiAddr>>8;
	auiTxBuff[1] = uiAddr&0xff;
	auiTxBuff[2] = uiCMD>>8;
	auiTxBuff[3] = uiCMD&0xff;

	INC_INTERRUPT_LOCK();
	res = transfer(hSPI, auiTxBuff, auiRxBuff, CMD_SIZE + wSize);
	if (res < 0) {	
		INC_MSG_PRINTF(1, "[%s] fail : 0x%X\n", __func__, res);
		return INC_ERROR;
	}
	memcpy(pBuff, &auiRxBuff[4], wSize);
	INC_INTERRUPT_FREE();
	return INC_SUCCESS;
}

INC_UINT8 INTERFACE_DBINIT(void)
{
	memset(&g_stDmbInfo,	0, sizeof(ST_SUBCH_INFO));
	memset(&g_stDabInfo,	0, sizeof(ST_SUBCH_INFO));
	memset(&g_stDataInfo,	0, sizeof(ST_SUBCH_INFO));
	memset(&g_stFIDCInfo,	0, sizeof(ST_SUBCH_INFO));
	return INC_SUCCESS;
}

void INTERFACE_UPLOAD_MODE(INC_UINT8 ucI2CID, UPLOAD_MODE_INFO ucUploadMode)
{
	m_ucUploadMode = ucUploadMode;
	INC_UPLOAD_MODE(ucI2CID);
}

INC_UINT8 INTERFACE_PLL_MODE(INC_UINT8 ucI2CID, PLL_MODE ucPllMode)
{
	m_ucPLL_Mode = ucPllMode;
	return INC_PLL_SET(ucI2CID);
}

// 초기 전원 입력시 호출
INC_UINT8 INTERFACE_INIT(INC_UINT8 ucI2CID)
{
	return INC_INIT(ucI2CID);
}

// 에러 발생시 에러코드 읽기
INC_ERROR_INFO INTERFACE_ERROR_STATUS(INC_UINT8 ucI2CID)
{
	ST_BBPINFO* pInfo;
	pInfo = INC_GET_STRINFO(ucI2CID);
	return pInfo->nBbpStatus;
}

/*********************************************************************************/
/* 단일 채널 선택하여 시작하기....                                               */  
/* pChInfo->ucServiceType, pChInfo->ucSubChID, pChInfo->ulRFFreq 는              */
/* 반드시 넘겨주어야 한다.                                                       */
/* DMB채널 선택시 pChInfo->ucServiceType = 0x18                                  */
/* DAB, DATA채널 선택시 pChInfo->ucServiceType = 0으로 설정을 해야함.            */
/*********************************************************************************/
INC_UINT8 INTERFACE_START(INC_UINT8 ucI2CID, ST_SUBCH_INFO* pChInfo)
{
	return INC_CHANNEL_START(ucI2CID, pChInfo);
}


/*********************************************************************************/
/* 스캔시  호출한다.                                                             */
/* 주파수 값은 받드시넘겨주어야 한다.                                            */
/* Band를 변경하여 스캔시는 m_ucRfBand를 변경하여야 한다.                        */
/* 주파수 값은 받드시넘겨주어야 한다.                                            */
/*********************************************************************************/
//INC_UINT8 INTERFACE_SCAN(INC_UINT8 ucI2CID, INC_UINT32 ulFreq)
INC_UINT8 INTERFACE_SCAN(INC_UINT8 ucI2CID, INC_CHANNEL_INFO *pfoundch, int chbuf_cnt, int *pfound_cnt, INC_UINT32 ulFreq)
{
	INC_UINT16 nLoop = 0, nTotalCH_cnt=0;
	INTERFACE_DBINIT();

  INC_MSG_PRINTF(1, " interface scan..., freq=%0d \n", ulFreq);
  
	if(!INC_ENSEMBLE_SCAN(ucI2CID, ulFreq))
	{
	  INC_MSG_PRINTF(1, "    !!!! ensemble scan err....\n");
	  return INC_ERROR;
  }
  
  INC_MSG_PRINTF(1, "    scan success.......\n");
	INC_DB_UPDATE(ulFreq, &g_stDmbInfo, &g_stDabInfo, &g_stDataInfo, &g_stFIDCInfo);

	INC_BUBBLE_SORT(&g_stDmbInfo,  INC_SUB_CHANNEL_ID);
	INC_BUBBLE_SORT(&g_stDabInfo,  INC_SUB_CHANNEL_ID);
	INC_BUBBLE_SORT(&g_stDataInfo, INC_SUB_CHANNEL_ID);
	INC_BUBBLE_SORT(&g_stFIDCInfo, INC_SUB_CHANNEL_ID);

	*pfound_cnt = g_stDmbInfo.nSetCnt + g_stDabInfo.nSetCnt;

  INC_MSG_PRINTF(1, "    found count=%d\n", *pfound_cnt);
  
	for(nLoop=0; nLoop<g_stDmbInfo.nSetCnt; nLoop++){
		if(pfoundch && nTotalCH_cnt<chbuf_cnt){
			*pfoundch++ = g_stDmbInfo.astSubChInfo[nLoop];
			nTotalCH_cnt++;
		}		
	}

	for(nLoop=0; nLoop<g_stDabInfo.nSetCnt; nLoop++){
		if(pfoundch && nTotalCH_cnt<chbuf_cnt){
			*pfoundch++ = g_stDabInfo.astSubChInfo[nLoop];
			nTotalCH_cnt++;
		}		
	}

  INC_MSG_PRINTF(1, "     scan exit....\n");

  
	return INC_SUCCESS;
}

/*********************************************************************************/
/* 단일채널 스캔이 완료되면 DMB채널 개수를 리턴한다.                             */
/*********************************************************************************/
INC_UINT16 INTERFACE_GETDMB_CNT(void)
{
	return (INC_UINT16)g_stDmbInfo.nSetCnt;
}

/*********************************************************************************/
/* 단일채널 스캔이 완료되면 DAB채널 개수를 리턴한다.                             */
/*********************************************************************************/
INC_UINT16 INTERFACE_GETDAB_CNT(void)
{
	return (INC_UINT16)g_stDabInfo.nSetCnt;
}

/*********************************************************************************/
/* 단일채널 스캔이 완료되면 DATA채널 개수를 리턴한다.                            */
/*********************************************************************************/
INC_UINT16 INTERFACE_GETDATA_CNT(void)
{
	return (INC_UINT16)g_stDataInfo.nSetCnt;
}

/*********************************************************************************/
/* 단일채널 스캔이 완료되면 Ensemble label을 리턴한다.                           */
/*********************************************************************************/
INC_UINT8* INTERFACE_GETENSEMBLE_LABEL(INC_UINT8 ucI2CID)
{
	ST_FICDB_LIST*	pList;
	pList = INC_GET_FICDB_LIST();
	return pList->aucEnsembleName;
}

/*********************************************************************************/
/* DMB 채널 정보를 리턴한다.                                                     */
/*********************************************************************************/
INC_CHANNEL_INFO* INTERFACE_GETDB_DMB(INC_INT16 uiPos)
{
	if(uiPos >= MAX_SUBCH_SIZE) return INC_NULL;
	if(uiPos >= g_stDmbInfo.nSetCnt) return INC_NULL;
	return &g_stDmbInfo.astSubChInfo[uiPos];
}

/*********************************************************************************/
/* DAB 채널 정보를 리턴한다.                                                     */
/*********************************************************************************/
INC_CHANNEL_INFO* INTERFACE_GETDB_DAB(INC_INT16 uiPos)
{
	if(uiPos >= MAX_SUBCH_SIZE) return INC_NULL;
	if(uiPos >= g_stDabInfo.nSetCnt) return INC_NULL;
	return &g_stDabInfo.astSubChInfo[uiPos];
}

/*********************************************************************************/
/* DATA 채널 정보를 리턴한다.                                                    */
/*********************************************************************************/
INC_CHANNEL_INFO* INTERFACE_GETDB_DATA(INC_INT16 uiPos)
{
	if(uiPos >= MAX_SUBCH_SIZE) return INC_NULL;
	if(uiPos >= g_stDataInfo.nSetCnt) return INC_NULL;
	return &g_stDataInfo.astSubChInfo[uiPos];
}

// 시청 중 FIC 정보 변경되었는지를 체크
INC_UINT8 INTERFACE_RECONFIG(INC_UINT8 ucI2CID)
{
	return INC_FIC_RECONFIGURATION_HW_CHECK(ucI2CID);
}


#ifndef INC_KERNEL_SPACE
INC_UINT8 INTERFACE_STATUS_CHECK(INC_UINT8 ucI2CID)
{
	return INC_STATUS_CHECK(ucI2CID);
}


INC_UINT16 INTERFACE_GET_CER(INC_UINT8 ucI2CID)
{
	return INC_GET_CER(ucI2CID);
}

INC_UINT8 INTERFACE_GET_SNR(INC_UINT8 ucI2CID)
{
	return INC_GET_SNR(ucI2CID);
}

INC_UINT32 INTERFACE_GET_POSTBER(INC_UINT8 ucI2CID)
{
	return INC_GET_POSTBER(ucI2CID);
}

INC_UINT32 INTERFACE_GET_PREBER(INC_UINT8 ucI2CID)
{
	return INC_GET_PREBER(ucI2CID);
}
#endif

/*********************************************************************************/
/* Scan, 채널 시작시에 강제로 중지시 호출한다.                                      */
/*********************************************************************************/
void INTERFACE_USER_STOP(INC_UINT8 ucI2CID)
{
	ST_BBPINFO* pInfo;
	pInfo = INC_GET_STRINFO(ucI2CID);
	pInfo->ucStop = 1;
}

void INTERFACE_USER_STOP_CLEAR(INC_UINT8 ucI2CID)
{
	ST_BBPINFO* pInfo;
	pInfo = INC_GET_STRINFO(ucI2CID);
	pInfo->ucStop = 0;
}

// 인터럽트 인에이블...
void INTERFACE_INT_ENABLE(INC_UINT8 ucI2CID, INC_UINT16 unSet)
{
	INC_INTERRUPT_SET(ucI2CID, unSet);
}

// Use when polling mode
INC_UINT8 INTERFACE_INT_CHECK(INC_UINT8 ucI2CID)
{
	INC_UINT16 nValue = 0;

	nValue = INC_CMD_READ(ucI2CID, APB_INT_BASE+ 0x01);
	if(!(nValue & INC_MPI_INTERRUPT_ENABLE))
		return FALSE;

	return TRUE;
}

// 인터럽스 클리어
void INTERFACE_INT_CLEAR(INC_UINT8 ucI2CID, INC_UINT16 unClr)
{
	INC_INTERRUPT_CLEAR(ucI2CID, unClr);
}

// 인터럽트 서비스 루틴... // SPI Slave Mode or MPI Slave Mode
INC_UINT8 INTERFACE_ISR(INC_UINT8 ucI2CID, INC_UINT8* pBuff)
{
	INC_UINT16 unData;
	unData = INC_CMD_READ(ucI2CID, APB_MPI_BASE + 0x6);
	if(unData < INC_INTERRUPT_SIZE) return INC_ERROR;

	INC_CMD_READ_BURST(ucI2CID, APB_STREAM_BASE, pBuff, INC_INTERRUPT_SIZE);

	if((m_unIntCtrl & INC_INTERRUPT_LEVEL) && (!(m_unIntCtrl & INC_INTERRUPT_AUTOCLEAR_ENABLE)))
		INTERFACE_INT_CLEAR(ucI2CID, INC_MPI_INTERRUPT_ENABLE);

	return INC_SUCCESS;
}

INC_UINT8 SAVE_CHANNEL_INFO(char* pStr)
{
#ifndef INC_KERNEL_SPACE
	FILE* pFile = fopen(pStr, "wb+");
	if(pFile == NULL)
		return INC_ERROR;

	INC_UINT32 dwWriteLen = 0;

	dwWriteLen = fwrite(&g_stDabInfo, sizeof(char), sizeof(ST_SUBCH_INFO), pFile);
	if( dwWriteLen != sizeof(ST_SUBCH_INFO) )
	{
		memset(&g_stDabInfo, 0x00, sizeof(ST_SUBCH_INFO));
	}

	dwWriteLen = fwrite(&g_stDmbInfo, sizeof(char), sizeof(ST_SUBCH_INFO), pFile);
	if( dwWriteLen != sizeof(ST_SUBCH_INFO) )
	{
		memset(&g_stDmbInfo, 0x00, sizeof(ST_SUBCH_INFO));
	}

	dwWriteLen = fwrite(&g_stDataInfo, sizeof(char), sizeof(ST_SUBCH_INFO), pFile);
	if( dwWriteLen != sizeof(ST_SUBCH_INFO) )
	{
		memset(&g_stDataInfo, 0x00, sizeof(ST_SUBCH_INFO));
	}

	fclose(pFile);
	return INC_SUCCESS;
#else
	return INC_ERROR;
#endif
}

INC_UINT8 LOAD_CHANNEL_INFO(char* pStr)
{
#ifndef INC_KERNEL_SPACE
	FILE* pFile = fopen(pStr, "rb");
	if(pFile == NULL)
		return INC_ERROR;

	INC_UINT32 dwReadLen = 0;

	dwReadLen = fread(&g_stDabInfo, sizeof(char), sizeof(ST_SUBCH_INFO), pFile);
	if( dwReadLen != sizeof(ST_SUBCH_INFO) )
	{
		memset(&g_stDabInfo, 0x00, sizeof(ST_SUBCH_INFO));
	}

	dwReadLen = fread(&g_stDmbInfo, sizeof(char), sizeof(ST_SUBCH_INFO), pFile);
	if( dwReadLen != sizeof(ST_SUBCH_INFO) )
	{
		memset(&g_stDmbInfo, 0x00, sizeof(ST_SUBCH_INFO));
	}

	dwReadLen = fread(&g_stDataInfo, sizeof(char), sizeof(ST_SUBCH_INFO), pFile);
	if( dwReadLen != sizeof(ST_SUBCH_INFO) )
	{
		memset(&g_stDataInfo, 0x00, sizeof(ST_SUBCH_INFO));
	}
	fclose(pFile);
	return INC_SUCCESS;
#else
	return INC_ERROR;
#endif
}
//////////////////////
// for NexTreaming 
//////////////////////
INC_UINT8 INTERFACE_CHANGE_BAND(INC_UINT8 ucI2CID, INC_UINT16 usBand)
{
	switch(usBand){
		case 0x01 : 
			m_ucRfBand = KOREA_BAND_ENABLE; 
			m_ucTransMode = TRANSMISSION_MODE1; 
			break;
		case 0x02 : 
			m_ucRfBand = BANDIII_ENABLE;
			m_ucTransMode = TRANSMISSION_MODE1; 
			break;
		case 0x04 : 
			m_ucRfBand = LBAND_ENABLE;
			m_ucTransMode = TRANSMISSION_MODE2; 
			break;
		case 0x08 : 
			//m_ucRfBand = CANADA_ENABLE;
			//m_ucTransMode = TRANSMISSION_MODE2; 
			//break;
		case 0x10 : 
			m_ucRfBand = CHINA_ENABLE;
			m_ucTransMode = TRANSMISSION_MODE1; break;
		default : return INC_ERROR;
	}
	return INC_SUCCESS;
}

INC_UINT8 INTERFACE_FIC_UPDATE_CHECK(INC_UINT8 ucI2CID)
{
	ST_BBPINFO* pInfo;
	pInfo = INC_GET_STRINFO(ucI2CID);

	if(pInfo->IsChangeEnsemble == TRUE)
		return TRUE;

	return FALSE;
}

void INTERFACE_INC_DEBUG(INC_UINT8 ucI2CID)
{
/*	INC_UINT16 nLoop = 0;

	for(nLoop = 0; nLoop < 3; nLoop++)
	{
		INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_DEINT_BASE+ 0x02+%d : 0x%X \r\n", nLoop*2, INC_CMD_READ(TDMB_I2C_ID80, APB_DEINT_BASE+ 0x02 + (nLoop*2)));
		INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_DEINT_BASE+ 0x03+%d : 0x%X \r\n", nLoop*2, INC_CMD_READ(TDMB_I2C_ID80, APB_DEINT_BASE+ 0x03 + (nLoop*2)));
		INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_VTB_BASE  + 0x02+%d : 0x%X \r\n", nLoop, INC_CMD_READ(TDMB_I2C_ID80, APB_VTB_BASE+ 0x02 + nLoop));
	}


	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_MPI_BASE+ 0x00 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_MPI_BASE+ 0x00));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_MPI_BASE+ 0x01 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_MPI_BASE+ 0x01));
//	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_MPI_BASE+ 0x02 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_MPI_BASE+ 0x02));
//	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_MPI_BASE+ 0x03 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_MPI_BASE+ 0x03));
//	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_MPI_BASE+ 0x04 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_MPI_BASE+ 0x04));
//	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_MPI_BASE+ 0x05 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_MPI_BASE+ 0x05));
//	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_MPI_BASE+ 0x06 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_MPI_BASE+ 0x06));
//	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_MPI_BASE+ 0x07 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_MPI_BASE+ 0x07));
//	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_MPI_BASE+ 0x08 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_MPI_BASE+ 0x08));


	// INIT
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_INT_BASE+ 0x00 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_INT_BASE+ 0x00));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_INT_BASE+ 0x01 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_INT_BASE+ 0x01));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_INT_BASE+ 0x02 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_INT_BASE+ 0x02));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_INT_BASE+ 0x03 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_INT_BASE+ 0x03));

	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0x3B : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0x3B));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0x00 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0x00));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0x84 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0x84));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0x86 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0x86));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0xB4 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0xB4));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0x1A : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0x1A));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0x8A : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0x8A));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0xC4 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0xC4));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0x24 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0x24));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0xBE : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0xBE));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0xB0 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0xB0));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0xC0 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0xC0));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0x8C : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0x8C));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0xA8 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0xA8));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0xAA : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0xAA));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0x80 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0x80));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0x88 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0x88));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0xC8 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0xC8));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0xBC : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0xBC));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0x90 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0x90));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0xCA : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0xCA));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0x40 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0x40));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0x24 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0x24));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0x41 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0x41));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_PHY_BASE+ 0xC6 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_PHY_BASE+ 0xC6));

	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_VTB_BASE+ 0x05 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_VTB_BASE+ 0x05));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_VTB_BASE+ 0x01 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_VTB_BASE+ 0x01));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_VTB_BASE+ 0x00 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_VTB_BASE+ 0x00));

	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_RS_BASE+ 0x00 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_RS_BASE+ 0x00));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_RS_BASE+ 0x07 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_RS_BASE+ 0x07));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_RS_BASE+ 0x0A : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_RS_BASE+ 0x0A));
	INC_MSG_PRINTF(INC_DEBUG_LEVEL, " APB_RS_BASE+ 0x01 : 0x%X \r\n", INC_CMD_READ(TDMB_I2C_ID80, APB_RS_BASE+ 0x01));
*/}
/*********************************************************************************/


#ifndef INC_KERNEL_SPACE
int inc_open_dev(void)
{
  int ret;
    
  hdev = open(device_name, O_RDWR);
  INC_MSG_PRINTF(INC_DEBUG_LEVEL, "    open i2c=%s, hdev=%04x\n", device_name, hdev);  
  if(hdev > 0)
  {
  
    ret = ioctl(hdev, 0x0800, 0);
    usleep(10*1000);
    ret = ioctl(hdev, I2C_SLAVE, TDMB_I2C_ID80);
    //ret = ioctl(hdev, I2C_TIMEOUT, 20);
    ret = ioctl(hdev, I2C_RETRIES, 3);
    //ret = ioctl(hdev, I2C_DMB_EXT_RESET, 0);
    return 0;
  }
  else
    return -1;
}

void inc_close_dev(void)
{
  if(hdev > 0)
    close(hdev);

  hdev = -1;
}
#endif


#ifndef INC_KERNEL_SPACE		
int inc_i2c_burst_read(unsigned short uiAddr, unsigned char* pbuf, int len)
{
	int ret;
	ret = dmbi2c_reg_read(uiAddr, pbuf, len);
	return ret;
  int ret;
  int i;
  unsigned short wt;

  wt = htons(uiAddr);
  for(i=0;i<DMB_I2C_RETRY_NUM;i++)
  {
    ret = write(hdev, &wt, 2);
    //INC_MSG_PRINTF"    write addr. ret=%d, i=%d\n", ret, i);
    if(ret>0)
      break;
    else
      INC_MSG_PRINTF(INC_DEBUG_LEVEL, "### write addr error.. ret=%d, try=%d\n", ret, i);
  }

  if(ret<=0)
  {
    INC_MSG_PRINTF(INC_DEBUG_LEVEL, "################################## write addr fail......\n");
    return -1;
  }

  
  ret = read(hdev, pbuf, len);
  if(ret != len)
    INC_MSG_PRINTF(INC_DEBUG_LEVEL, "########## burst read fail ....... exp_len=%d, len=%d\n", len, ret);
  return ret;
}


int inc_i2c_writew(unsigned short uiAddr, unsigned short uiData)
{
  int ret, i;
  unsigned short wt;  
  unsigned char tbuf[4];
  unsigned short reg, data;

  reg = htons(uiAddr);
  data = htons(uiData);

  tbuf[0] = reg & 0xFF;
  tbuf[1] = (reg >> 8) & 0xFF;
  tbuf[2] = data & 0xFF;
  tbuf[3] = (data >> 8) & 0xFF;

  for(i=0;i<DMB_I2C_RETRY_NUM;i++)
  {
    ret = write(hdev, tbuf, 4);
    if(ret<4)
    {
      DMB_LOGE("### write error. ret=%d, try=%d\n", ret, i);
    }
    else
      break;
  }
  
  if(ret<4)
  {
    DMB_LOGE("####  write error final=%d\n", ret);
    return -1;
  }

  usleep(1);
  return ret;
}

int inc_i2c_readw(unsigned short uiaddr, unsigned short *wdata)
{
  int ret;
  int i;
  unsigned short wt;
  //INC_MSG_PRINTF(INC_DEBUG_LEVEL, "dev_readw.........\n");

  wt = htons(uiaddr);
  for(i=0;i<DMB_I2C_RETRY_NUM;i++)
  {
    ret = write(hdev, &wt, 2);
    //INC_MSG_PRINTF(INC_DEBUG_LEVEL, "    write addr. ret=%d, i=%d\n", ret, i);
    if(ret>0)
      break;
    else
      DMB_LOGE("### write addr error.. ret=%d, try=%d\n", ret, i);
  }

  if(ret<=0)
  {
    INC_MSG_PRINTF(INC_DEBUG_LEVEL, "################################## write addr fail......\n");
    return -1;
  }

  
  // read what you want..
  for(i=0;i<DMB_I2C_RETRY_NUM;i++)
  {
    ret = read(hdev, &wt, 2);
    //INC_MSG_PRINTF(INC_DEBUG_LEVEL, "    read data. ret=%d, wdata=%04x, i=%d\n", ret, ntohs(wt), i);
    if(ret>0)
      break;
    else
      DMB_LOGE("### read data error....ret=%d, try=%d\n", ret, i);
  }

  if(ret <= 0)
  {
    DMB_LOGE("#### Error: read word.. data=%0x, ret=%d\n", wt, ret);
    return -1;
  }
  else
  {
    *wdata = ntohs(wt);
  }

  return ret;
}

#endif  
