/*
** =========================================================================
** File:
**     ImmVibeSPI.c
**
** Description: 
**     Device-dependent functions called by Immersion TSP API
**     to control PWM duty cycle, amp enable/disable, save IVT file, etc...
**
** Portions Copyright (c) 2008-2009 Immersion Corporation. All Rights Reserved. 
**
** This file contains Original Code and/or Modifications of Original Code 
** as defined in and that are subject to the GNU Public License v2 - 
** (the 'License'). You may not use this file except in compliance with the 
** License. You should have received a copy of the GNU General Public License 
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or contact 
** TouchSenseSales@immersion.com.
**
** The Original Code and all software distributed under the License are 
** distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER 
** EXPRESS OR IMPLIED, AND IMMERSION HEREBY DISCLAIMS ALL SUCH WARRANTIES, 
** INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS 
** FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT. Please see 
** the License for the specific language governing rights and limitations 
** under the License.
** =========================================================================
*/

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/io.h> 
#include <asm/io.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <mach/vreg.h>
#include <mach/gpio.h>
#include <mach/clk.h>
#include <tspdrv.h>
#include "pm.h"
#include <mach/msm_iomap.h>
#include <linux/regulator/consumer.h>


/*
** Constants used to set PWM frequency.  Current values result in a resonant
** frequency of 175 Hz and a signal frequency of 22.400 kHz
**
** OEM will need update these values of these variables to adjust
** the frequency of the LRA PWM frequency once Immersion has completed
** the tuning of the target device.
*/

/* Variable defined to allow for tuning of the handset. */

// 현재는 지원되지 않는 lib 
#define	VIBE_FALSE false
#define VIBE_TRUE	true

#define GPIO_LEVEL_HIGH	1
#define GPIO_LEVEL_LOW 	0

#define VIBE_DEBUG 0

#ifdef IMMVIBESPIAPI
#undef IMMVIBESPIAPI
#endif
#define IMMVIBESPIAPI static


#define NUM_ACTUATORS 1

#define GPMN_M_DEFAULT			7
#define GPMN_N_DEFAULT			6000
#define GPMN_D_DEFAULT			3000
#define PWM_MULTIPLIER			5980

#define GPMN_M_MASK				0x01FF
#define GPMN_N_MASK				0x1FFF
#define GPMN_D_MASK				0x1FFF

#define GP_MN_CLK_MDIV			0x004C
#define GP_MN_CLK_NDIV			0x0050
#define GP_MN_CLK_DUTY			0x0054


#define KTTECH_GPIO_MOTOR_GP_MN     (29)
#define KTTECH_GPIO_MOTOR_EN        (138)

/* Variable for toggling amplifier */
static bool g_bAmpEnabled = false;

/* Variable for setting PWM in Force Out Set */
VibeInt32 g_nForce_32 = 0;

static struct regulator *vreg_motor_3_0v;

#define VIBE_TUNING /* For temporary section for Tuning Work */
/*
** GP_CLK set macro
*/
#define VIBE_TUNING /* For temporary section for Tuning Work */
#ifdef VIBE_TUNING
/* hard code default 175 Hz parameters here */
VibeInt32 g_nLRA_PWM_M = GPMN_M_DEFAULT;	
VibeInt32 g_nLRA_PWM_N = GPMN_N_DEFAULT;	
VibeInt32 g_nLRA_PWM_D = (GPMN_N_DEFAULT >> 1);
VibeInt32 g_nLRA_PWM_Multiplier = PWM_MULTIPLIER;	
#endif
	
/*
** GP_MN set macro
*/
#define MSM_CLK_SIZE            SZ_4K
#define MSM_WEB_BASE            0x18800000
volatile void __iomem		    *MSM_CLK_BASE ;
	
#define REG_WRITEL(value, reg)	writel(value, (MSM_CLK_BASE+reg))

/*
** This function is used to set and re-set the GP_CLK M and N counters
** to output the desired target frequency.
**
*/
static VibeStatus vibe_set_pwm_freq(void)
{
    // pwm frequency setting.
    REG_WRITEL((g_nLRA_PWM_M & GPMN_M_MASK), GP_MN_CLK_MDIV);
    REG_WRITEL((~(g_nLRA_PWM_N - g_nLRA_PWM_M) & GPMN_N_MASK), GP_MN_CLK_NDIV);
    
    return VIBE_S_SUCCESS;
}

/*
** Called to disable amp (disable output force)
** OEM must review this function and verify logic level in Hardware
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_AmpDisable(VibeUInt8 nActuatorIndex)
{
//#error Please review the code between the #if and #endif
    int rc = 0;

	//printk(KERN_ERR "ImmVibeSPI_ForceOut_AmpDisable g_bAmpEnabled = %d \n", g_bAmpEnabled);

    if (g_bAmpEnabled)
    {
        gpio_tlmm_config(GPIO_CFG(KTTECH_GPIO_MOTOR_GP_MN, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
		
        gpio_set_value(KTTECH_GPIO_MOTOR_GP_MN, GPIO_LEVEL_LOW);
        gpio_set_value(KTTECH_GPIO_MOTOR_EN, GPIO_LEVEL_LOW);

		if (regulator_is_enabled(vreg_motor_3_0v)) {
          rc = regulator_disable(vreg_motor_3_0v);
		  if (rc) {
			  printk("%s: vreg_enable failed %d\n", __FUNCTION__, rc);
			  return VIBE_E_FAIL;
		  }
		  regulator_put(vreg_motor_3_0v);
		}

		//printk(KERN_INFO "ImmVibeSPI_ForceOut_AmpDisable \n");

        g_bAmpEnabled = false;
        udelay(100);
		
    }

  return VIBE_S_SUCCESS;
}

/*
** Called to enable amp (enable output force)
** OEM must review this function and verify logic level in Hardware
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_AmpEnable(VibeUInt8 nActuatorIndex)
{
    int rc = 0;

    //printk(KERN_ERR "ImmVibeSPI_ForceOut_AmpEnable g_bAmpEnabled = %d \n", g_bAmpEnabled);	
    if (!g_bAmpEnabled)
    {
        gpio_tlmm_config(GPIO_CFG(KTTECH_GPIO_MOTOR_GP_MN, 2, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_4MA), GPIO_CFG_ENABLE);

		vreg_motor_3_0v = regulator_get(NULL, "8901_l1");
		
		if (IS_ERR(vreg_motor_3_0v)) {
			printk("%s: regulator_get failed...\n", __FUNCTION__);
			return VIBE_E_FAIL;
		}
		
		rc = regulator_set_voltage(vreg_motor_3_0v, 3300000, 3300000);
		if (rc) {
			printk("%s: regulator_set_voltage... \n", __FUNCTION__);
			return VIBE_E_FAIL;
		}

        rc = regulator_enable(vreg_motor_3_0v);
		if (rc) {
			printk("%s: vreg_enable failed %d\n", __FUNCTION__, rc);
			return VIBE_E_FAIL;
		}

		udelay(100);

        /* Re-Set PWM frequency */
        vibe_set_pwm_freq(); 
        
        g_bAmpEnabled = true;

        /* Enable amp */
        gpio_set_value(KTTECH_GPIO_MOTOR_EN, GPIO_LEVEL_HIGH);
		udelay(100);

		//printk(KERN_INFO "ImmVibeSPI_ForceOut_AmpEnable \n");

    }

    return VIBE_S_SUCCESS;
}

/*
** Called at initialization time to set PWM freq, disable amp, etc...
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_Initialize(void)
{
//#error Please review the code between the #if and #endif

	/* MOT EN setting */
    //gpio_tlmm_config(GPIO_CFG(30, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
  	gpio_tlmm_config(GPIO_CFG(KTTECH_GPIO_MOTOR_EN, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_4MA), GPIO_CFG_ENABLE);
    printk(KERN_ERR "ImmVibeSPI_ForceOut_Initialize.\n");


    MSM_CLK_BASE = ioremap(MSM_WEB_BASE, MSM_CLK_SIZE);

#if 0
    vreg_motor_3_0v = regulator_get(NULL, "8901_l1");
 
    if (IS_ERR(vreg_motor_3_0v)) {
		printk("%s: regulator_get...\n", __FUNCTION__);
    	return VIBE_E_FAIL;
    }
    
    rc = regulator_set_voltage(vreg_motor_3_0v,	3000000, 3000000);
    if (rc) {
    	printk("%s: regulator_set_voltage... \n", __FUNCTION__);
    	return VIBE_E_FAIL;
    }

	mdelay(1);
#endif
    //g_bAmpEnabled = true;   /* to force ImmVibeSPI_ForceOut_AmpDisable disabling the amp */

    /* 
    ** Disable amp.
    ** If multiple actuators are supported, please make sure to call ImmVibeSPI_ForceOut_AmpDisable
    ** for each actuator (provide the actuator index as input argument).
    */
    //ImmVibeSPI_ForceOut_AmpDisable(0);


    return VIBE_S_SUCCESS;
}

/*
** Called at termination time to set PWM freq, disable amp, etc...
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_Terminate(void)
{
    DbgOut((KERN_DEBUG "ImmVibeSPI_ForceOut_Terminate.\n"));

    /* 
    ** Disable amp.
    ** If multiple actuators are supported, please make sure to call ImmVibeSPI_ForceOut_AmpDisable
    ** for each actuator (provide the actuator index as input argument).
    */
    ImmVibeSPI_ForceOut_AmpDisable(0);

	if( MSM_CLK_BASE )
	  iounmap( MSM_CLK_BASE ) ;

    return VIBE_S_SUCCESS;
}

/*
** Called by the real-time loop to set PWM duty cycle, and enable amp if required
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_SetSamples(VibeUInt8 nActuatorIndex, VibeUInt16 nOutputSignalBitDepth, VibeUInt16 nBufferSizeInBytes, VibeInt8* pForceOutputBuffer)
{
    VibeInt8 nForce;

    switch (nOutputSignalBitDepth)
    {
        case 8:
            /* pForceOutputBuffer is expected to contain 1 byte */
            if (nBufferSizeInBytes != 1)
			{
				DbgOut((KERN_ERR "[ImmVibeSPI] ImmVibeSPI_ForceOut_SetSamples nBufferSizeInBytes =  %d \n", nBufferSizeInBytes ));
				return VIBE_E_FAIL;
            }
            nForce = pForceOutputBuffer[0];
            break;
        case 16:
            /* pForceOutputBuffer is expected to contain 2 byte */
            if (nBufferSizeInBytes != 2) return VIBE_E_FAIL;

            /* Map 16-bit value to 8-bit */
            nForce = ((VibeInt16*)pForceOutputBuffer)[0] >> 8;
            break;
        default:
            /* Unexpected bit depth */
            return VIBE_E_FAIL;
    }

	//printk("ImmVibeSPI_ForceOut_Set nForce = %d \n", nForce);
  
    if(nForce == 0)
    {
      /* Set 50% duty cycle */
      REG_WRITEL((g_nLRA_PWM_D & GPMN_D_MASK), GP_MN_CLK_DUTY);
    }
    else
    {
      REG_WRITEL(((((g_nLRA_PWM_Multiplier * nForce) >> 8) + g_nLRA_PWM_D) & GPMN_D_MASK), GP_MN_CLK_DUTY);
    }
    return VIBE_S_SUCCESS;
}

/*
** Called to get the device name (device name must be returned as ANSI char)
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_Device_GetName(VibeUInt8 nActuatorIndex, char *szDevName, int nSize)
{
    return VIBE_S_SUCCESS;
}

