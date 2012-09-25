/* Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
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
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/switch.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>

#include <linux/mfd/pmic8058.h>
#include <linux/pmic8058-othc.h>
#include <linux/m_adc.h>

#ifdef CONFIG_MACH_KTTECH
#include <mach/board.h>
#include <linux/wakelock.h>
#endif

#ifdef CONFIG_KTTECH_SND_CODEC_WM8993
#include <mach/qdsp6v2/wm8993_kttech.h>
#include <mach/qdsp6v2/audio_dev_ctl.h>
#endif /*CONFIG_KTTECH_SND_CODEC_WM8993*/

#if !defined(KTTECH_FINAL_BUILD) && defined(OTHC_DEBUG_LOG)
#define OTHC_ERR(fmt, args...)	pr_err("" fmt, ##args)
#define OTHC_INFO(fmt, args...)	pr_info("" fmt, ##args)
#define OTHC_DBG(fmt, args...)	pr_debug("" fmt, ##args)
#else
#define OTHC_ERR(fmt, args...)	pr_err("" fmt, ##args)
#define OTHC_INFO(x...)	do{} while(0)
#define OTHC_DBG(x...)	do{} while(0)
#endif

#ifdef CONFIG_KTTECH_HEADSET
#define OTHC_NOMIC_HEADSET
#endif /*CONFIG_KTTECH_HEADSET*/

#define PM8058_OTHC_LOW_CURR_MASK	0xF0
#define PM8058_OTHC_HIGH_CURR_MASK	0x0F
#define PM8058_OTHC_EN_SIG_MASK		0x3F
#define PM8058_OTHC_HYST_PREDIV_MASK	0xC7
#define PM8058_OTHC_CLK_PREDIV_MASK	0xF8
#define PM8058_OTHC_HYST_CLK_MASK	0x0F
#define PM8058_OTHC_PERIOD_CLK_MASK	0xF0

#define PM8058_OTHC_LOW_CURR_SHIFT	0x4
#define PM8058_OTHC_EN_SIG_SHIFT	0x6
#define PM8058_OTHC_HYST_PREDIV_SHIFT	0x3
#define PM8058_OTHC_HYST_CLK_SHIFT	0x4

#define OTHC_GPIO_MAX_LEN		25

struct pm8058_othc {
	bool othc_sw_state;
	bool switch_reject;
	bool othc_support_n_switch;
	bool accessory_support;
	bool accessories_adc_support;
	int othc_base;
	int othc_irq_sw;
	int othc_irq_ir;
#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun - irq_gpio
	int othc_irgpio_mickeydetect;
	int othc_irgpio_eardetect;
#endif /*CONFIG_KTTECH_HEADSET*/
	int othc_ir_state;
	int num_accessories;
	int curr_accessory_code;
	int curr_accessory;
	int video_out_gpio;
	u32 sw_key_code;
	u32 accessories_adc_channel;
	unsigned long switch_debounce_ms;
	unsigned long detection_delay_ms;
	void *adc_handle;
	void *accessory_adc_handle;
	spinlock_t lock;
	struct regulator *othc_vreg;
	struct input_dev *othc_ipd;
	struct switch_dev othc_sdev;
	struct pmic8058_othc_config_pdata *othc_pdata;
	struct othc_accessory_info *accessory_info;
	struct hrtimer timer;
	struct othc_n_switch_config *switch_config;
	struct pm8058_chip *pm_chip;
	struct work_struct switch_work;
	struct delayed_work detect_work;
#ifdef CONFIG_KTTECH_HEADSET
	bool mic_on, hs_on;
	struct hrtimer timer_nomic;
	struct hrtimer timer_key;
	struct wake_lock hs_idlelock;
	spinlock_t input_lock;
#endif /*CONFIG_KTTECH_HEADSET*/
};

static struct pm8058_othc *config[OTHC_MICBIAS_MAX];

#ifdef CONFIG_KTTECH_HEADSET
static enum O3_HW_VER hw_ver;
static struct mutex micbias_lock;
static struct mutex sw_set_lock;
static struct mutex state_lock;
static bool run_detect;

static struct workqueue_struct *g_nomic_detection_work_queue;
static void nomic_detection_work(struct work_struct *work);
static DECLARE_WORK(g_nomic_detection_work, nomic_detection_work);
#endif /*CONFIG_KTTECH_HEADSET*/

/*
 * The API pm8058_micbias_enable() allows to configure
 * the MIC_BIAS. Only the lines which are not used for
 * headset detection can be configured using this API.
 * The API returns an error code if it fails to configure
 * the specified MIC_BIAS line, else it returns 0.
 */
int pm8058_micbias_enable(enum othc_micbias micbias,
		enum othc_micbias_enable enable)
{
	int rc;
	u8 reg;
	struct pm8058_othc *dd = config[micbias];

OTHC_INFO("micbias= %d, enable = %d\n", micbias, enable);

	if (dd == NULL) {
		pr_err("MIC_BIAS not registered, cannot enable\n");
		return -ENODEV;
	}

	if (dd->othc_pdata->micbias_capability != OTHC_MICBIAS) {
		pr_err("MIC_BIAS enable capability not supported\n");
		return -EINVAL;
	}

#ifdef CONFIG_KTTECH_HEADSET
	mutex_lock(&micbias_lock);
#endif /*CONFIG_KTTECH_HEADSET*/

	rc = pm8058_read(dd->pm_chip, dd->othc_base + 1, &reg, 1);
	if (rc < 0) {
#ifdef CONFIG_KTTECH_HEADSET
		mutex_unlock(&micbias_lock);
#endif /*CONFIG_KTTECH_HEADSET*/
		pr_err("PM8058 read failed\n");
		return rc;
	}

	reg &= PM8058_OTHC_EN_SIG_MASK;
	reg |= (enable << PM8058_OTHC_EN_SIG_SHIFT);

	rc = pm8058_write(dd->pm_chip, dd->othc_base + 1, &reg, 1);
	if (rc < 0) {
#ifdef CONFIG_KTTECH_HEADSET
		mutex_unlock(&micbias_lock);
#endif /*CONFIG_KTTECH_HEADSET*/
		pr_err("PM8058 write failed\n");
		return rc;
	}

#ifdef CONFIG_KTTECH_HEADSET
	mutex_unlock(&micbias_lock);
#endif /*CONFIG_KTTECH_HEADSET*/

	return rc;
}
EXPORT_SYMBOL(pm8058_micbias_enable);

int pm8058_othc_svideo_enable(enum othc_micbias micbias, bool enable)
{
	struct pm8058_othc *dd = config[micbias];

OTHC_INFO("micbias= %d, enable = %d\n", micbias, enable);

	if (dd == NULL) {
		pr_err("MIC_BIAS not registered, cannot enable\n");
		return -ENODEV;
	}

	if (dd->othc_pdata->micbias_capability != OTHC_MICBIAS_HSED) {
		pr_err("MIC_BIAS enable capability not supported\n");
		return -EINVAL;
	}

	if (dd->accessories_adc_support) {
		/* GPIO state for MIC_IN = 0, SVIDEO = 1 */
		gpio_set_value_cansleep(dd->video_out_gpio, !!enable);
		if (enable) {
			pr_debug("Enable the video path\n");
			switch_set_state(&dd->othc_sdev, dd->curr_accessory);
			input_report_switch(dd->othc_ipd, dd->curr_accessory_code, 1);
			input_sync(dd->othc_ipd);
		} else {
			pr_debug("Disable the video path\n");
			switch_set_state(&dd->othc_sdev, 0);
			input_report_switch(dd->othc_ipd, dd->curr_accessory_code, 0);
			input_sync(dd->othc_ipd);
		}
	}

	return 0;
}
EXPORT_SYMBOL(pm8058_othc_svideo_enable);

#ifdef CONFIG_PM
static int pm8058_othc_suspend(struct device *dev)
{
	int rc = 0;
	struct pm8058_othc *dd = dev_get_drvdata(dev);

OTHC_INFO("micbias_capability = %d\n", dd->othc_pdata->micbias_capability);

	if (dd->othc_pdata->micbias_capability == OTHC_MICBIAS_HSED) {
		if (device_may_wakeup(dev)) {
			enable_irq_wake(dd->othc_irq_sw);
			enable_irq_wake(dd->othc_irq_ir);
		}
	}

	if (!device_may_wakeup(dev)) {
		rc = regulator_disable(dd->othc_vreg);
		if (rc)
			pr_err("othc micbais power off failed\n");
	}

	return rc;
}

static int pm8058_othc_resume(struct device *dev)
{
	int rc = 0;
	struct pm8058_othc *dd = dev_get_drvdata(dev);

OTHC_INFO("micbias_capability = %d\n", dd->othc_pdata->micbias_capability);

	if (dd->othc_pdata->micbias_capability == OTHC_MICBIAS_HSED) {
		if (device_may_wakeup(dev)) {
			disable_irq_wake(dd->othc_irq_sw);
			disable_irq_wake(dd->othc_irq_ir);
		}
	}

	if (!device_may_wakeup(dev)) {
		rc = regulator_enable(dd->othc_vreg);
		if (rc)
			pr_err("othc micbais power on failed\n");
	}

	return rc;
}

static struct dev_pm_ops pm8058_othc_pm_ops = {
	.suspend = pm8058_othc_suspend,
	.resume = pm8058_othc_resume,
};
#endif

static int __devexit pm8058_othc_remove(struct platform_device *pd)
{
	struct pm8058_othc *dd = platform_get_drvdata(pd);

OTHC_INFO("micbias_capability = %d\n", dd->othc_pdata->micbias_capability);

	pm_runtime_set_suspended(&pd->dev);
	pm_runtime_disable(&pd->dev);

	if (dd->othc_pdata->micbias_capability == OTHC_MICBIAS_HSED) {
		device_init_wakeup(&pd->dev, 0);
		if (dd->othc_support_n_switch == true) {
			adc_channel_close(dd->adc_handle);
			cancel_work_sync(&dd->switch_work);
		}

#ifndef CONFIG_KTTECH_HEADSET // 20110423 by ssgun - irq_gpio
		if (dd->accessory_support == true) {
			int i;
			for (i = 0; i < dd->num_accessories; i++) {
				if (dd->accessory_info[i].detect_flags & OTHC_GPIO_DETECT)
					gpio_free(dd->accessory_info[i].gpio);
			}
		}
#endif /*CONFIG_KTTECH_HEADSET*/
		cancel_delayed_work_sync(&dd->detect_work);
		free_irq(dd->othc_irq_sw, dd);
		free_irq(dd->othc_irq_ir, dd);
#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun - irq_gpio
		if (dd->othc_irgpio_mickeydetect != -1) {
			gpio_free(dd->othc_irgpio_mickeydetect);
		}
		if (dd->othc_irgpio_eardetect != -1) {
			gpio_free(dd->othc_irgpio_eardetect);
		}
		if(hw_ver < O3_PP1) {
			gpio_free(106);
		}

		wake_lock_destroy(&dd->hs_idlelock);

		if(g_nomic_detection_work_queue)
			destroy_workqueue(g_nomic_detection_work_queue);
#endif /*CONFIG_KTTECH_HEADSET*/
		input_unregister_device(dd->othc_ipd);
	}
	regulator_disable(dd->othc_vreg);
	regulator_put(dd->othc_vreg);

	kfree(dd);

	return 0;
}

static enum hrtimer_restart pm8058_othc_timer(struct hrtimer *timer)
{
	unsigned long flags;
	struct pm8058_othc *dd = container_of(timer, struct pm8058_othc, timer);

OTHC_INFO("%s : %d\n", __func__, dd->othc_irgpio_mickeydetect);

	spin_lock_irqsave(&dd->lock, flags);
	dd->switch_reject = false;
	spin_unlock_irqrestore(&dd->lock, flags);

	return HRTIMER_NORESTART;
}

#ifdef CONFIG_KTTECH_HEADSET // 20110428 by ssgun
static enum hrtimer_restart pm8058_othc_timer_nomic(struct hrtimer *timer)
{
#if 0 // 20110506 by ssgun
	int rc = 0, level = 0;
	struct pm8058_othc *dd = config[OTHC_MICBIAS_2];

	if(!run_detect || !dd) {
		pr_err("Unable to configure headset data\n");
		return HRTIMER_NORESTART;
	}

	if (dd->hs_on == 0) {
		pr_err("Rejected None Headset\n");
		return HRTIMER_NORESTART;
	}

	// NO_MIC_HEADSET 처리를 위해 3초 WAKELOCK을 설정한다.
	wake_lock_timeout(&dd->hs_idlelock, 3*HZ);

	if (dd->othc_irgpio_mickeydetect < 0) {
		OTHC_INFO("Check the MIC_BIAS status, to check if inserted or removed\n");

		/* Check the MIC_BIAS status, to check if inserted or removed */
		level = pm8058_irq_get_rt_status(dd->pm_chip, dd->othc_irq_sw);
		if (level < 0) {
			pr_err("Unable to read IRQ status register\n");
			goto fail_ir; //return HRTIMER_NORESTART;
		}
	} else {
		OTHC_INFO("Check the GPIO_%d status, to check if inserted or removed\n",
					dd->othc_irgpio_mickeydetect);

		// Key Press - GPIO_46 값이 0일 경우 Key Press
		// 전제 조건으로 Headset 연결 및 Mic가 있는 경우
		level = gpio_get_value_cansleep(dd->othc_irgpio_mickeydetect);
		if (level < 0) {
			pr_err("Unable to read IR GPIO\n");
			goto fail_ir; //return HRTIMER_NORESTART;
		}
		level = !level;
	}
	OTHC_INFO("NO_MIC_HEADSET Detected Level = %d\n", level);

	if(level) {
		do {
		//mutex_lock(&sw_set_lock);
		dd->curr_accessory = OTHC_HEADSET; //OTHC_HEADPHONE;
		dd->curr_accessory_code = SW_HEADPHONE_INSERT;
		dd->mic_on = 0;
		dd->hs_on = 1;

		switch_set_state(&dd->othc_sdev, 2);
		input_report_switch(dd->othc_ipd, dd->curr_accessory_code, 1);
		input_sync(dd->othc_ipd);
		//mutex_unlock(&sw_set_lock);
		} while(0);

		// NO_MIC_HEADSET 일 경우 MICBIAS_1을 꺼야 한다.
		msleep(100);
		rc = pm8058_micbias_enable(OTHC_MICBIAS_1, OTHC_SIGNAL_OFF);
		if(rc)
			pr_err("Disabling earmic power failed\n");
		else
			pr_debug("Disabling earmic power success\n");

OTHC_ERR("********** NO_MIC_HEADSET DETECT & REPORT **********\n");
	} else {
		dd->mic_on = dd->hs_on = 1; // check

		spin_lock_irqsave(&dd->lock, flags);
		dd->switch_reject = false;
		spin_unlock_irqrestore(&dd->lock, flags);

OTHC_ERR("********** HEADSET DETECT **********\n");
	}

fail_ir:
	return HRTIMER_NORESTART;
#else
	queue_work(g_nomic_detection_work_queue, &g_nomic_detection_work);
	return HRTIMER_NORESTART;
#endif
}

static enum hrtimer_restart pm8058_othc_timer_key(struct hrtimer *timer)
{
	int level = 0;
	struct pm8058_othc *dd = config[OTHC_MICBIAS_2];

	if(!run_detect || !dd) {
		pr_err("Unable to configure headset data\n");
		return HRTIMER_NORESTART;
	}

	if (dd->hs_on == 0) {
		pr_err("Rejected None Headset\n");
		return HRTIMER_NORESTART;
	}

	wake_lock_timeout(&dd->hs_idlelock, 5*HZ);

	if (dd->othc_irgpio_mickeydetect < 0) {
		OTHC_INFO("Check the MIC_BIAS status, to check if pressed or released\n");

		/* Check the MIC_BIAS status, to check if inserted or removed */
		level = pm8058_irq_get_rt_status(dd->pm_chip, dd->othc_irq_sw);
		if (level < 0) {
			pr_err("Unable to read IRQ status register\n");
			return HRTIMER_NORESTART;
		}
	} else {
		OTHC_INFO("Check the GPIO_%d status, to check if pressed or released\n",
					dd->othc_irgpio_mickeydetect);

		// Key Press - GPIO_46 값이 0일 경우 Key Press
		// 전제 조건으로 Headset 연결 및 Mic가 있는 경우
		level = gpio_get_value_cansleep(dd->othc_irgpio_mickeydetect);
		if (level < 0) {
			pr_err("Unable to read IR GPIO\n");
			return HRTIMER_NORESTART;
		}
		level = !level;
	}
	OTHC_INFO("Headset Long Key Level = %d\n", level);

	if(level) {
		unsigned long flags;

		spin_lock_irqsave(&dd->lock, flags);
		dd->othc_sw_state = true;
		input_report_key(dd->othc_ipd, KEY_MEDIA, 1);
		input_sync(dd->othc_ipd);
		spin_unlock_irqrestore(&dd->lock, flags);
		pr_debug("Switch has been pressed\n");
	}

	return HRTIMER_NORESTART;
}
#endif /*CONFIG_KTTECH_HEADSET*/

#ifndef CONFIG_KTTECH_HEADSET
static void othc_report_switch(struct pm8058_othc *dd, u32 res)
{
	u8 i;
	struct othc_switch_info *sw_info = dd->switch_config->switch_info;

OTHC_INFO("num_keys = %d\n", dd->switch_config->num_keys);

	for (i = 0; i < dd->switch_config->num_keys; i++) {
		if (res >= sw_info[i].min_adc_threshold &&
				res <= sw_info[i].max_adc_threshold) {
			dd->othc_sw_state = true;
			dd->sw_key_code = sw_info[i].key_code;
			input_report_key(dd->othc_ipd, sw_info[i].key_code, 1);
			input_sync(dd->othc_ipd);
			return;
		}
	}
}
#endif /*CONFIG_KTTECH_HEADSET*/

static void switch_work_f(struct work_struct *work)
{
#ifdef CONFIG_KTTECH_HEADSET
	unsigned long flags;
	struct pm8058_othc *dd = config[OTHC_MICBIAS_2];
#else
	int rc, i;
	u32 res = 0;
	struct adc_chan_result adc_result;
	struct pm8058_othc *dd = container_of(work, struct pm8058_othc, switch_work);
	DECLARE_COMPLETION_ONSTACK(adc_wait);
	u8 num_adc_samples = dd->switch_config->num_adc_samples;
#endif /*CONFIG_KTTECH_HEADSET*/

#ifdef CONFIG_KTTECH_HEADSET
	if(!run_detect || !dd) {
		pr_err("Unable to configure headset data\n");
		goto done;
	}

	if(dd->hs_on == 0 || dd->mic_on == 0) {
		pr_err("Rejected None Headset\n");
		goto done;
	}

	wake_lock_timeout(&dd->hs_idlelock, 5*HZ);

	spin_lock_irqsave(&dd->lock, flags);
	dd->othc_sw_state = false;
	input_report_key(dd->othc_ipd, KEY_MEDIA, 0);
	input_sync(dd->othc_ipd);
	spin_unlock_irqrestore(&dd->lock, flags);
	pr_debug("Switch has been released\n");
#else
	/* sleep for settling time */
	msleep(dd->switch_config->voltage_settling_time_ms);

	for (i = 0; i < num_adc_samples; i++) {
		rc = adc_channel_request_conv(dd->adc_handle, &adc_wait);
		if (rc) {
			pr_err("adc_channel_request_conv failed\n");
			goto bail_out;
		}
		rc = wait_for_completion_interruptible(&adc_wait);
		if (rc) {
			pr_err("wait_for_completion_interruptible failed\n");
			goto bail_out;
		}
		rc = adc_channel_read_result(dd->adc_handle, &adc_result);
		if (rc) {
			pr_err("adc_channel_read_result failed\n");
			goto bail_out;
		}
		res += adc_result.physical;
	}
bail_out:
	if (i == num_adc_samples && num_adc_samples != 0) {
		res /= num_adc_samples;
		othc_report_switch(dd, res);
	} else
		pr_err("Insufficient ADC samples\n");
#endif /*CONFIG_KTTECH_HEADSET*/

#ifdef CONFIG_KTTECH_HEADSET
done:
#endif /*CONFIG_KTTECH_HEADSET*/
	enable_irq(dd->othc_irq_sw);
}

#ifndef CONFIG_KTTECH_HEADSET // 20110423 by ssgun
static int accessory_adc_detect(struct pm8058_othc *dd, int accessory)
{
	int rc;
	u32 res;
	struct adc_chan_result accessory_adc_result;
	DECLARE_COMPLETION_ONSTACK(accessory_adc_wait);

OTHC_INFO("Don't Used ADC\n");

	rc = adc_channel_request_conv(dd->accessory_adc_handle, &accessory_adc_wait);
	if (rc) {
		pr_err("adc_channel_request_conv failed\n");
		goto adc_failed;
	}
	rc = wait_for_completion_interruptible(&accessory_adc_wait);
	if (rc) {
		pr_err("wait_for_completion_interruptible failed\n");
		goto adc_failed;
	}
	rc = adc_channel_read_result(dd->accessory_adc_handle,
						&accessory_adc_result);
	if (rc) {
		pr_err("adc_channel_read_result failed\n");
		goto adc_failed;
	}

	res = accessory_adc_result.physical;

	if (res >= dd->accessory_info[accessory].adc_thres.min_threshold &&
		res <= dd->accessory_info[accessory].adc_thres.max_threshold) {
		pr_debug("Accessory on ADC detected!, ADC Value = %u\n", res);
		return 1;
	}

adc_failed:
	return 0;
}
#endif /*CONFIG_KTTECH_HEADSET*/

static int pm8058_accessory_report(struct pm8058_othc *dd, int status)
{
#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun
	int rc, detected = 0;
#else
	int i, rc, detected = 0;
#endif /*CONFIG_KTTECH_HEADSET*/
	u8 micbias_status, switch_status;

	if (dd->accessory_support == false) {
		/* Report default headset */
#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun
		do {
		mutex_lock(&sw_set_lock);
		dd->mic_on = dd->hs_on = !!status;
		switch_set_state(&dd->othc_sdev, !!status);
#else
		switch_set_state(&dd->othc_sdev, !!status);
#endif /*CONFIG_KTTECH_HEADSET*/
		input_report_switch(dd->othc_ipd, SW_HEADPHONE_INSERT,
							!!status);
		input_sync(dd->othc_ipd);
		mutex_unlock(&sw_set_lock);
		} while(0);
#ifdef CONFIG_KTTECH_HEADSET // 20110321 by ssgun - MICBIAS
		goto success_set_bias;
#else
		return 0;
#endif /*CONFIG_KTTECH_HEADSET*/
	}

	/* For accessory */
	if (dd->accessory_support == true && status == 0) {
		/* Report removal of the accessory. */

		/*
		 * If the current accessory is video cable, reject the removal
		 * interrupt.
		 */
		pr_debug("Accessory [%d] removed\n", dd->curr_accessory);
		if (dd->curr_accessory == OTHC_SVIDEO_OUT)
			return 0;

#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun
		do {
		mutex_lock(&sw_set_lock);
		dd->mic_on = dd->hs_on = 0;
		switch_set_state(&dd->othc_sdev, 0);
#else
		switch_set_state(&dd->othc_sdev, 0);
#endif /*CONFIG_KTTECH_HEADSET*/
		input_report_switch(dd->othc_ipd, dd->curr_accessory_code, 0);
		input_sync(dd->othc_ipd);
		mutex_unlock(&sw_set_lock);
		} while(0);
#ifdef CONFIG_KTTECH_HEADSET // 20110321 by ssgun - MICBIAS
		goto success_set_bias;
#else
		return 0;
#endif /*CONFIG_KTTECH_HEADSET*/
	}

#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun - irq_gpio
	if (dd->othc_irgpio_eardetect < 0) {
		/* Check the MIC_BIAS status */
		rc = pm8058_irq_get_rt_status(dd->pm_chip, dd->othc_irq_ir);
		if (rc < 0) {
			pr_err("Unable to read IR status from PMIC\n");
			goto fail_ir_accessory;
		}
		micbias_status = !!rc;
	} else {
		rc = gpio_get_value_cansleep(dd->othc_irgpio_eardetect);
		if (rc < 0) {
			pr_err("Unable to read IR status from GPIO\n");
			goto fail_ir_accessory;
		}
		micbias_status = !rc;
	}
#else
	/* Check the MIC_BIAS status */
	rc = pm8058_irq_get_rt_status(dd->pm_chip, dd->othc_irq_ir);
	if (rc < 0) {
		pr_err("Unable to read IR status from PMIC\n");
		goto fail_ir_accessory;
	}
	micbias_status = !!rc;
#endif /*CONFIG_KTTECH_HEADSET*/

#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun - irq_gpio
	if (dd->othc_irgpio_mickeydetect < 0) {
		/* Check the switch status */
		rc = pm8058_irq_get_rt_status(dd->pm_chip, dd->othc_irq_sw);
		if (rc < 0) {
			pr_err("Unable to read SWITCH status\n");
			goto fail_ir_accessory;
		}
		switch_status = !!rc;
	} else {
		rc = gpio_get_value_cansleep(dd->othc_irgpio_mickeydetect);
		if (rc < 0) {
			pr_err("Unable to read IR status from GPIO\n");
			goto fail_ir_accessory;
		}
		switch_status = !rc;
	}
#else
	/* Check the switch status */
	rc = pm8058_irq_get_rt_status(dd->pm_chip, dd->othc_irq_sw);
	if (rc < 0) {
		pr_err("Unable to read SWITCH status\n");
		goto fail_ir_accessory;
	}
	switch_status = !!rc;
#endif /*CONFIG_KTTECH_HEADSET*/

#ifdef CONFIG_KTTECH_HEADSET
	rc = gpio_get_value_cansleep(dd->othc_irgpio_eardetect);
	rc = !rc; // active low
	if (rc)
		detected = 1;
	else
		detected = 0;
#else
	/* Loop through to check which accessory is connected */
	for (i = 0; i < dd->num_accessories; i++) {
		detected = 0;
		if (dd->accessory_info[i].enabled == false)
			continue;

		if (dd->accessory_info[i].detect_flags & OTHC_MICBIAS_DETECT) {
			pr_debug("[MICBIAS] accessory_info[%d].gpio_%d = %d\n", i, dd->accessory_info[i].gpio, rc); 
			if (micbias_status)
				detected = 1;
			else
				continue;
		}
		if (dd->accessory_info[i].detect_flags & OTHC_SWITCH_DETECT) {
			pr_debug("[SWITCH] accessory_info[%d].gpio_%d = %d\n", i, dd->accessory_info[i].gpio, rc); 
			if (switch_status)
				detected = 1;
			else
				continue;
		}
		if (dd->accessory_info[i].detect_flags & OTHC_GPIO_DETECT) {
			rc = gpio_get_value_cansleep(dd->accessory_info[i].gpio);
			if (rc < 0)
				continue;

 			pr_debug("[GPIO] accessory_info[%d].gpio_%d = %d\n", i, dd->accessory_info[i].gpio, rc); 
			if (rc ^ dd->accessory_info[i].active_low)
				detected = 1;
			else
				continue;
		}
		if (dd->accessory_info[i].detect_flags & OTHC_ADC_DETECT)
			detected = accessory_adc_detect(dd, i);

		if (detected)
			break;
	}
#endif /*CONFIG_KTTECH_HEADSET*/

	if (detected) {
#ifdef CONFIG_KTTECH_HEADSET
		dd->curr_accessory = OTHC_HEADSET;
		dd->curr_accessory_code = SW_HEADPHONE_INSERT;
#else
		dd->curr_accessory = dd->accessory_info[i].accessory;
		dd->curr_accessory_code = dd->accessory_info[i].key_code;
#endif /*CONFIG_KTTECH_HEADSET*/

		/* if Video out cable detected enable the video path*/
		if (dd->curr_accessory == OTHC_SVIDEO_OUT) {
			pm8058_othc_svideo_enable(dd->othc_pdata->micbias_select, true);
		} else {
#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun
			do {
			mutex_lock(&sw_set_lock);
			dd->mic_on = dd->hs_on = 1;
			switch_set_state(&dd->othc_sdev, 1);
#else
			switch_set_state(&dd->othc_sdev, dd->curr_accessory);
#endif /*CONFIG_KTTECH_HEADSET*/
			input_report_switch(dd->othc_ipd, dd->curr_accessory_code, 1);
			input_sync(dd->othc_ipd);
			mutex_unlock(&sw_set_lock);
			} while(0);
OTHC_ERR("********** HEADSET DETECT & REPORT **********\n");
		}
		pr_debug("Accessory [%d] inserted\n", dd->curr_accessory);
	} else {
		pr_debug("Unable to detect accessory. False interrupt!\n");
	}

#ifdef CONFIG_KTTECH_HEADSET // 20110321 by ssgun - MICBIAS
success_set_bias:
	if(status) {
		rc = pm8058_micbias_enable(OTHC_MICBIAS_1, OTHC_SIGNAL_ALWAYS_ON);
		if(rc)
			pr_err("Enabling earmic power failed\n");
		else {
			pr_debug("Enabling earmic power success\n");
		}
	} else {
		rc = pm8058_micbias_enable(OTHC_MICBIAS_1, OTHC_SIGNAL_OFF);
		if(rc)
			pr_err("Disabling earmic power failed\n");
		else
			pr_debug("Disabling earmic power success\n");
	}

#ifdef OTHC_NOMIC_HEADSET
#ifdef OTHC_NOMICHS_DELAY // 20110501 by ssgun - test code : timer -> delay
	if(status) {
			int nomic_cnt = 0;
			bool nomic_hs =  false;

			while(++nomic_cnt < 3) {
				rc = gpio_get_value_cansleep(dd->othc_irgpio_mickeydetect);
OTHC_INFO("[%d] GPIO_%d = %d\n", nomic_cnt, dd->othc_irgpio_mickeydetect, rc);
				if(rc == 0) {
					nomic_hs = true;
				} else {
					nomic_hs = false;
					break;
				}

				msleep(1000);
			};

			if(nomic_hs) {
				do {
				mutex_lock(&sw_set_lock);
				dd->mic_on = 0;
				dd->hs_on = 1;

				switch_set_state(&dd->othc_sdev, 2);
				input_report_switch(dd->othc_ipd, dd->curr_accessory_code, 1);
				input_sync(dd->othc_ipd);
				mutex_unlock(&sw_set_lock);
				} while(0);

				msleep(100);
				rc = pm8058_micbias_enable(OTHC_MICBIAS_1, OTHC_SIGNAL_OFF);
				if(rc)
					pr_err("Disabling earmic power failed\n");
				else
					pr_debug("Disabling earmic power success\n");
OTHC_INFO("********** NO_MIC_HEADSET DETECT & REPORT **********\n");
			}
		}
#else // timer - fatal error : local_bh_enable
	if(status) {
		unsigned long flags;

		/* Enable the switch reject flag */
		spin_lock_irqsave(&dd->lock, flags);
		dd->switch_reject = true;
		spin_unlock_irqrestore(&dd->lock, flags);

		/* Start the HR timer if one is not active */
		if (hrtimer_active(&dd->timer_nomic)) {
			pr_debug("Cancel timer_nomic\n");
			hrtimer_cancel(&dd->timer_nomic);
		}

		// 3초후에도 GPIO_46 값이 LOW(0)일 경우 NO_MIC_HEADSET으로 처리한다.
		hrtimer_start(&dd->timer_nomic, ktime_set(3, 0), HRTIMER_MODE_REL); // 3sec
	}
#endif /*OTHC_NOMICHS_DELAY*/
#endif /*OTHC_NOMIC_HEADSET*/
#endif /*CONFIG_KTTECH_HEADSET*/

	return 0;

fail_ir_accessory:
	return rc;
}

static void detect_work_f(struct work_struct *work)
{
	int rc;
#ifndef CONFIG_KTTECH_HEADSET
	struct pm8058_othc *dd =
		container_of(work, struct pm8058_othc, detect_work.work);
#else // 구조체 설정에 따라 인덱스 확인할 것.
	struct pm8058_othc *dd = config[OTHC_MICBIAS_2];

	if(!run_detect || !dd) {
		pr_err("Unable to configure headset data\n");
		goto done;
	}

	wake_lock_timeout(&dd->hs_idlelock, 10*HZ);
#endif /*CONFIG_KTTECH_HEADSET*/

	if (dd->othc_ir_state) {
#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun
		if(dd->hs_on) {
			pr_debug("[GPIO] Check if the accessory is already inserted\n");
			goto done;
		}
#endif /*CONFIG_KTTECH_HEADSET*/

		/* inserted */
		rc = pm8058_accessory_report(dd, 1);
		if (rc) {
			pr_err("Accessory could not be detected\n");
		} else {
			pr_debug("Headset Inserted\n");
		}
	} else {
#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun
		if(dd->hs_on == 0) {
			OTHC_INFO("[GPIO] Check if the accessory is already inserted\n");
			dd->othc_sw_state = false;
			goto done;
		}
#endif /*CONFIG_KTTECH_HEADSET*/

		/* removed */
		rc = pm8058_accessory_report(dd, 0);
		if (rc) {
			pr_err("Accessory could not be detected\n");
		} else {
			pr_debug("Headset Removed\n");
		}
		/* Clear existing switch state */
		dd->othc_sw_state = false;
	}

#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun
done:
#endif /*CONFIG_KTTECH_HEADSET*/
	enable_irq(dd->othc_irq_ir);
}

#ifdef CONFIG_KTTECH_HEADSET // 20110506 by ssgun
static void nomic_detection_work(struct work_struct *work)
{
	int rc = 0, level = 0;
	struct pm8058_othc *dd = config[OTHC_MICBIAS_2];

	if(!run_detect || !dd) {
		pr_err("Unable to configure headset data\n");
		return;
	}

	if (dd->hs_on == 0) {
		pr_err("Rejected None Headset\n");
		return;
	}

	// NO_MIC_HEADSET 처리를 위해 5초 WAKELOCK을 설정한다.
	wake_lock_timeout(&dd->hs_idlelock, 5*HZ);

	if (dd->othc_irgpio_mickeydetect < 0) {
		OTHC_INFO("Check the MIC_BIAS status, to check if inserted or removed\n");

		/* Check the MIC_BIAS status, to check if inserted or removed */
		level = pm8058_irq_get_rt_status(dd->pm_chip, dd->othc_irq_sw);
		if (level < 0) {
			pr_err("Unable to read IRQ status register\n");
			return;
		}
	} else {
		OTHC_INFO("Check the GPIO_%d status, to check if inserted or removed\n",
					dd->othc_irgpio_mickeydetect);

		// Key Press - GPIO_46 값이 0일 경우 Key Press
		// 전제 조건으로 Headset 연결 및 Mic가 있는 경우
		level = gpio_get_value_cansleep(dd->othc_irgpio_mickeydetect);
		if (level < 0) {
			pr_err("Unable to read IR GPIO\n");
			return;
		}
		level = !level;
	}
	OTHC_INFO("NO_MIC_HEADSET Detected Level = %d\n", level);

	if(level) {
		mutex_lock(&sw_set_lock);
		do {
		dd->curr_accessory = OTHC_HEADSET; //OTHC_HEADPHONE;
		dd->curr_accessory_code = SW_HEADPHONE_INSERT;
		dd->mic_on = 0;
		dd->hs_on = 1;

		switch_set_state(&dd->othc_sdev, 2);
		input_report_switch(dd->othc_ipd, dd->curr_accessory_code, 1);
		input_sync(dd->othc_ipd);
		} while(0);
		mutex_unlock(&sw_set_lock);

		// NO_MIC_HEADSET 일 경우 MICBIAS_1을 꺼야 한다.
		msleep(100);
		rc = pm8058_micbias_enable(OTHC_MICBIAS_1, OTHC_SIGNAL_OFF);
		if(rc)
			pr_err("Disabling earmic power failed\n");
		else
			pr_debug("Disabling earmic power success\n");

OTHC_ERR("********** NO_MIC_HEADSET DETECT & REPORT **********\n");
	} else {
		unsigned long flags;

		dd->mic_on = dd->hs_on = 1; // check

		spin_lock_irqsave(&dd->lock, flags);
		dd->switch_reject = false;
		spin_unlock_irqrestore(&dd->lock, flags);

OTHC_ERR("********** HEADSET DETECT **********\n");
	}

	return;
}
#endif /*CONFIG_KTTECH_HEADSET*/

/*
 * The pm8058_no_sw detects the switch press and release operation.
 * The odd number call is press and even number call is release.
 * The current state of the button is maintained in othc_sw_state variable.
 * This isr gets called only for NO type headsets.
 */
static irqreturn_t pm8058_no_sw(int irq, void *dev_id)
{
	int level;
	struct pm8058_othc *dd = dev_id;
	unsigned long flags;

#ifdef CONFIG_KTTECH_HEADSET
	if(!run_detect || !dd) {
		pr_err("Unable to configure headset data\n");
		return IRQ_HANDLED;
	}

	/* Check if headset has been inserted, else return */
	if (!dd->othc_ir_state) {
		pr_debug("Rejected ir state\n");
		//msleep(100); // check ir_state debounce
		return IRQ_HANDLED;
	}
#endif /*CONFIG_KTTECH_HEADSET*/

	spin_lock_irqsave(&dd->lock, flags);
	if (dd->switch_reject == true) {
		pr_debug("Rejected switch interrupt\n");
		spin_unlock_irqrestore(&dd->lock, flags);
		return IRQ_HANDLED;
	}
	spin_unlock_irqrestore(&dd->lock, flags);

#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun - detect irq type
	if (dd->mic_on != 1 && dd->hs_on != 1) {
		pr_err("Rejected NO_MIC_HEADSET\n");
		return IRQ_HANDLED;
	}

	if (dd->othc_irgpio_mickeydetect < 0) {
		OTHC_INFO("Check the MIC_BIAS status, to check if inserted or removed\n");

		/* Check the MIC_BIAS status, to check if inserted or removed */
		level = pm8058_irq_get_rt_status(dd->pm_chip, dd->othc_irq_sw);
		if (level < 0) {
			pr_err("Unable to read IRQ status register\n");
			return IRQ_HANDLED;
		}
	} else {
		OTHC_INFO("Check the GPIO_%d status, to check if inserted or removed\n",
					dd->othc_irgpio_mickeydetect);

		// Key Press - GPIO_46 값이 0일 경우 Key Press
		// 전제 조건으로 Headset 연결 및 Mic가 있는 경우
		level = gpio_get_value_cansleep(dd->othc_irgpio_mickeydetect);
		if (level < 0) {
			pr_err("Unable to read IR GPIO\n");
			return IRQ_HANDLED;
		}
		level = !level;
	}
	OTHC_INFO("Headset Key Level = %d\n", level);
#else
	level = pm8058_irq_get_rt_status(dd->pm_chip, dd->othc_irq_sw);
	if (level < 0) {
		pr_err("Unable to read IRQ status register\n");
		return IRQ_HANDLED;
	}
#endif /*CONFIG_KTTECH_HEADSET*/

	if (dd->othc_support_n_switch == true) {
		if (level == 0) {
			dd->othc_sw_state = false;
			input_report_key(dd->othc_ipd, dd->sw_key_code, 0);
			input_sync(dd->othc_ipd);
			pr_debug("Headset Key Released\n");
		} else {
			disable_irq_nosync(dd->othc_irq_sw);
			schedule_work_on(0, &dd->switch_work);
			pr_debug("Do Switch Work\n");
		}
		return IRQ_HANDLED;
	}

	/*
	 * It is necessary to check the software state and the hardware state
	 * to make sure that the residual interrupt after the debounce time does
	 * not disturb the software state machine.
	 */
	if (level == 1 && dd->othc_sw_state == false) {
		/*  Switch has been pressed */
#ifdef CONFIG_KTTECH_HEADSET
		OTHC_INFO("Switch pressing\n");

		/* Start the HR timer if one is not active */
		if (hrtimer_active(&dd->timer_key)) {
			pr_debug("Cancel timer_key\n");
			hrtimer_cancel(&dd->timer_key);
		}

		// 500ms 후에도 GPIO_46 값이 LOW(0)일 경우 NO_MIC_HEADSET으로 처리한다.
		// 500ms 로 기다리지 않고 system delay를 예상하여 400ms로 설정한다.
		hrtimer_start(&dd->timer_key, ktime_set(0, 400000000), HRTIMER_MODE_REL); // 400ms
#else
		pr_debug("Switch has been pressed\n");
		dd->othc_sw_state = true;
		input_report_key(dd->othc_ipd, KEY_MEDIA, 1);
#endif /*CONFIG_KTTECH_HEADSET*/
	} else if (level == 0 && dd->othc_sw_state == true) {
		/* Switch has been released */
#ifdef CONFIG_KTTECH_HEADSET
		OTHC_INFO("Switch releasing \n");

		disable_irq_nosync(dd->othc_irq_sw);
		schedule_work(&dd->switch_work);
		pr_debug("Do Switch Work\n");
#else
		pr_debug("Switch has been released\n");
		dd->othc_sw_state = false;
		input_report_key(dd->othc_ipd, KEY_MEDIA, 0);
#endif /*CONFIG_KTTECH_HEADSET*/
	}
#ifndef CONFIG_KTTECH_HEADSET
	input_sync(dd->othc_ipd);
#endif /*CONFIG_KTTECH_HEADSET*/

	return IRQ_HANDLED;
}

/*
 * The pm8058_nc_ir detects insert / remove of the headset (for NO),
 * The current state of the headset is maintained in othc_ir_state variable.
 * Due to a hardware bug, false switch interrupts are seen during headset
 * insert. This is handled in the software by rejecting the switch interrupts
 * for a small period of time after the headset has been inserted.
 */
static irqreturn_t pm8058_nc_ir(int irq, void *dev_id)
{
	unsigned long flags, rc;
	struct pm8058_othc *dd = dev_id;

#ifdef CONFIG_KTTECH_HEADSET
	if(!run_detect || !dd) {
		pr_err("Unable to configure headset data\n");
		return IRQ_HANDLED;
	}
#endif /*CONFIG_KTTECH_HEADSET*/

	spin_lock_irqsave(&dd->lock, flags);
	/* Enable the switch reject flag */
	dd->switch_reject = true;
	spin_unlock_irqrestore(&dd->lock, flags);

	/* Start the HR timer if one is not active */
	if (hrtimer_active(&dd->timer)) {
		pr_debug("Cancel timer. Switch Debounce\n");
		hrtimer_cancel(&dd->timer);
	}

	hrtimer_start(&dd->timer,
		ktime_set((dd->switch_debounce_ms / 1000),
		(dd->switch_debounce_ms % 1000) * 1000000), HRTIMER_MODE_REL);

	/* disable irq, this gets enabled in the workqueue */
	disable_irq_nosync(dd->othc_irq_ir);

#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun - detect irq type
	if (dd->othc_irgpio_eardetect < 0) {
		OTHC_INFO("Check the MIC_BIAS status, to check if inserted or removed\n");

		/* Check the MIC_BIAS status, to check if inserted or removed */
		rc = pm8058_irq_get_rt_status(dd->pm_chip, dd->othc_irq_ir);
		if (rc < 0) {
			pr_err("Unable to read IR status\n");
			goto fail_ir;
		}
	} else {
		OTHC_INFO("Check the GPIO_%d status, to check if inserted or removed\n",
					dd->othc_irgpio_eardetect);

		rc = gpio_get_value_cansleep(dd->othc_irgpio_eardetect);
		if (rc < 0) {
			pr_err("Unable to read IR GPIO\n");
			goto fail_ir;
		}
		rc = !rc;
	}
#else
	/* Check the MIC_BIAS status, to check if inserted or removed */
	rc = pm8058_irq_get_rt_status(dd->pm_chip, dd->othc_irq_ir);
	if (rc < 0) {
		pr_err("Unable to read IR status\n");
		goto fail_ir;
	}
#endif /*CONFIG_KTTECH_HEADSET*/

	dd->othc_ir_state = rc;
	schedule_delayed_work_on(0, &dd->detect_work,
				msecs_to_jiffies(dd->detection_delay_ms));

fail_ir:
	return IRQ_HANDLED;
}

static int pm8058_configure_micbias(struct pm8058_othc *dd)
{
	int rc;
	u8 reg, value;
#ifndef CONFIG_KTTECH_HEADSET // 20110428 by ssgun - test code : disable switch
	u32 value1;
#endif /*CONFIG_KTTECH_HEADSET*/
	u16 base_addr = dd->othc_base;
#ifndef CONFIG_KTTECH_HEADSET // 20110428 by ssgun - test code : disable switch
	struct hsed_bias_config *hsed_config =
			dd->othc_pdata->hsed_config->hsed_bias_config;
#endif /*CONFIG_KTTECH_HEADSET*/

OTHC_INFO("micbias_enable = %d\n", dd->othc_pdata->micbias_enable);

#ifndef CONFIG_KTTECH_HEADSET // 20110428 by ssgun - disable switch micbias
	/* Intialize the OTHC module */
	/* Control Register 1*/
	rc = pm8058_read(dd->pm_chip, base_addr, &reg, 1);
	if (rc < 0) {
		pr_err("PM8058 read failed\n");
		return rc;
	}

	/* set iDAC high current threshold */
	value = (hsed_config->othc_highcurr_thresh_uA / 100) - 2;
	reg =  (reg & PM8058_OTHC_HIGH_CURR_MASK) | value;

	rc = pm8058_write(dd->pm_chip, base_addr, &reg, 1);
	if (rc < 0) {
		pr_err("PM8058 read failed\n");
		return rc;
	}

	/* Control register 2*/
	rc = pm8058_read(dd->pm_chip, base_addr + 1, &reg, 1);
	if (rc < 0) {
		pr_err("PM8058 read failed\n");
		return rc;
	}
#endif /*CONFIG_KTTECH_HEADSET*/

	value = dd->othc_pdata->micbias_enable;
	reg &= PM8058_OTHC_EN_SIG_MASK;
	reg |= (value << PM8058_OTHC_EN_SIG_SHIFT);

#ifndef CONFIG_KTTECH_HEADSET // 20110428 by ssgun - disable switch micbias
	value = 0;
	value1 = (hsed_config->othc_hyst_prediv_us << 10) / USEC_PER_SEC;
	while (value1 != 0) {
		value1 = value1 >> 1;
		value++;
	}
	if (value > 7) {
		pr_err("Invalid input argument - othc_hyst_prediv_us\n");
		return -EINVAL;
	}
	reg &= PM8058_OTHC_HYST_PREDIV_MASK;
	reg |= (value << PM8058_OTHC_HYST_PREDIV_SHIFT);

	value = 0;
	value1 = (hsed_config->othc_period_clkdiv_us << 10) / USEC_PER_SEC;
	while (value1 != 1) {
		value1 = value1 >> 1;
		value++;
	}
	if (value > 8) {
		pr_err("Invalid input argument - othc_period_clkdiv_us\n");
		return -EINVAL;
	}
	reg = (reg &  PM8058_OTHC_CLK_PREDIV_MASK) | (value - 1);
#endif /*CONFIG_KTTECH_HEADSET*/

	rc = pm8058_write(dd->pm_chip, base_addr + 1, &reg, 1);
	if (rc < 0) {
		pr_err("PM8058 read failed\n");
		return rc;
	}

#ifndef CONFIG_KTTECH_HEADSET // 20110428 by ssgun - disable switch micbias
	/* Control register 3 */
	rc = pm8058_read(dd->pm_chip, base_addr + 2 , &reg, 1);
	if (rc < 0) {
		pr_err("PM8058 read failed\n");
		return rc;
	}

	value = hsed_config->othc_hyst_clk_us /
					hsed_config->othc_hyst_prediv_us;
	if (value > 15) {
		pr_err("Invalid input argument - othc_hyst_prediv_us\n");
		return -EINVAL;
	}
	reg &= PM8058_OTHC_HYST_CLK_MASK;
	reg |= value << PM8058_OTHC_HYST_CLK_SHIFT;

	value = hsed_config->othc_period_clk_us /
					hsed_config->othc_period_clkdiv_us;
	if (value > 15) {
		pr_err("Invalid input argument - othc_period_clkdiv_us\n");
		return -EINVAL;
	}
	reg = (reg & PM8058_OTHC_PERIOD_CLK_MASK) | value;

	rc = pm8058_write(dd->pm_chip, base_addr + 2, &reg, 1);
	if (rc < 0) {
		pr_err("PM8058 read failed\n");
		return rc;
	}
#endif /*CONFIG_KTTECH_HEADSET*/

	return 0;
}

static ssize_t othc_headset_print_name(struct switch_dev *sdev, char *buf)
{
	switch (switch_get_state(sdev)) {
	case OTHC_NO_DEVICE:
		return sprintf(buf, "No Device\n");
	case OTHC_HEADSET:
	case OTHC_HEADPHONE:
	case OTHC_MICROPHONE:
	case OTHC_ANC_HEADSET:
	case OTHC_ANC_HEADPHONE:
	case OTHC_ANC_MICROPHONE:
		return sprintf(buf, "Headset\n");
	}
	return -EINVAL;
}

static int pm8058_configure_switch(struct pm8058_othc *dd)
{
	int rc, i;

	if (dd->othc_support_n_switch == true) {
OTHC_INFO("n-switch support\n");

		/* n-switch support */
		rc = adc_channel_open(dd->switch_config->adc_channel,
							&dd->adc_handle);
		if (rc) {
			pr_err("Unable to open ADC channel\n");
			return -ENODEV;
		}

		for (i = 0; i < dd->switch_config->num_keys; i++) {
			input_set_capability(dd->othc_ipd, EV_KEY,
				dd->switch_config->switch_info[i].key_code);
		}
	} else { /* Only single switch supported */
OTHC_INFO("Only single switch supported\n");

		input_set_capability(dd->othc_ipd, EV_KEY, KEY_MEDIA);
	}

	return 0;
}

static int pm8058_configure_accessory(struct pm8058_othc *dd)
{
#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun - detect irq type
	int rc;
#else
	int i, rc;
	char name[OTHC_GPIO_MAX_LEN];
#endif /*CONFIG_KTTECH_HEADSET*/

OTHC_INFO("num_accessories = %d\n", dd->num_accessories);

	/*
	 * Not bailing out if the gpio_* configure calls fail. This is required
	 * as multiple accessories are detected by the same gpio.
	 */
#ifdef CONFIG_KTTECH_HEADSET
	input_set_capability(dd->othc_ipd, EV_SW, SW_HEADPHONE_INSERT);
	input_set_capability(dd->othc_ipd, EV_SW, SW_MICROPHONE_INSERT);
#else
	for (i = 0; i < dd->num_accessories; i++) {
		if (dd->accessory_info[i].enabled == false)
			continue;
#ifndef CONFIG_KTTECH_HEADSET // 20110423 by ssgun - detect irq type
		if (dd->accessory_info[i].detect_flags & OTHC_GPIO_DETECT) {
			snprintf(name, OTHC_GPIO_MAX_LEN, "%s%d",
							"othc_acc_gpio_", i);
			rc = gpio_request(dd->accessory_info[i].gpio, name);
			if (rc) {
				pr_err("Unable to request GPIO [%d]\n",
						dd->accessory_info[i].gpio);
				continue;
			}
			rc = gpio_direction_input(dd->accessory_info[i].gpio);
			if (rc) {
				pr_err("Unable to set-direction GPIO [%d]\n",
						dd->accessory_info[i].gpio);
				gpio_free(dd->accessory_info[i].gpio);
				continue;
			}
		}
#endif /*CONFIG_KTTECH_HEADSET*/
		input_set_capability(dd->othc_ipd, EV_SW,
					dd->accessory_info[i].key_code);
	}
#endif /*CONFIG_KTTECH_HEADSET*/

	if (dd->accessories_adc_support) {
		/*
		 * Check if 3 switch is supported. If both are using the same
		 * ADC channel, the same handle can be used.
		 */
		if (dd->othc_support_n_switch) {
			if (dd->adc_handle != NULL &&
				(dd->accessories_adc_channel ==
				 dd->switch_config->adc_channel))
				dd->accessory_adc_handle = dd->adc_handle;
		} else {
			rc = adc_channel_open(dd->accessories_adc_channel,
						&dd->accessory_adc_handle);
			if (rc) {
				pr_err("Unable to open ADC channel\n");
				rc = -ENODEV;
				goto accessory_adc_fail;
			}
		}
		if (dd->video_out_gpio != 0) {
			rc = gpio_request(dd->video_out_gpio, "vout_enable");
			if (rc < 0) {
				pr_err("request VOUT gpio failed (%d)\n", rc);
				goto accessory_adc_fail;
			}
			rc = gpio_direction_output(dd->video_out_gpio, 0);
			if (rc < 0) {
				pr_err("direction_out failed (%d)\n", rc);
				goto accessory_adc_fail;
			}
		}
	}

	return 0;

accessory_adc_fail:
#ifndef CONFIG_KTTECH_HEADSET // 20110423 by ssgun - detect irq type
	for (i = 0; i < dd->num_accessories; i++) {
		if (dd->accessory_info[i].enabled == false)
			continue;
		gpio_free(dd->accessory_info[i].gpio);
	}
#endif /*CONFIG_KTTECH_HEADSET*/
	return rc;
}

static int
othc_configure_hsed(struct pm8058_othc *dd, struct platform_device *pd)
{
	int rc;
	struct input_dev *ipd;
	struct pmic8058_othc_config_pdata *pdata = pd->dev.platform_data;
	struct othc_hsed_config *hsed_config = pdata->hsed_config;

OTHC_INFO("config h2w device\n");

	dd->othc_sdev.name = "h2w";
	dd->othc_sdev.print_name = othc_headset_print_name;

	rc = switch_dev_register(&dd->othc_sdev);
	if (rc) {
		pr_err("Unable to register switch device\n");
		return rc;
	}

	ipd = input_allocate_device();
	if (ipd == NULL) {
		pr_err("Unable to allocate memory\n");
		rc = -ENOMEM;
		goto fail_input_alloc;
	}

	/* Get the IRQ for Headset Insert-remove and Switch-press */
	dd->othc_irq_sw = platform_get_irq(pd, 0);
	dd->othc_irq_ir = platform_get_irq(pd, 1);
	if (dd->othc_irq_ir < 0 || dd->othc_irq_sw < 0) {
		pr_err("othc resource:IRQs absent\n");
		rc = -ENXIO;
		goto fail_micbias_config;
	}

	if (pdata->hsed_name != NULL)
		ipd->name = pdata->hsed_name;
	else
		ipd->name = "pmic8058_othc";

	ipd->phys = "pmic8058_othc/input0";
	ipd->dev.parent = &pd->dev;

	dd->othc_ipd = ipd;
	dd->othc_sw_state = false;
	dd->switch_debounce_ms = hsed_config->switch_debounce_ms;
	dd->othc_support_n_switch = hsed_config->othc_support_n_switch;
	dd->accessory_support = pdata->hsed_config->accessories_support;
	dd->detection_delay_ms = pdata->hsed_config->detection_delay_ms;

	if (dd->othc_support_n_switch == true)
		dd->switch_config = hsed_config->switch_config;

	if (dd->accessory_support == true) {
		dd->accessory_info = pdata->hsed_config->accessories;
		dd->num_accessories = pdata->hsed_config->othc_num_accessories;
		dd->accessories_adc_support =
				pdata->hsed_config->accessories_adc_support;
		dd->accessories_adc_channel =
				pdata->hsed_config->accessories_adc_channel;
		dd->video_out_gpio = pdata->hsed_config->video_out_gpio;
	}

#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun - detect irq type
	dd->othc_irgpio_eardetect = 99;
	dd->othc_irgpio_mickeydetect = 46;
#endif /*CONFIG_KTTECH_HEADSET*/

	/* Configure the MIC_BIAS line for headset detection */
	rc = pm8058_configure_micbias(dd);
	if (rc < 0)
		goto fail_micbias_config;

	/* Configure for the switch events */
	rc = pm8058_configure_switch(dd);
	if (rc < 0)
		goto fail_micbias_config;

	/* Configure the accessory */
	if (dd->accessory_support == true) {
		rc = pm8058_configure_accessory(dd);
		if (rc < 0)
			goto fail_micbias_config;
	}

	input_set_drvdata(ipd, dd);
	spin_lock_init(&dd->lock);

	rc = input_register_device(ipd);
	if (rc) {
		pr_err("Unable to register OTHC device\n");
		goto fail_micbias_config;
	}

	OTHC_DBG("Create Timer\n");
	hrtimer_init(&dd->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	dd->timer.function = pm8058_othc_timer;

#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun - detect irq type
	hrtimer_init(&dd->timer_nomic, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	dd->timer_nomic.function = pm8058_othc_timer_nomic;

	hrtimer_init(&dd->timer_key, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	dd->timer_key.function = pm8058_othc_timer_key;

	wake_lock_init(&dd->hs_idlelock, WAKE_LOCK_SUSPEND, "headset_idlelock");

	OTHC_INFO("hw_ver = %d\n", hw_ver);
	if(hw_ver < O3_PP1) {
		rc = gpio_request(106, "othc_irgpio_106");
 		if (rc) {
 			pr_err("Unable to request IR GPIO_106\n");
 		} else {
			rc = gpio_direction_output(106, 1);
			if (rc) {
				pr_err("GPIO_106 set_direction failed\n");
			}
		}
	}

#ifdef CONFIG_KTTECH_HEADSET  // 20110502 by ssgun - reset 방지
	INIT_DELAYED_WORK(&dd->detect_work, detect_work_f);
	INIT_WORK(&dd->switch_work, switch_work_f);

	g_nomic_detection_work_queue = create_workqueue("hs_detection");
	if (g_nomic_detection_work_queue == NULL) {
		rc = -ENOMEM;
		goto fail_ir_status;
	}
#endif /*CONFIG_KTTECH_HEADSET*/

#if 0 // 20110509 by ssgun
	/* Request the HEADSET IR interrupt */
	if (dd->othc_irgpio_eardetect < 0) {
		OTHC_INFO("[PMIC] Request the HEADSET IR interrupt\n");

		rc = request_threaded_irq(dd->othc_irq_ir, NULL, pm8058_nc_ir,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_DISABLED,
					"pm8058_othc_ir", dd);
		if (rc < 0) {
			pr_err("Unable to request pm8058_othc_ir IRQ\n");
			goto fail_ir_irq;
		}
	} else {
		OTHC_INFO("[GPIO] Request the HEADSET IR_GPIO interrupt\n");

		rc = gpio_request(dd->othc_irgpio_eardetect, "h2w_detect");
		if (rc) {
			pr_err("Unable to request IR GPIO_EAR\n");
			goto fail_ir_gpio_req;
		}
OTHC_ERR("GPIO_%d = %d\n", dd->othc_irgpio_eardetect, gpio_get_value(dd->othc_irgpio_eardetect));

#if 0 // tlmm config와 중복 설정
		rc = gpio_direction_input(dd->othc_irgpio_eardetect);
		if (rc) {
			pr_err("GPIO %d set_direction failed\n", dd->othc_irgpio_eardetect);
			goto fail_ir_irq;
		}
		gpio_set_value_cansleep(dd->othc_irgpio_eardetect, 1); // Initial Value : HIGH
#endif

		dd->othc_irq_ir = gpio_to_irq(dd->othc_irgpio_eardetect);
		set_irq_wake(dd->othc_irq_ir, 1);

#if 0 // 20110502 by ssgun - reset 방지
		rc = request_any_context_irq(dd->othc_irq_ir, pm8058_nc_ir,
		IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
				"pm8058_othc_ir", dd);
		if (rc < 0) {
			pr_err("could not request hs irq err=%d\n", rc);
			goto fail_ir_irq;
		}
#else
		rc = request_threaded_irq(dd->othc_irq_ir, NULL, pm8058_nc_ir,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_DISABLED,
					"pm8058_othc_ir", dd);
		if (rc < 0) {
			pr_err("Unable to request pm8058_othc_ir IRQ\n");
			goto fail_ir_irq;
		}
#endif
	}

	/* Request the  SWITCH press/release interrupt */
	if (dd->othc_irgpio_mickeydetect < 0) {
		OTHC_INFO("[PMIC] Request the SWITCH press/release interrupt\n");

		rc = request_threaded_irq(dd->othc_irq_sw, NULL, pm8058_no_sw,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_DISABLED,
				"pm8058_othc_sw", dd);
		if (rc < 0) {
			pr_err("Unable to request pm8058_othc_sw IRQ\n");
			goto fail_sw_irq;
		}
	} else {
		OTHC_INFO("[GPIO] Request the SWITCH press/release IR_GPIO interrupt\n");

		rc = gpio_request(dd->othc_irgpio_mickeydetect, "h2w_button");
		if (rc) {
			pr_err("Unable to request IR GPIO_MICKEY\n");
			goto fail_ir_gpio_req;
		}
OTHC_ERR("GPIO_%d = %d\n", dd->othc_irgpio_mickeydetect, gpio_get_value(dd->othc_irgpio_mickeydetect));

#if 0 // tlmm config와 중복 설정
		rc = gpio_direction_input(dd->othc_irgpio_mickeydetect);
		if (rc) {
			pr_err("GPIO %d set_direction failed\n", dd->othc_irgpio_mickeydetect);
			goto fail_ir_irq;
		}

		gpio_set_value_cansleep(dd->othc_irgpio_mickeydetect, 0); // Initial Value : LOW
#endif

		dd->othc_irq_sw = gpio_to_irq(dd->othc_irgpio_mickeydetect);
		set_irq_wake(dd->othc_irq_sw, 1);

#if 0 // 20110502 by ssgun - reset 방지
		rc = request_any_context_irq(dd->othc_irq_sw, pm8058_no_sw,
		IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
				"pm8058_othc_sw", dd);
		if (rc < 0) {
			pr_err("could not request hs irq err=%d\n", rc);
			goto fail_ir_irq;
		}
#else
		rc = request_threaded_irq(dd->othc_irq_sw, NULL, pm8058_no_sw,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_DISABLED,
				"pm8058_othc_sw", dd);
		if (rc < 0) {
			pr_err("Unable to request pm8058_othc_sw IRQ\n");
			goto fail_sw_irq;
		}
#endif
	}
#else
	/* Request the HEADSET IR interrupt */
	rc = request_threaded_irq(dd->othc_irq_ir, NULL, pm8058_nc_ir,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_DISABLED,
				"pm8058_othc_ir", dd);
	if (rc < 0) {
		pr_err("Unable to request pm8058_othc_ir IRQ\n");
		goto fail_ir_irq;
	}
	set_irq_wake(dd->othc_irq_ir, 1);

	/* Request the	SWITCH press/release interrupt */
	rc = request_threaded_irq(dd->othc_irq_sw, NULL, pm8058_no_sw,
	IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_DISABLED,
			"pm8058_othc_sw", dd);
	if (rc < 0) {
		pr_err("Unable to request pm8058_othc_sw IRQ\n");
		goto fail_sw_irq;
	}
	set_irq_wake(dd->othc_irq_sw, 1);
#endif
	OTHC_INFO("othc_irq_ir = %d, othc_irq_sw = %d\n", dd->othc_irq_ir, dd->othc_irq_sw);

	msleep(300); // get gpio debounce

	/* Check if the accessory is already inserted during boot up */
	if (dd->othc_irgpio_eardetect < 0) {
		OTHC_INFO("[PMIC] Check if the accessory is already inserted during boot up\n");

		rc = pm8058_irq_get_rt_status(dd->pm_chip, dd->othc_irq_ir);
		if (rc < 0) {
			pr_err("Unable to get accessory status at boot\n");
			goto fail_ir_status;
		}
	} else {
		OTHC_INFO("[GPIO] Check if the accessory is already inserted during boot up\n");

		rc = gpio_get_value_cansleep(dd->othc_irgpio_eardetect);
		if (rc < 0) {
			pr_err("Unable to get accessory status at boot\n");
			goto fail_ir_status;
		}
		rc = !rc;
	}
#else
	/* Request the HEADSET IR interrupt */
	rc = request_threaded_irq(dd->othc_irq_ir, NULL, pm8058_nc_ir,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_DISABLED,
				"pm8058_othc_ir", dd);
	if (rc < 0) {
		pr_err("Unable to request pm8058_othc_ir IRQ\n");
		goto fail_ir_irq;
	}

	/* Request the  SWITCH press/release interrupt */
	rc = request_threaded_irq(dd->othc_irq_sw, NULL, pm8058_no_sw,
	IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_DISABLED,
			"pm8058_othc_sw", dd);
	if (rc < 0) {
		pr_err("Unable to request pm8058_othc_sw IRQ\n");
		goto fail_sw_irq;
	}

	/* Check if the accessory is already inserted during boot up */
	rc = pm8058_irq_get_rt_status(dd->pm_chip, dd->othc_irq_ir);
	if (rc < 0) {
		pr_err("Unable to get accessory status at boot\n");
		goto fail_ir_irq;
	}
#endif /*CONFIG_KTTECH_HEADSET*/
	if (rc) {
		pr_debug("Accessory inserted during boot up\n");
		/* process the data and report the inserted accessory */
		rc = pm8058_accessory_report(dd, 1);
		if (rc)
			pr_debug("Unabele to detect accessory at boot up\n");
#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun - detect irq type
		else
			dd->othc_ir_state = 1;
#endif /*CONFIG_KTTECH_HEADSET*/
	}

	device_init_wakeup(&pd->dev,
			hsed_config->hsed_bias_config->othc_wakeup);

#ifndef CONFIG_KTTECH_HEADSET  // 20110502 by ssgun - reset 방지
	INIT_DELAYED_WORK(&dd->detect_work, detect_work_f);

	if (dd->othc_support_n_switch == true)
		INIT_WORK(&dd->switch_work, switch_work_f);
#endif /*CONFIG_KTTECH_HEADSET*/

	return 0;

#ifdef CONFIG_KTTECH_HEADSET
fail_ir_status:
	free_irq(dd->othc_irq_sw, dd);
#endif /*CONFIG_KTTECH_HEADSET*/
fail_sw_irq:
	free_irq(dd->othc_irq_ir, dd);
fail_ir_irq:
#ifdef CONFIG_KTTECH_HEADSET // 20110423 by ssgun - detect irq type
	if (dd->othc_irgpio_eardetect != -1) {
		gpio_free(dd->othc_irgpio_eardetect);
	}
	if (dd->othc_irgpio_mickeydetect != -1) {
		gpio_free(dd->othc_irgpio_mickeydetect);
	}
	if(hw_ver < O3_PP1) {
		gpio_free(106);
	}
	destroy_workqueue(g_nomic_detection_work_queue);
#if 0 // 20110509 by ssgun
fail_ir_gpio_req:
#endif
#endif /*CONFIG_KTTECH_HEADSET*/
	input_unregister_device(ipd);
	dd->othc_ipd = NULL;
fail_micbias_config:
	input_free_device(ipd);
fail_input_alloc:
	switch_dev_unregister(&dd->othc_sdev);
	return rc;
}

static int __devinit pm8058_othc_probe(struct platform_device *pd)
{
	int rc;
	struct pm8058_othc *dd;
	struct pm8058_chip *chip;
	struct resource *res;
	struct pmic8058_othc_config_pdata *pdata = pd->dev.platform_data;

OTHC_INFO("micbias_capability = %d\n", pdata->micbias_capability);

	chip = platform_get_drvdata(pd);
	if (chip == NULL) {
		pr_err("Invalid driver information\n");
		return  -EINVAL;
	}

	/* Check PMIC8058 version. A0 version is not supported */
	if (pm8058_rev(chip) == PM_8058_REV_1p0) {
		pr_err("PMIC8058 version not supported\n");
		return -ENODEV;
	}

	if (pdata == NULL) {
		pr_err("Platform data not present\n");
		return -EINVAL;
	}

	dd = kzalloc(sizeof(*dd), GFP_KERNEL);
	if (dd == NULL) {
		pr_err("Unable to allocate memory\n");
		return -ENOMEM;
	}

	/* Enable runtime PM ops, start in ACTIVE mode */
	rc = pm_runtime_set_active(&pd->dev);
	if (rc < 0)
		dev_dbg(&pd->dev, "unable to set runtime pm state\n");
	pm_runtime_enable(&pd->dev);

	res = platform_get_resource_byname(pd, IORESOURCE_IO, "othc_base");
	if (res == NULL) {
		pr_err("othc resource:Base address absent\n");
		rc = -ENXIO;
		goto fail_get_res;
	}

	dd->othc_pdata = pdata;
	dd->pm_chip = chip;
	dd->othc_base = res->start;
	if (pdata->micbias_regulator == NULL) {
		pr_err("OTHC regulator not specified\n");
		goto fail_get_res;
	}

	dd->othc_vreg = regulator_get(NULL,
				pdata->micbias_regulator->regulator);
	if (IS_ERR(dd->othc_vreg)) {
		pr_err("regulator get failed\n");
		rc = PTR_ERR(dd->othc_vreg);
		goto fail_get_res;
	}

	rc = regulator_set_voltage(dd->othc_vreg,
				pdata->micbias_regulator->min_uV,
				pdata->micbias_regulator->max_uV);
	if (rc) {
		pr_err("othc regulator set voltage failed\n");
		goto fail_reg_enable;
	}

	rc = regulator_enable(dd->othc_vreg);
	if (rc) {
		pr_err("othc regulator enable failed\n");
		goto fail_reg_enable;
	}

	platform_set_drvdata(pd, dd);

	if (pdata->micbias_capability == OTHC_MICBIAS_HSED) {
		/* HSED to be supported on this MICBIAS line */
		if (pdata->hsed_config != NULL) {
			rc = othc_configure_hsed(dd, pd);
			if (rc < 0) {
				pr_err("HSED configure failed\n");
				goto fail_othc_hsed;
			}
		} else {
			pr_err("HSED config data not present\n");
			rc = -EINVAL;
			goto fail_othc_hsed;
		}
	}

	/* Store the local driver data structure */
	if (dd->othc_pdata->micbias_select < OTHC_MICBIAS_MAX)
		config[dd->othc_pdata->micbias_select] = dd;

	pr_debug("Device %s:%d:%d successfully registered\n",
					pd->name, pd->id, dd->othc_pdata->micbias_select);

#ifdef CONFIG_KTTECH_HEADSET // 20110501 by ssgun
	if(dd->othc_pdata->micbias_select == OTHC_MICBIAS_2) {
		run_detect = true;
	}
#endif /*CONFIG_KTTECH_HEADSET*/

	return 0;

fail_othc_hsed:
	regulator_disable(dd->othc_vreg);
fail_reg_enable:
	regulator_put(dd->othc_vreg);
fail_get_res:
	pm_runtime_set_suspended(&pd->dev);
	pm_runtime_disable(&pd->dev);

	kfree(dd);
	return rc;
}

static struct platform_driver pm8058_othc_driver = {
	.driver = {
		.name = "pm8058-othc",
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &pm8058_othc_pm_ops,
#endif
	},
	.probe = pm8058_othc_probe,
	.remove = __devexit_p(pm8058_othc_remove),
};

static int __init pm8058_othc_init(void)
{
OTHC_DBG("%s\n", __func__);

#ifdef CONFIG_KTTECH_HEADSET
	mutex_init(&micbias_lock);
	mutex_init(&sw_set_lock);
	mutex_init(&state_lock);

	run_detect = false;

#if 0 // 20110507 by ssgun - 초기 설정 원복
	gpio_tlmm_config(GPIO_CFG( 46, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_8MA), GPIO_CFG_ENABLE);
	gpio_tlmm_config(GPIO_CFG( 99, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_8MA), GPIO_CFG_ENABLE);
#else
	gpio_tlmm_config(GPIO_CFG( 46, 0, GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
	gpio_tlmm_config(GPIO_CFG( 99, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
#endif

	hw_ver = get_kttech_hw_version();
	pr_err("hw_ver = %d\n", hw_ver);
	if(hw_ver < O3_PP1)
	{
		gpio_tlmm_config(GPIO_CFG(106, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
	}
#endif /*CONFIG_KTTECH_HEADSET*/

	return platform_driver_register(&pm8058_othc_driver);
}

static void __exit pm8058_othc_exit(void)
{
	platform_driver_unregister(&pm8058_othc_driver);
}
/*
 * Move to late_initcall, to make sure that the ADC driver registration is
 * completed before we open a ADC channel.
 */
late_initcall(pm8058_othc_init);
module_exit(pm8058_othc_exit);

MODULE_ALIAS("platform:pmic8058_othc");
MODULE_DESCRIPTION("PMIC 8058 OTHC");
MODULE_LICENSE("GPL v2");
