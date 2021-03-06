#
# Makefile fragment for Linux 2.6
# Broadcom DHD Driver
#
# Copyright (C) 1999-2010, Broadcom Corporation
# 
#      Unless you and Broadcom execute a separate written software license
# agreement governing use of this software, this software is licensed to you
# under the terms of the GNU General Public License version 2 (the "GPL"),
# available at http://www.broadcom.com/licenses/GPLv2.php, with the
# following added to such license:
# 
#      As a special exception, the copyright holders of this software give you
# permission to link this software with independent modules, and to copy and
# distribute the resulting executable under terms of your choice, provided that
# you also meet, for each linked independent module, the terms and conditions of
# the license of that module.  An independent module is a module which is not
# derived from this software.  The special exception does not apply to any
# modifications of the software.
# 
#      Notwithstanding the above, under no circumstances may you combine this
# software in any way with any other Broadcom software provided under a license
# other than the GPL, without Broadcom's express prior written consent.
#
# $Id: makefile.26,v 1.1.144.1 2008/05/07 22:53:44 Exp $

SRCBASE := drivers/net/wireless/bcm4329

WLANCFLAGS = -DLINUX -DSRCBASE=\"$(SRCBASE)\" -DBCMDRIVER -DBCMDONGLEHOST     \
	-DDHDTHREAD -DBCMWPA2 -DBCMWAPI_WPI -DUNRELEASEDCHIP -DDHD_GPL        \
	-DDHD_SCHED -DBDC -DTOE -DDHD_BCMEVENTS -DSHOW_EVENTS -DBCMSDIO       \
	-DBCMLXSDMMC -DBCMPLATFORM_BUS -DEMBEDDED_PLATFORM                    \
	-DARP_OFFLOAD_SUPPORT -DPKT_FILTER_SUPPORT			      \
	-fshort-wchar -Wall -Wstrict-prototypes -Werror
	
WLANCFLAGS += -I$(SRCBASE)/dhd/sys \
	-I$(SRCBASE)/dongle  \
	-I$(SRCBASE)/include \
	-I$(SRCBASE)/include/proto \
	-I$(SRCBASE)/shared \
	-I$(SRCBASE)/wl/sys

WLANOFILES := \
	bcmsdio/sys/bcmsdh.o \
	bcmsdio/sys/bcmsdh_linux.o \
	bcmsdio/sys/bcmsdh_sdmmc.o \
	bcmsdio/sys/bcmsdh_sdmmc_linux.o \
	dhd/sys/dhd_cdc.o \
	dhd/sys/dhd_common.o \
	dhd/sys/dhd_custom_gpio.o \
	dhd/sys/dhd_linux.o \
	dhd/sys/dhd_linux_sched.o \
	dhd/sys/dhd_sdio.o \
	shared/aiutils.o \
	shared/bcmutils.o \
	shared/bcmwifi.o \
	shared/hndpmu.o \
	shared/linux_osl.o \
	shared/sbutils.o \
	shared/siutils.o \
	wl/sys/wl_iw.o


ifeq ($(CONFIG_BCM4329_DEBUG),y)
WLANCFLAGS += -DDHD_DEBUG -DSDTEST
endif

ifeq ($(CONFIG_BCM4329_OOB_ONLY),y)
WLANCFLAGS += -DOOB_INTR_ONLY -DMMC_SDIO_ABORT -DCUSTOM_OOB_GPIO_NUM=$(CONFIG_BCM4329_OOB_GPIO)
else
WLANCFLAGS += -DSDIO_ISR_THREAD
endif

ifeq ($(CONFIG_BCM4329_SOFTAP),y)
WLANCFLAGS += -DSOFTAP
endif

ifeq ($(CONFIG_MMC_MSM7X00A),y)
WLANCFLAGS += -Dlinux
WLANCFLAGS += -DDHD_SDALIGN=64 -DMAX_HDR_READ=64 -DDHD_FIRSTREAD=64
endif

ifeq ($(CONFIG_BCM4329_MEM_PREALLOC),y)
WLANCFLAGS += -DDHD_USE_STATIC_BUF
endif

obj-$(CONFIG_BCM4329) += wlan.o
wlan-objs = $(WLANOFILES)
EXTRA_CFLAGS = $(WLANCFLAGS)
EXTRA_LDFLAGS += --strip-debug
